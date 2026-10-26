#include <stdbool.h>
#include <stddef.h>

#include <tinysd/bits.h>
#include <tinysd/commands.h>
#include <tinysd/delay.h>

#include <tinysd/sdio.h>

// Bytes of the common CIS pointer
#define COMMON_CIS_POINTER_LEN 3

// End of the CIS area, past which no tuple lies
#define CIS_AREA_END 0x18000u

// Bytes ahead of each tuple's body, the code and the link
#define TUPLE_HEADER_LEN 2

// Tuple link that marks the last tuple of the chain
#define CIS_LINK_END 0xff

// Bytes of the manufacturer tuple read, two little-endian 16-bit ids
#define MANFID_TUPLE_LEN 4

// Function 0 extension tuple fields, from Table 16-6 of the SDIO specification
#define TPLFE_TYPE_FUNCTION_0       0x00
#define TPLFE_MAX_TRAN_SPEED_OFFSET 3

// TPLFE_MAX_TRAN_SPEED fields, from Section 16.7.3 of the SDIO specification
#define TRAN_SPEED_UNIT       TSD_BITS(2, 0)
#define TRAN_SPEED_TIME_VALUE TSD_BITS(6, 3)

// Longest wait for the card to power up, in milliseconds
#define POWER_UP_TIMEOUT_MS 1000

// Delay between tries while the card powers up, in microseconds
#define POWER_UP_DELAY_US 10000

// Delay before the first command once the clock runs, covering the 1 ms and 74 clocks the SD specification asks for
#define FIRST_COMMAND_DELAY_US 1000

// Clock the card is enumerated at, in kHz
#define ENUMERATION_CLOCK_KHZ 400

// Fastest clock in default speed, in kHz
#define DEFAULT_SPEED_MAX_KHZ 25000

// Units of TPLFE_MAX_TRAN_SPEED in kHz, with 0 for a reserved unit
static const uint32_t tran_speed_units_khz[8] = {100, 1000, 10000, 100000, 0, 0, 0, 0};

// Time values of TPLFE_MAX_TRAN_SPEED in tenths, with 0 for the reserved value
static const uint8_t tran_speed_time_values[16] = {0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80};

// Record the fields the driver uses from the common CIS at address, walking its tuples to the end of the chain
static int read_common_cis(struct tsd_sdio *card, uint32_t address)
{
  // Walk until the end of the chain or of the CIS area, leaving any field not found at 0
  while (address < CIS_AREA_END) {
    // Read the tuple's code and stop at the end of the chain
    uint8_t code;
    int ret = tsd_sdio_read_f0(card, address, &code);
    if (ret < 0)
      return ret;
    if (code == TSD_SDIO_CISTPL_END)
      return 0;

    // Step over a filler, which has no link
    if (code == TSD_SDIO_CISTPL_NULL) {
      address++;
      continue;
    }

    // Read the link to the next tuple
    uint8_t link;
    ret = tsd_sdio_read_f0(card, address + 1, &link);
    if (ret < 0)
      return ret;

    // Take the ids from the manufacturer tuple, two little-endian 16-bit values
    if (code == TSD_SDIO_CISTPL_MANFID && link >= MANFID_TUPLE_LEN) {
      uint8_t bytes[MANFID_TUPLE_LEN];
      for (int i = 0; i < MANFID_TUPLE_LEN; i++) {
        ret = tsd_sdio_read_f0(card, address + TUPLE_HEADER_LEN + i, &bytes[i]);
        if (ret < 0)
          return ret;
      }
      card->cis.manufacturer_id = (uint16_t)(bytes[0] | (bytes[1] << 8));
      card->cis.card_id         = (uint16_t)(bytes[2] | (bytes[3] << 8));
    }

    // Take the fastest clock from function 0's extension tuple
    if (code == TSD_SDIO_CISTPL_FUNCE && link > TPLFE_MAX_TRAN_SPEED_OFFSET) {
      // Read the extension type and the transfer speed
      uint8_t type;
      uint8_t speed;
      ret = tsd_sdio_read_f0(card, address + TUPLE_HEADER_LEN, &type);
      if (ret < 0)
        return ret;
      ret = tsd_sdio_read_f0(card, address + TUPLE_HEADER_LEN + TPLFE_MAX_TRAN_SPEED_OFFSET, &speed);
      if (ret < 0)
        return ret;

      // Decode the speed for function 0
      if (type == TPLFE_TYPE_FUNCTION_0) {
        uint32_t unit_khz       = tran_speed_units_khz[TSD_FIELD_GET(TRAN_SPEED_UNIT, speed)];
        uint32_t time_value     = tran_speed_time_values[TSD_FIELD_GET(TRAN_SPEED_TIME_VALUE, speed)];
        card->cis.max_clock_khz = unit_khz * time_value / 10;
      }
    }

    // Stop after a tuple marked last by its FFh link, once its body has been checked
    if (link == CIS_LINK_END)
      return 0;

    // Step to the next tuple
    address += TUPLE_HEADER_LEN + link;
  }

  return 0;
}

// Ask what the card wants with a first CMD5, then offer our voltage window until it reports ready
static int power_up(struct tsd_sdio *card)
{
  // Ask what the card wants
  uint32_t ocr;
  int ret = tsd_host_send_command(card->host, TSD_CMD_IO_SEND_OP_COND, 0, TSD_RESPONSE_R4, &ocr);
  if (ret < 0)
    return ret;

  for (uint32_t waited_us = 0; waited_us < POWER_UP_TIMEOUT_MS * 1000; waited_us += POWER_UP_DELAY_US) {
    // Offer our window, and stop once the card reports ready
    ret = tsd_host_send_command(card->host, TSD_CMD_IO_SEND_OP_COND, TSD_OCR_VOLTAGE_WINDOW, TSD_RESPONSE_R4, &ocr);
    if (ret < 0)
      return ret;
    if (ocr & TSD_OCR_READY)
      return 0;

    // Wait before asking again
    tsd_delay_us(POWER_UP_DELAY_US);
  }

  return TSD_ERR_TIMEOUT;
}

// Read the address of the common CIS from its three-byte pointer in the CCCR
static int read_common_cis_pointer(struct tsd_sdio *card, uint32_t *address)
{
  // Read the pointer's bytes
  uint8_t pointer[COMMON_CIS_POINTER_LEN];
  for (int i = 0; i < COMMON_CIS_POINTER_LEN; i++) {
    int ret = tsd_sdio_read_f0(card, TSD_SDIO_CCCR_COMMON_CIS_POINTER + i, &pointer[i]);
    if (ret < 0)
      return ret;
  }

  // Join them, lowest byte first
  *address = pointer[0] | ((uint32_t)pointer[1] << 8) | ((uint32_t)pointer[2] << 16);

  return 0;
}

// Enumerate the card slowly on one data line, select it and read its common CIS
static int enumerate_card(struct tsd_sdio *card, struct tsd_host *host)
{
  // Take the host and forget the address and CIS fields of an earlier enumeration
  card->host                = host;
  card->address             = 0;
  card->cis.manufacturer_id = 0;
  card->cis.card_id         = 0;
  card->cis.max_clock_khz   = 0;

  // Start at the enumeration clock on one data line, whatever the card was doing before
  int ret = tsd_host_set_clock(host, ENUMERATION_CLOCK_KHZ);
  if (ret < 0)
    return ret;
  tsd_host_set_bus_width(host, 1);

  // Let the clock run before the first command
  tsd_delay_us(FIRST_COMMAND_DELAY_US);

  // Reset the card, since one left mid transfer answers nothing until then, and ignore the reset's own result
  tsd_sdio_write_f0(card, TSD_SDIO_CCCR_IO_ABORT, TSD_SDIO_IO_ABORT_RES);
  ret = tsd_host_send_command(card->host, TSD_CMD_GO_IDLE_STATE, 0, TSD_RESPONSE_NONE, NULL);
  if (ret < 0)
    return ret;

  // Power the card up with CMD5
  ret = power_up(card);
  if (ret < 0)
    return ret;

  // Let the card pick its address and select it with that
  uint32_t relative;
  ret = tsd_host_send_command(card->host, TSD_CMD_SEND_RELATIVE_ADDR, 0, TSD_RESPONSE_R6, &relative);
  if (ret < 0)
    return ret;
  card->address = relative >> TSD_RCA_SHIFT;
  ret = tsd_host_send_command(card->host, TSD_CMD_SELECT_CARD, card->address << TSD_RCA_SHIFT, TSD_RESPONSE_R1, NULL);
  if (ret < 0)
    return ret;

  // Read the fields the driver uses from the common CIS
  uint32_t common_cis;
  ret = read_common_cis_pointer(card, &common_cis);
  if (ret < 0)
    return ret;
  return read_common_cis(card, common_cis);
}

// Switch the card and then the host between 1-bit and 4-bit mode
static int set_bus_width(struct tsd_sdio *card, uint8_t width)
{
  // Read the bus interface control register and set its width field
  uint8_t interface;
  int ret = tsd_sdio_read_f0(card, TSD_SDIO_CCCR_BUS_INTERFACE_CONTROL, &interface);
  if (ret < 0)
    return ret;
  interface = (interface & ~TSD_SDIO_BUS_WIDTH) |
              TSD_FIELD_PREP(TSD_SDIO_BUS_WIDTH, width == 4 ? TSD_SDIO_BUS_WIDTH_4BIT : TSD_SDIO_BUS_WIDTH_1BIT);

  // Switch the card first, so it is listening on four lines when the host starts driving them
  ret = tsd_sdio_write_f0(card, TSD_SDIO_CCCR_BUS_INTERFACE_CONTROL, interface);
  if (ret < 0)
    return ret;

  // Then switch the host
  tsd_host_set_bus_width(card->host, width);

  return 0;
}

// Switch the card to high speed through the bus speed select register
static bool switch_to_high_speed(struct tsd_sdio *card)
{
  // Check the card has the register, which CCCR revision 1.20 added
  uint8_t revision;
  if (tsd_sdio_read_f0(card, TSD_SDIO_CCCR_SDIO_REVISION, &revision) < 0)
    return false;
  if (TSD_FIELD_GET(TSD_SDIO_REVISION_CCCR, revision) < TSD_SDIO_REVISION_CCCR_1_20)
    return false;

  // Check the card supports high speed
  uint8_t speed;
  if (tsd_sdio_read_f0(card, TSD_SDIO_CCCR_BUS_SPEED_SELECT, &speed) < 0)
    return false;
  if (!(speed & TSD_SDIO_SPEED_SHS))
    return false;

  // Enable high speed
  return tsd_sdio_write_f0(card, TSD_SDIO_CCCR_BUS_SPEED_SELECT, speed | TSD_SDIO_SPEED_EHS) == 0;
}

// Return the I/O of a card that got as far as an address to its initialization state
static void reset_card(struct tsd_sdio *card)
{
  // Only a card still selected and clocked hears the reset, and one that takes it may not answer
  if (card->address)
    tsd_sdio_write_f0(card, TSD_SDIO_CCCR_IO_ABORT, TSD_SDIO_IO_ABORT_RES);
}

int tsd_sdio_identify(struct tsd_host *host, struct tsd_sdio_cis *cis)
{
  struct tsd_sdio card = {0};

  // Enumerate the card to read its ids
  int ret = enumerate_card(&card, host);

  // Leave the card as found for another driver, with its I/O reset and no CMD0
  reset_card(&card);

  *cis = card.cis;
  return ret;
}

int tsd_sdio_init(struct tsd_sdio *card, struct tsd_host *host, uint8_t width, uint32_t clock_khz)
{
  // Refuse a width the bus does not have
  if (width != 1 && width != 4)
    return TSD_ERR_PARAM;

  // Enumerate the card
  int ret = enumerate_card(card, host);
  if (ret < 0)
    return ret;

  // Switch to the width asked for
  ret = set_bus_width(card, width);
  if (ret < 0)
    return ret;

  // Run a clock above default speed only once the card switches to high speed
  if (clock_khz > DEFAULT_SPEED_MAX_KHZ && !switch_to_high_speed(card))
    clock_khz = DEFAULT_SPEED_MAX_KHZ;

  // Hold a card in default speed to a lower limit from its CIS, such as a low speed card's
  if (clock_khz <= DEFAULT_SPEED_MAX_KHZ && card->cis.max_clock_khz && clock_khz > card->cis.max_clock_khz)
    clock_khz = card->cis.max_clock_khz;

  return tsd_host_set_clock(card->host, clock_khz);
}
