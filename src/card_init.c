#include <stddef.h>
#include <stdint.h>

#include <tinysd/bits.h>
#include <tinysd/commands.h>
#include <tinysd/delay.h>

#include <tinysd/card.h>

// Longest wait for the card to power up, in milliseconds
#define POWER_UP_TIMEOUT_MS 10000

// Delay between ACMD41 tries while the card powers up, in microseconds
#define POWER_UP_DELAY_US 10000

// Delay before the first command once the clock runs, covering the 1 ms and 74 clocks the SD specification asks for
#define FIRST_COMMAND_DELAY_US 1000

// Clock the card is identified at, in kHz
#define IDENTIFICATION_CLOCK_KHZ 400

// Fastest clock in default speed, in kHz
#define DEFAULT_SPEED_MAX_KHZ 25000

// Send an application command, announced by CMD55 to the card's address
static int send_app_command(struct tsd_card *card, uint8_t index, uint32_t argument, enum tsd_response response_type,
                            uint32_t *response)
{
  // Announce it, then send it
  int ret = tsd_host_send_command(card->host, TSD_CMD_APP_CMD, card->address << TSD_RCA_SHIFT, TSD_RESPONSE_R1, NULL);
  if (ret < 0)
    return ret;

  return tsd_host_send_command(card->host, index, argument, response_type, response);
}

// Check with CMD8 whether the card is version 2, refusing one that does not echo our voltage and pattern
static int check_version(struct tsd_card *card, bool *version2)
{
  // Offer our voltage with a check pattern
  uint32_t argument = TSD_FIELD_PREP(TSD_IF_COND_VOLTAGE, TSD_IF_COND_VOLTAGE_2_7_3_6V);
  argument |= TSD_FIELD_PREP(TSD_IF_COND_PATTERN, TSD_IF_COND_CHECK_PATTERN);
  uint32_t reply = 0;
  int ret        = tsd_host_send_command(card->host, TSD_CMD_SEND_IF_COND, argument, TSD_RESPONSE_R7, &reply);

  // Take no answer as a version 1 card
  *version2 = false;
  if (ret == TSD_ERR_TIMEOUT)
    return 0;
  if (ret < 0)
    return ret;

  // Take an answer as a version 2 card, which must echo the voltage and pattern
  *version2 = true;
  if ((reply & (TSD_IF_COND_VOLTAGE | TSD_IF_COND_PATTERN)) != argument)
    return TSD_ERR_COMMAND;

  return 0;
}

// Repeat ACMD41 until the card reports ready, offering high capacity to a version 2 card
static int power_up(struct tsd_card *card, bool version2)
{
  uint32_t argument = TSD_OCR_VOLTAGE_WINDOW | (version2 ? TSD_OCR_CCS : 0);
  for (uint32_t waited_us = 0; waited_us < POWER_UP_TIMEOUT_MS * 1000; waited_us += POWER_UP_DELAY_US) {
    // Ask the card to power up
    uint32_t ocr = 0;
    int ret      = send_app_command(card, TSD_ACMD_SD_SEND_OP_COND, argument, TSD_RESPONSE_R3, &ocr);
    if (ret < 0)
      return ret;

    // Note whether the card took high capacity once it reports ready
    if (ocr & TSD_OCR_READY) {
      card->high_capacity = version2 && (ocr & TSD_OCR_CCS);
      return 0;
    }

    // Wait before asking again
    tsd_delay_us(POWER_UP_DELAY_US);
  }

  return TSD_ERR_TIMEOUT;
}

// Read the card's CSD with CMD9 and count the blocks it holds
static int read_capacity(struct tsd_card *card)
{
  // Ask the card for its CSD
  struct tsd_command command = {
    .index         = TSD_CMD_SEND_CSD,
    .argument      = card->address << TSD_RCA_SHIFT,
    .response_type = TSD_RESPONSE_R2,
  };
  int ret = tsd_host_request(card->host, &command, NULL);
  if (ret < 0)
    return ret;

  // Read the CSD structure version
  const uint32_t *csd = command.response;
  uint32_t version    = TSD_FIELD_GET(TSD_CSD_STRUCTURE, csd[0]);

  // Count a version 2 CSD's size in units of 1024 blocks
  if (version == TSD_CSD_VERSION_2) {
    uint32_t size = TSD_FIELD_GET(TSD_CSD_V2_C_SIZE_HIGH, csd[1]) << 16 | TSD_FIELD_GET(TSD_CSD_V2_C_SIZE_LOW, csd[2]);
    card->blocks  = (size + 1) * 1024;
    return 0;
  }

  // Refuse any other structure, such as an SDUC card's version 3, since its size needs 64-bit block numbers
  if (version != TSD_CSD_VERSION_1)
    return TSD_ERR_COMMAND;

  // Scale a version 1 CSD's size by its multiplier and block length
  uint32_t size = TSD_FIELD_GET(TSD_CSD_V1_C_SIZE_HIGH, csd[1]) << 2 | TSD_FIELD_GET(TSD_CSD_V1_C_SIZE_LOW, csd[2]);
  uint32_t multiplier   = TSD_FIELD_GET(TSD_CSD_V1_C_SIZE_MULT, csd[2]);
  uint32_t block_length = TSD_FIELD_GET(TSD_CSD_V1_READ_BL_LEN, csd[1]);
  card->blocks          = (size + 1) << (multiplier + block_length - 7);
  return 0;
}

// Switch the card to high speed with CMD6
static bool switch_to_high_speed(struct tsd_card *card)
{
  // Ask for high speed and read back the switch status
  uint32_t status[TSD_SWITCH_STATUS_LEN / 4];
  struct tsd_command command = {
    .index         = TSD_CMD_SWITCH_FUNC,
    .argument      = TSD_SWITCH_HIGH_SPEED,
    .response_type = TSD_RESPONSE_R1,
  };
  struct tsd_data data = {
    .buffer     = status,
    .block_size = TSD_SWITCH_STATUS_LEN,
    .blocks     = 1,
  };
  if (tsd_host_request(card->host, &command, &data) < 0)
    return false;

  // Check the card switched, since a card without high speed stays at default speed
  return TSD_FIELD_GET(TSD_SWITCH_STATUS_FUNCTION, ((uint8_t *)status)[TSD_SWITCH_STATUS_GROUP1]) ==
         TSD_SWITCH_FUNCTION_HIGH_SPEED;
}

int tsd_card_init(struct tsd_card *card, struct tsd_host *host, uint8_t width, uint32_t clock_khz)
{
  // Refuse a width the bus does not have
  if (width != 1 && width != 4)
    return TSD_ERR_PARAM;

  // Take the host and start from no address and no read, as a standard capacity card
  card->host          = host;
  card->address       = 0;
  card->high_capacity = false;
  card->blocks        = 0;
  card->reading       = false;
  card->next_block    = UINT32_MAX;

  // Start at the identification clock on one data line
  int ret = tsd_host_set_clock(host, IDENTIFICATION_CLOCK_KHZ);
  if (ret < 0)
    return ret;
  tsd_host_set_bus_width(host, 1);

  // Let the clock run before the first command
  tsd_delay_us(FIRST_COMMAND_DELAY_US);

  // Reset the card to idle
  ret = tsd_host_send_command(card->host, TSD_CMD_GO_IDLE_STATE, 0, TSD_RESPONSE_NONE, NULL);
  if (ret < 0)
    return ret;

  // Check with CMD8 whether the card is version 2
  bool version2;
  ret = check_version(card, &version2);
  if (ret < 0)
    return ret;

  // Power the card up with ACMD41
  ret = power_up(card, version2);
  if (ret < 0)
    return ret;

  // Move the card on through identification and take its relative address
  ret = tsd_host_send_command(card->host, TSD_CMD_ALL_SEND_CID, 0, TSD_RESPONSE_R2, NULL);
  if (ret < 0)
    return ret;
  uint32_t relative;
  ret = tsd_host_send_command(card->host, TSD_CMD_SEND_RELATIVE_ADDR, 0, TSD_RESPONSE_R6, &relative);
  if (ret < 0)
    return ret;
  card->address = relative >> TSD_RCA_SHIFT;

  // Count the card's blocks, which CMD9 reads before the card is selected
  ret = read_capacity(card);
  if (ret < 0)
    return ret;

  // Select the card by its address, taking an R1 since it has nothing to be busy with, like Linux and Zephyr
  ret = tsd_host_send_command(card->host, TSD_CMD_SELECT_CARD, card->address << TSD_RCA_SHIFT, TSD_RESPONSE_R1, NULL);
  if (ret < 0)
    return ret;

  // Set 512-byte blocks on a standard capacity card, since its block length may start larger
  if (!card->high_capacity) {
    ret = tsd_host_send_command(card->host, TSD_CMD_SET_BLOCKLEN, TSD_CARD_BLOCK_SIZE, TSD_RESPONSE_R1, NULL);
    if (ret < 0)
      return ret;
  }

  // Switch the card and then the host to 4 bits when asked
  if (width == 4) {
    ret = send_app_command(card, TSD_ACMD_SET_BUS_WIDTH, TSD_ACMD6_BUS_WIDTH_4BIT, TSD_RESPONSE_R1, NULL);
    if (ret < 0)
      return ret;
    tsd_host_set_bus_width(host, 4);
  }

  // Run a clock above default speed only once the card switches to high speed
  if (clock_khz > DEFAULT_SPEED_MAX_KHZ && !switch_to_high_speed(card))
    clock_khz = DEFAULT_SPEED_MAX_KHZ;

  return tsd_host_set_clock(host, clock_khz);
}
