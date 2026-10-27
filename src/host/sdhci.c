#include <string.h>

#include <tinysd/bits.h>
#include <tinysd/delay.h>

#include <tinysd/host/sdhci.h>

// Every bit of the normal and error interrupt status registers
#define INT_ALL 0xffffffffu

// Upper 16 bits of a 32-bit access, which hold the second of two 16-bit registers
#define UPPER_16_BITS TSD_BITS(31, 16)

// Largest clock divisor before version 3, a power of two
#define CLOCK_DIVISOR_MAX 256

// Largest clock divisor from version 3, an even number
#define CLOCK_DIVISOR_MAX_V3 2046

// Longest data timeout the controller offers
#define TIMEOUT_MAX 0x0e

// Error bits 9 to 0, every error before version 3 added tuning
#define ERR_ALL TSD_BITS(9, 0)

// Status bits a command or data phase waits on, the ones enabled for reporting
#define INT_WAIT_MASK                                                                                              \
  (INT_COMMAND_COMPLETE | INT_TRANSFER_COMPLETE | INT_BUFFER_WRITE_READY | INT_BUFFER_READ_READY)

// Largest block size the block size register holds
#define BLOCK_SIZE_MAX 0xfffu

// Largest block count the block count register holds
#define BLOCK_COUNT_MAX 0xffffu

// Clock above which the bus runs in high speed mode
#define HIGH_SPEED_THRESHOLD_KHZ 25000

// SDMA buffer boundary, which a buffer must not cross
#define DMA_BOUNDARY (4096u << SDMA_BOUNDARY_512K)

// Delay between status reads while a wait polls, in microseconds
#define WAIT_STEP_US 10

// Longest wait for a command phase, in milliseconds
#define COMMAND_TIMEOUT_MS 200

// Longest wait for a data phase, in milliseconds
#define DATA_TIMEOUT_MS 10000

// Longest wait for a reset bit to clear, in milliseconds
#define RESET_TIMEOUT_MS 100

// Longest wait for the internal clock to settle, in milliseconds
#define CLOCK_TIMEOUT_MS 150

// Settling time after bus power comes up, in microseconds
#define POWER_SETTLE_US 10000

// Register offsets from the controller base, versions 1 to 3
#define REG_SDMA_ADDRESS        0x00
#define REG_BLOCK_SIZE          0x04
#define REG_BLOCK_COUNT         0x06
#define REG_ARGUMENT            0x08
#define REG_TRANSFER_MODE       0x0c
#define REG_COMMAND             0x0e
#define REG_RESPONSE            0x10
#define REG_BUFFER              0x20
#define REG_PRESENT_STATE       0x24
#define REG_CTRL                0x28
#define REG_POWER               0x29
#define REG_BLOCK_GAP           0x2a
#define REG_WAKEUP              0x2b
#define REG_CLOCK               0x2c
#define REG_TIMEOUT             0x2e
#define REG_RESET               0x2f
#define REG_INT_STATUS          0x30
#define REG_ERR_STATUS          0x32
#define REG_INT_ENABLE          0x34
#define REG_ERR_ENABLE          0x36
#define REG_INT_SIGNAL          0x38
#define REG_ERR_SIGNAL          0x3a
#define REG_AUTO_CMD_ERR        0x3c
#define REG_CTRL_2              0x3e
#define REG_CAPS                0x40
#define REG_CAPS_HIGH           0x44
#define REG_MAX_CURRENT         0x48
#define REG_FORCE_AUTO_CMD_ERR  0x50
#define REG_FORCE_ERR           0x52
#define REG_ADMA_ERR            0x54
#define REG_ADMA_ADDRESS        0x58
#define REG_PRESET_INIT         0x60
#define REG_PRESET_DEFAULT      0x62
#define REG_PRESET_HIGH_SPEED   0x64
#define REG_PRESET_SDR12        0x66
#define REG_PRESET_SDR25        0x68
#define REG_PRESET_SDR50        0x6a
#define REG_PRESET_SDR104       0x6c
#define REG_PRESET_DDR50        0x6e
#define REG_SLOT_INT_STATUS     0xfc
#define REG_VERSION             0xfe

// Block size register fields
#define BLOCK_SDMA_BOUNDARY     TSD_BITS(14, 12)
#define BLOCK_TRANSFER_SIZE     TSD_BITS(11, 0)

// Transfer mode register fields
#define XFER_MULTI_BLOCK        TSD_BIT(5)
#define XFER_READ               TSD_BIT(4)
#define XFER_AUTO_CMD           TSD_BITS(3, 2)
#define XFER_BLOCK_COUNT        TSD_BIT(1)
#define XFER_DMA                TSD_BIT(0)

// Command register fields
#define CMD_INDEX               TSD_BITS(13, 8)
#define CMD_TYPE                TSD_BITS(7, 6)
#define CMD_DATA_PRESENT        TSD_BIT(5)
#define CMD_INDEX_CHECK         TSD_BIT(4)
#define CMD_CRC_CHECK           TSD_BIT(3)
#define CMD_RESPONSE_TYPE       TSD_BITS(1, 0)

// Present state register fields
#define STATE_CMD_LEVEL         TSD_BIT(24)
#define STATE_DAT_3_0_LEVEL     TSD_BITS(23, 20)
#define STATE_WP_LEVEL          TSD_BIT(19)
#define STATE_CD_LEVEL          TSD_BIT(18)
#define STATE_CARD_STABLE       TSD_BIT(17)
#define STATE_CARD_INSERTED     TSD_BIT(16)
#define STATE_BUFFER_READ       TSD_BIT(11)
#define STATE_BUFFER_WRITE      TSD_BIT(10)
#define STATE_READ_ACTIVE       TSD_BIT(9)
#define STATE_WRITE_ACTIVE      TSD_BIT(8)
#define STATE_DAT_7_4_LEVEL     TSD_BITS(7, 4)
#define STATE_RETUNING_REQUEST  TSD_BIT(3)
#define STATE_DAT_ACTIVE        TSD_BIT(2)
#define STATE_DAT_INHIBIT       TSD_BIT(1)
#define STATE_CMD_INHIBIT       TSD_BIT(0)

// Host control 1 register fields
#define CTRL_CD_TEST_SELECT     TSD_BIT(7)
#define CTRL_CD_TEST_LEVEL      TSD_BIT(6)
#define CTRL_8BIT               TSD_BIT(5)
#define CTRL_DMA_SELECT         TSD_BITS(4, 3)
#define CTRL_HIGH_SPEED         TSD_BIT(2)
#define CTRL_4BIT               TSD_BIT(1)
#define CTRL_LED                TSD_BIT(0)

// Power control register fields for VDD1
#define POWER_VOLTAGE           TSD_BITS(3, 1)
#define POWER_ON                TSD_BIT(0)

// Clock control register fields, with the divisor fields holding half the divisor
#define CLOCK_DIVISOR           TSD_BITS(15, 8)
#define CLOCK_DIVISOR_HIGH      TSD_BITS(7, 6)
#define CLOCK_GENERATOR_SELECT  TSD_BIT(5)
#define CLOCK_SD_ENABLE         TSD_BIT(2)
#define CLOCK_STABLE            TSD_BIT(1)
#define CLOCK_INTERNAL_ENABLE   TSD_BIT(0)

// Software reset register fields
#define RESET_DAT               TSD_BIT(2)
#define RESET_CMD               TSD_BIT(1)
#define RESET_ALL               TSD_BIT(0)

// Normal interrupt status register fields
#define INT_ERROR               TSD_BIT(15)
#define INT_RETUNING            TSD_BIT(12)
#define INT_C                   TSD_BIT(11)
#define INT_B                   TSD_BIT(10)
#define INT_A                   TSD_BIT(9)
#define INT_CARD                TSD_BIT(8)
#define INT_CARD_REMOVAL        TSD_BIT(7)
#define INT_CARD_INSERTION      TSD_BIT(6)
#define INT_BUFFER_READ_READY   TSD_BIT(5)
#define INT_BUFFER_WRITE_READY  TSD_BIT(4)
#define INT_DMA                 TSD_BIT(3)
#define INT_BLOCK_GAP           TSD_BIT(2)
#define INT_TRANSFER_COMPLETE   TSD_BIT(1)
#define INT_COMMAND_COMPLETE    TSD_BIT(0)

// Error interrupt status register fields
#define ERR_TUNING              TSD_BIT(10)
#define ERR_ADMA                TSD_BIT(9)
#define ERR_AUTO_CMD            TSD_BIT(8)
#define ERR_CURRENT_LIMIT       TSD_BIT(7)
#define ERR_DATA_END_BIT        TSD_BIT(6)
#define ERR_DATA_CRC            TSD_BIT(5)
#define ERR_DATA_TIMEOUT        TSD_BIT(4)
#define ERR_COMMAND_INDEX       TSD_BIT(3)
#define ERR_COMMAND_END_BIT     TSD_BIT(2)
#define ERR_COMMAND_CRC         TSD_BIT(1)
#define ERR_COMMAND_TIMEOUT     TSD_BIT(0)

// Capabilities register fields, bits 31 to 0
#define CAPS_SLOT_TYPE          TSD_BITS(31, 30)
#define CAPS_ASYNC_INTERRUPT    TSD_BIT(29)
#define CAPS_64BIT              TSD_BIT(28)
#define CAPS_1_8V               TSD_BIT(26)
#define CAPS_3_0V               TSD_BIT(25)
#define CAPS_3_3V               TSD_BIT(24)
#define CAPS_SUSPEND_RESUME     TSD_BIT(23)
#define CAPS_SDMA               TSD_BIT(22)
#define CAPS_HIGH_SPEED         TSD_BIT(21)
#define CAPS_ADMA2              TSD_BIT(19)
#define CAPS_8BIT               TSD_BIT(18)
#define CAPS_MAX_BLOCK_LENGTH   TSD_BITS(17, 16)
#define CAPS_BASE_CLOCK_V3      TSD_BITS(15, 8)
#define CAPS_BASE_CLOCK         TSD_BITS(13, 8)
#define CAPS_TIMEOUT_CLOCK_UNIT TSD_BIT(7)
#define CAPS_TIMEOUT_CLOCK      TSD_BITS(5, 0)

// Capabilities register fields, bits 63 to 32, from version 3
#define CAPS_CLOCK_MULTIPLIER   TSD_BITS(23, 16)
#define CAPS_RETUNING_MODES     TSD_BITS(15, 14)
#define CAPS_SDR50_TUNING       TSD_BIT(13)
#define CAPS_RETUNING_TIMER     TSD_BITS(11, 8)
#define CAPS_DRIVER_TYPE_D      TSD_BIT(6)
#define CAPS_DRIVER_TYPE_C      TSD_BIT(5)
#define CAPS_DRIVER_TYPE_A      TSD_BIT(4)
#define CAPS_DDR50              TSD_BIT(2)
#define CAPS_SDR104             TSD_BIT(1)
#define CAPS_SDR50              TSD_BIT(0)

// Values of BLOCK_SDMA_BOUNDARY
enum sdma_boundary {
  SDMA_BOUNDARY_4K   = 0,
  SDMA_BOUNDARY_8K   = 1,
  SDMA_BOUNDARY_16K  = 2,
  SDMA_BOUNDARY_32K  = 3,
  SDMA_BOUNDARY_64K  = 4,
  SDMA_BOUNDARY_128K = 5,
  SDMA_BOUNDARY_256K = 6,
  SDMA_BOUNDARY_512K = 7,
};

// Values of XFER_AUTO_CMD
enum auto_cmd {
  AUTO_CMD_DISABLED = 0,
  AUTO_CMD12        = 1,
  AUTO_CMD23        = 2,
};

// Values of CMD_RESPONSE_TYPE
enum response_type {
  RESPONSE_TYPE_NONE    = 0,
  RESPONSE_TYPE_136     = 1,
  RESPONSE_TYPE_48      = 2,
  RESPONSE_TYPE_48_BUSY = 3,
};

// Values of CMD_TYPE
enum command_type {
  CMD_TYPE_NORMAL  = 0,
  CMD_TYPE_SUSPEND = 1,
  CMD_TYPE_RESUME  = 2,
  CMD_TYPE_ABORT   = 3,
};

// Values of CTRL_DMA_SELECT
enum dma_select {
  DMA_SELECT_SDMA     = 0,
  DMA_SELECT_ADMA2_32 = 2,
  DMA_SELECT_ADMA2_64 = 3,
};

// Values of POWER_VOLTAGE
enum bus_voltage {
  VOLTAGE_1_8V = 5,
  VOLTAGE_3_0V = 6,
  VOLTAGE_3_3V = 7,
};

// Host controller specification versions in the version register
enum host_version {
  HOST_VERSION_1_00 = 0x00,
  HOST_VERSION_2_00 = 0x01,
  HOST_VERSION_3_00 = 0x02,
};

// Read a 32-bit controller register
static uint32_t read32(struct tsd_sdhci *sdhci, uint32_t offset)
{
  return *(volatile uint32_t *)(sdhci->config.base + offset);
}

// Write a 32-bit controller register
static void write32(struct tsd_sdhci *sdhci, uint32_t offset, uint32_t value)
{
  *(volatile uint32_t *)(sdhci->config.base + offset) = value;
}

// Read a 16-bit register with a 32-bit access, since some controllers take no narrower one
static uint16_t read16(struct tsd_sdhci *sdhci, uint32_t offset)
{
  return (uint16_t)(read32(sdhci, offset & ~3u) >> ((offset & 3) * 8));
}

// Write a 16-bit register with a 32-bit access
static void write16(struct tsd_sdhci *sdhci, uint32_t offset, uint16_t value)
{
  uint32_t word_offset = offset & ~3u;
  uint32_t shift       = (offset & 3) * 8;
  uint32_t word        = read32(sdhci, word_offset) & ~((uint32_t)UINT16_MAX << shift);
  write32(sdhci, word_offset, word | (uint32_t)value << shift);
}

// Read an 8-bit register with a 32-bit access
static uint8_t read8(struct tsd_sdhci *sdhci, uint32_t offset)
{
  return (uint8_t)(read32(sdhci, offset & ~3u) >> ((offset & 3) * 8));
}

// Write an 8-bit register with a 32-bit access
static void write8(struct tsd_sdhci *sdhci, uint32_t offset, uint8_t value)
{
  uint32_t word_offset = offset & ~3u;
  uint32_t shift       = (offset & 3) * 8;
  uint32_t word        = read32(sdhci, word_offset) & ~((uint32_t)UINT8_MAX << shift);
  write32(sdhci, word_offset, word | (uint32_t)value << shift);
}

// Wait until any masked bit of a register is set, or all are clear, with word receiving the last value read
static int wait_register(struct tsd_sdhci *sdhci, uint32_t offset, uint32_t mask, bool set, uint32_t timeout_ms,
                         uint32_t *word)
{
  for (uint32_t waited_us = 0;; waited_us += WAIT_STEP_US) {
    // Read the register and stop once its bits are as wanted
    uint32_t value = read32(sdhci, offset);
    if (word)
      *word = value;
    if (set ? (value & mask) != 0 : (value & mask) == 0)
      return 0;

    // Give up once the timeout has passed, and wait a step before the next read
    if (waited_us >= timeout_ms * 1000)
      return TSD_ERR_TIMEOUT;
    tsd_delay_us(WAIT_STEP_US);
  }
}

// Reset the given parts of the controller and wait for the bits to clear
static int reset_controller(struct tsd_sdhci *sdhci, uint32_t bits)
{
  // Set the bits and wait for the controller to clear them
  write8(sdhci, REG_RESET, bits);
  return wait_register(sdhci, REG_RESET & ~3u, bits << ((REG_RESET & 3) * 8), false, RESET_TIMEOUT_MS, NULL);
}

// Reset the command and data circuits and clear every status bit, so the next command starts clean
static void reset_lines(struct tsd_sdhci *sdhci)
{
  // Reset the circuits
  reset_controller(sdhci, RESET_CMD | RESET_DAT);

  // Clear the status
  write32(sdhci, REG_INT_STATUS, INT_ALL);
}

// Turn the error status bits of an interrupt status read into a tsd_error
static int classify_error(uint32_t status)
{
  // Report a timeout first, then a command fault, and anything else as a data fault
  uint32_t error_status = TSD_FIELD_GET(UPPER_16_BITS, status) & ERR_ALL;
  if (error_status & (ERR_COMMAND_TIMEOUT | ERR_DATA_TIMEOUT))
    return TSD_ERR_TIMEOUT;
  if (error_status & (ERR_COMMAND_CRC | ERR_COMMAND_END_BIT | ERR_COMMAND_INDEX))
    return TSD_ERR_COMMAND;
  return TSD_ERR_DATA;
}

// Wait for any of the wanted status bits or an error, clearing the wanted bits found so each phase sees only its own
static int wait_status(struct tsd_sdhci *sdhci, uint32_t wanted, uint32_t timeout_ms)
{
  uint32_t status;

  // Wait for a wanted bit or an error
  int ret = wait_register(sdhci, REG_INT_STATUS, wanted | INT_ERROR, true, timeout_ms, &status);
  if (ret < 0)
    return ret;

  // Classify an error, leaving the status for the caller's reset
  if (status & INT_ERROR)
    return classify_error(status);

  // Clear the wanted bits found
  write32(sdhci, REG_INT_STATUS, status & wanted);

  return 0;
}

// Wait out an R1b's busy, ended by the DAT inhibit clearing
static int wait_busy(struct tsd_sdhci *sdhci)
{
  // Wait for the busy to end
  int ret = wait_register(sdhci, REG_PRESENT_STATE, STATE_DAT_INHIBIT, false, DATA_TIMEOUT_MS, NULL);
  if (ret < 0)
    return ret;

  // Report an error raised during the busy, such as a busy timeout
  uint32_t status = read32(sdhci, REG_INT_STATUS);
  if (status & INT_ERROR)
    return classify_error(status);

  // Clear the busy's Transfer Complete, so it cannot satisfy the next command's waits
  write32(sdhci, REG_INT_STATUS, status & INT_TRANSFER_COMPLETE);

  return 0;
}

// Turn a data port word, which is little endian, into the CPU's order or back
static uint32_t swap_fifo_word(uint32_t word)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return __builtin_bswap32(word);
#else
  return word;
#endif
}

// Move one block through the data port a word at a time
static void move_block_fifo(struct tsd_sdhci *sdhci, uint8_t *data, uint32_t block_size, bool read)
{
  uint32_t *words = (uint32_t *)data;
  uint32_t whole  = block_size / 4;
  uint32_t tail   = block_size % 4;

  // Move each whole word in the CPU's order
  if (read) {
    for (uint32_t i = 0; i < whole; i++)
      words[i] = swap_fifo_word(read32(sdhci, REG_BUFFER));
  } else {
    for (uint32_t i = 0; i < whole; i++)
      write32(sdhci, REG_BUFFER, swap_fifo_word(words[i]));
  }

  // Move a last partial word through a word of its own, so nothing past the block is touched
  if (tail) {
    uint32_t word = 0;
    if (read) {
      word = swap_fifo_word(read32(sdhci, REG_BUFFER));
      memcpy(&words[whole], &word, tail);
    } else {
      memcpy(&word, &words[whole], tail);
      write32(sdhci, REG_BUFFER, swap_fifo_word(word));
    }
  }
}

// Run the data phase through the FIFO, waiting for the controller to be ready for each block
static int transfer_fifo(struct tsd_sdhci *sdhci, struct tsd_data *data)
{
  uint32_t ready  = data->write ? INT_BUFFER_WRITE_READY : INT_BUFFER_READ_READY;
  uint8_t *buffer = data->buffer;

  // Move each block once the buffer is ready for it
  for (uint32_t i = 0; i < data->blocks; i++) {
    int ret = wait_status(sdhci, ready, DATA_TIMEOUT_MS);
    if (ret < 0)
      return ret;
    move_block_fifo(sdhci, buffer, data->block_size, !data->write);
    buffer += data->block_size;
  }

  // Wait for the data phase to finish
  return wait_status(sdhci, INT_TRANSFER_COMPLETE, DATA_TIMEOUT_MS);
}

// Run the data phase by SDMA, which the controller started with the command
static int transfer_dma(struct tsd_sdhci *sdhci, struct tsd_data *data, uint32_t len)
{
  // Wait for the data phase to finish, then hand the buffer back to the CPU
  int ret = wait_status(sdhci, INT_TRANSFER_COMPLETE, DATA_TIMEOUT_MS);
  sdhci->config.dma_unmap(sdhci, data->buffer, len, !data->write);

  return ret;
}

// Set or clear the card interrupt in both enables, which clears a latched one when turned off and on
static void write_card_interrupt(struct tsd_sdhci *sdhci, bool on)
{
  // Set or clear it in the status enable, which gates the latch, and the signal enable, which gates the line
  uint32_t status_enable = read16(sdhci, REG_INT_ENABLE) & ~INT_CARD;
  uint32_t signal_enable = read16(sdhci, REG_INT_SIGNAL) & ~INT_CARD;
  if (on) {
    status_enable |= INT_CARD;
    signal_enable |= INT_CARD;
  }
  write16(sdhci, REG_INT_ENABLE, status_enable);
  write16(sdhci, REG_INT_SIGNAL, signal_enable);
}

// Find the command register's response type and check bits for a response
static uint32_t encode_response(enum tsd_response response_type)
{
  switch (response_type) {
    case TSD_RESPONSE_NONE:
      // Expect nothing back
      return 0;

    case TSD_RESPONSE_R2:
      // Expect 136 bits, which carry a CRC but no index
      return TSD_FIELD_PREP(CMD_RESPONSE_TYPE, RESPONSE_TYPE_136) | CMD_CRC_CHECK;

    case TSD_RESPONSE_R1B:
      // Expect 48 bits and the busy signal, and check them
      return TSD_FIELD_PREP(CMD_RESPONSE_TYPE, RESPONSE_TYPE_48_BUSY) | CMD_CRC_CHECK | CMD_INDEX_CHECK;

    case TSD_RESPONSE_R3:
    case TSD_RESPONSE_R4:
      // Expect 48 bits that carry no CRC or index
      return TSD_FIELD_PREP(CMD_RESPONSE_TYPE, RESPONSE_TYPE_48);

    default:
      // Expect 48 bits and check them, for R1, R5, R6 and R7
      return TSD_FIELD_PREP(CMD_RESPONSE_TYPE, RESPONSE_TYPE_48) | CMD_CRC_CHECK | CMD_INDEX_CHECK;
  }
}

// Read a command's response, moving an R2's 120 bits up a byte into the order the SD specification gives
static void read_response(struct tsd_sdhci *sdhci, struct tsd_command *command)
{
  // Take a 48-bit response's payload from the first word
  if (command->response_type != TSD_RESPONSE_R2) {
    command->response[0] = read32(sdhci, REG_RESPONSE);
    return;
  }

  // Take an R2 from the top word down
  for (int i = 0; i < 4; i++)
    command->response[i] = read32(sdhci, REG_RESPONSE + (3 - i) * 4);

  // Shift each word up a byte, filling it from the word below
  for (int i = 0; i < 4; i++)
    command->response[i] = (command->response[i] << 8) | (i < 3 ? command->response[i + 1] >> 24 : 0);
}

// Map a data phase's buffer for SDMA, or return 0 to move it through the FIFO
static uint32_t map_dma_buffer(struct tsd_sdhci *sdhci, struct tsd_data *data, uint32_t len)
{
  // Use SDMA for a buffer the config maps
  if (!data || !sdhci->config.dma_map)
    return 0;
  uint32_t dma_address = sdhci->config.dma_map(sdhci, data->buffer, len, !data->write);

  // Fall back to the FIFO for a buffer that crosses a boundary, where the engine would pause for the CPU
  if (dma_address && (dma_address ^ (dma_address + len - 1)) & ~(DMA_BOUNDARY - 1)) {
    sdhci->config.dma_unmap(sdhci, data->buffer, len, !data->write);
    return 0;
  }

  return dma_address;
}

// Write a command and the shape of its data phase to the controller, which starts it
static void start_command(struct tsd_sdhci *sdhci, struct tsd_command *command, struct tsd_data *data,
                          uint32_t dma_address)
{
  // Build the transfer mode for the lower 16 bits
  uint32_t blocks     = data ? data->blocks : 0;
  uint16_t block_size = data ? data->block_size : 0;
  uint32_t mode       = 0;
  if (data) {
    if (!data->write)
      mode |= XFER_READ;
    if (dma_address)
      mode |= XFER_DMA;

    // Set the multi block bits only beyond one block, since a single block is its own transfer kind
    if (blocks > 1)
      mode |= XFER_BLOCK_COUNT | XFER_MULTI_BLOCK;
  }

  // Build the command for the upper 16 bits
  uint32_t command_word = TSD_FIELD_PREP(CMD_INDEX, command->index) | encode_response(command->response_type);
  if (data)
    command_word |= CMD_DATA_PRESENT;

  // Clear stale status from an earlier command, so it cannot satisfy this one's waits
  write32(sdhci, REG_INT_STATUS, read32(sdhci, REG_INT_STATUS) & ~INT_CARD);

  // Hand an SDMA buffer to the controller
  if (dma_address)
    write32(sdhci, REG_SDMA_ADDRESS, dma_address);

  // Set the block size and count and the argument
  uint32_t block_word = TSD_FIELD_PREP(BLOCK_TRANSFER_SIZE, block_size);
  block_word |= TSD_FIELD_PREP(BLOCK_SDMA_BOUNDARY, SDMA_BOUNDARY_512K);
  block_word |= TSD_FIELD_PREP(UPPER_16_BITS, blocks);
  write32(sdhci, REG_BLOCK_SIZE, block_word);
  write32(sdhci, REG_ARGUMENT, command->argument);

  // Start the command by writing the command register
  write32(sdhci, REG_TRANSFER_MODE, TSD_FIELD_PREP(UPPER_16_BITS, command_word) | mode);
}

// Issue a command and complete its data phase
static int run_command(struct tsd_sdhci *sdhci, struct tsd_command *command, struct tsd_data *data)
{
  // Refuse a data phase that is empty or too large, and count nothing moved yet
  uint32_t len = 0;
  if (data) {
    if (data->block_size == 0 || data->block_size > BLOCK_SIZE_MAX)
      return TSD_ERR_PARAM;
    if (data->blocks == 0 || data->blocks > BLOCK_COUNT_MAX)
      return TSD_ERR_PARAM;
    len        = data->blocks * data->block_size;
    data->done = 0;
  }

  // Wait for the previous command to leave the command line
  int ret = wait_register(sdhci, REG_PRESENT_STATE, STATE_CMD_INHIBIT, false, COMMAND_TIMEOUT_MS, NULL);

  // Wait for the data lines too, which stay busy while the card programs a write
  if (ret == 0)
    ret = wait_register(sdhci, REG_PRESENT_STATE, STATE_DAT_INHIBIT, false, DATA_TIMEOUT_MS, NULL);
  if (ret < 0) {
    reset_lines(sdhci);
    return TSD_ERR_BUSY;
  }

  // Start the command, by SDMA when the buffer maps
  uint32_t dma_address = map_dma_buffer(sdhci, data, len);
  start_command(sdhci, command, data, dma_address);

  // Wait for the command phase, handing back a mapped buffer and resetting the circuits on a failure
  ret = wait_status(sdhci, INT_COMMAND_COMPLETE, COMMAND_TIMEOUT_MS);
  if (ret < 0) {
    if (dma_address)
      sdhci->config.dma_unmap(sdhci, data->buffer, len, !data->write);
    reset_lines(sdhci);
    return ret;
  }

  // Read the response
  if (command->response_type != TSD_RESPONSE_NONE)
    read_response(sdhci, command);

  // Finish here without a data phase, once the card's busy after an R1b ends
  if (!data) {
    if (command->response_type == TSD_RESPONSE_R1B)
      ret = wait_busy(sdhci);
    if (ret < 0)
      reset_lines(sdhci);
    return ret;
  }

  // Run the data phase
  if (dma_address) {
    ret = transfer_dma(sdhci, data, len);
  } else {
    ret = transfer_fifo(sdhci, data);
  }

  // Count every byte moved once the data phase finishes
  if (ret == 0) {
    data->done = len;
    return 0;
  }

  // Count the bytes the card sent before a failure, a read block that failed its CRC included
  uint32_t remaining = read16(sdhci, REG_BLOCK_COUNT);
  data->done         = len - remaining * data->block_size;

  // Reset the circuits for the next command
  reset_lines(sdhci);

  return ret;
}

// Send a command and run its data phase, with the card interrupt masked while it runs
static int send_request(struct tsd_host *host, struct tsd_command *command, struct tsd_data *data)
{
  struct tsd_sdhci *sdhci = TSD_SDHCI(host);

  // Mask the card interrupt while the command runs, since the card signals it on a data line between transfers
  if (sdhci->card_interrupt)
    write_card_interrupt(sdhci, false);

  // Run the command
  int ret = run_command(sdhci, command, data);

  // Unmask it again, which also drops one latched while it was masked
  if (sdhci->card_interrupt)
    write_card_interrupt(sdhci, true);

  return ret;
}

// Run the SD clock at the highest rate at or below a limit
static int set_clock(struct tsd_host *host, uint32_t clock_khz)
{
  struct tsd_sdhci *sdhci = TSD_SDHCI(host);

  // Stop the clock before any change
  write16(sdhci, REG_CLOCK, 0);
  if (clock_khz == 0)
    return 0;

  // Stay at default speed on a controller without high speed
  bool high_speed = read32(sdhci, REG_CAPS) & CAPS_HIGH_SPEED;
  if (!high_speed && clock_khz > HIGH_SPEED_THRESHOLD_KHZ)
    clock_khz = HIGH_SPEED_THRESHOLD_KHZ;

  // Divide the base clock down to the limit, by an even divisor from version 3 and a power of two before it
  bool version3        = sdhci->version >= HOST_VERSION_3_00;
  uint32_t max_divisor = version3 ? CLOCK_DIVISOR_MAX_V3 : CLOCK_DIVISOR_MAX;
  uint32_t divisor     = 1;
  while (divisor <= max_divisor && sdhci->base_clock_khz / divisor > clock_khz)
    divisor = version3 ? (divisor + 2) & ~1u : divisor * 2;
  if (divisor > max_divisor)
    return TSD_ERR_PARAM;

  // Start the internal clock with half the divisor in the fields, and let it settle before the bus clock starts
  uint32_t half_divisor = divisor / 2;
  uint32_t control      = TSD_FIELD_PREP(CLOCK_DIVISOR, half_divisor);
  control |= TSD_FIELD_PREP(CLOCK_DIVISOR_HIGH, half_divisor >> 8);
  write16(sdhci, REG_CLOCK, control | CLOCK_INTERNAL_ENABLE);
  int ret = wait_register(sdhci, REG_CLOCK, CLOCK_STABLE, true, CLOCK_TIMEOUT_MS, NULL);
  if (ret < 0)
    return ret;
  write16(sdhci, REG_CLOCK, control | CLOCK_INTERNAL_ENABLE | CLOCK_SD_ENABLE);

  // Sample on the other edge above default speed
  uint32_t host_control = read8(sdhci, REG_CTRL) & ~CTRL_HIGH_SPEED;
  if (sdhci->base_clock_khz / divisor > HIGH_SPEED_THRESHOLD_KHZ)
    host_control |= CTRL_HIGH_SPEED;
  write8(sdhci, REG_CTRL, host_control);

  return 0;
}

// Select the bus width
static void set_bus_width(struct tsd_host *host, uint8_t width)
{
  struct tsd_sdhci *sdhci = TSD_SDHCI(host);

  // Set or clear the 4-bit bus bit
  uint32_t host_control = read8(sdhci, REG_CTRL) & ~CTRL_4BIT;
  if (width == 4)
    host_control |= CTRL_4BIT;
  write8(sdhci, REG_CTRL, host_control);
}

// Let the card interrupt reach the CPU or stop it
static void set_card_interrupt(struct tsd_host *host, bool on)
{
  struct tsd_sdhci *sdhci = TSD_SDHCI(host);

  // Remember the choice for the commands that mask it, and apply it
  sdhci->card_interrupt = on;
  write_card_interrupt(sdhci, on);
}

// Operations card drivers reach the controller through
static const struct tsd_host_api sdhci_api = {
  .request            = send_request,
  .set_clock          = set_clock,
  .set_bus_width      = set_bus_width,
  .set_card_interrupt = set_card_interrupt,
};

int tsd_sdhci_init(struct tsd_sdhci *sdhci, struct tsd_sdhci_config config)
{
  // Keep how to reach the controller and fill in its host interface, with the card interrupt off
  sdhci->host.api       = &sdhci_api;
  sdhci->config         = config;
  sdhci->card_interrupt = false;

  // Silence every interrupt line before touching anything else
  write32(sdhci, REG_INT_SIGNAL, 0);

  // Reset the whole controller
  int ret = reset_controller(sdhci, RESET_ALL);
  if (ret < 0)
    return ret;

  // Note the host controller specification version, which sets the base clock and divisor fields
  sdhci->version = read8(sdhci, REG_VERSION);

  // Read the base clock, which the capabilities give in MHz
  uint32_t capabilities = read32(sdhci, REG_CAPS);
  uint32_t clock_field  = sdhci->version >= HOST_VERSION_3_00 ? CAPS_BASE_CLOCK_V3 : CAPS_BASE_CLOCK;
  sdhci->base_clock_khz = TSD_FIELD_GET(clock_field, capabilities) * 1000;
  if (sdhci->base_clock_khz == 0)
    return TSD_ERR_PARAM;

  // Set the longest data timeout
  write8(sdhci, REG_TIMEOUT, TIMEOUT_MAX);

  // Report the phases a command waits on and every error, signal none, since the driver polls
  write32(sdhci, REG_INT_ENABLE, TSD_FIELD_PREP(UPPER_16_BITS, ERR_ALL) | INT_WAIT_MASK);

  // Power the bus at the highest voltage offered, refusing one without 3.3 V or 3.0 V
  uint32_t voltage;
  if (capabilities & CAPS_3_3V) {
    voltage = VOLTAGE_3_3V;
  } else if (capabilities & CAPS_3_0V) {
    voltage = VOLTAGE_3_0V;
  } else {
    return TSD_ERR_POWER;
  }
  write8(sdhci, REG_POWER, 0);
  write8(sdhci, REG_POWER, TSD_FIELD_PREP(POWER_VOLTAGE, voltage) | POWER_ON);
  tsd_delay_us(POWER_SETTLE_US);

  // Fail when the controller dropped the power bit, which it does when it cannot supply the bus
  if (!(read8(sdhci, REG_POWER) & POWER_ON))
    return TSD_ERR_POWER;

  return 0;
}
