#include <stdint.h>

#include <tinysd/bits.h>
#include <tinysd/commands.h>
#include <tinysd/crc.h>
#include <tinysd/delay.h>
#include <tinysd/frame.h>

#include <tinysd/host/spi.h>

// Clock the card starts at, in kHz
#define IDENTIFICATION_CLOCK_KHZ 400

// Bytes clocked with the card released at power up, 80 clocks covering the 74 the SD specification asks for
#define POWER_UP_BYTES 10

// Longest wait for a response, in milliseconds
#define COMMAND_TIMEOUT_MS 200

// Longest wait for the busy after an R1b, in milliseconds
#define BUSY_TIMEOUT_MS 3000

// Longest wait for a read block to start or a written block to program, in milliseconds
#define DATA_TIMEOUT_MS 10000

// Delay between reads while a wait polls, in microseconds
#define WAIT_STEP_US 10

// Read bytes while they match value under mask, leaving the first that does not in byte
static int wait_byte(struct tsd_spi *spi, uint8_t mask, uint8_t value, uint32_t timeout_ms, uint8_t *byte)
{
  for (uint32_t waited_us = 0;; waited_us += WAIT_STEP_US) {
    // Read a byte and stop once it no longer matches
    int ret = spi->config.read(spi, byte, 1);
    if (ret < 0)
      return ret;
    if ((*byte & mask) != value)
      return 0;

    // Give up once the timeout has passed, and wait a step before the next read
    if (waited_us >= timeout_ms * 1000)
      return TSD_ERR_TIMEOUT;
    tsd_delay_us(WAIT_STEP_US);
  }
}

// Turn the error bits of an R1 into a tsd_error
static int check_r1(uint8_t r1)
{
  // Report a bad address or argument first, then a command the card refused or received damaged
  if (r1 & (TSD_SPI_R1_PARAMETER_ERROR | TSD_SPI_R1_ADDRESS_ERROR))
    return TSD_ERR_PARAM;
  if (r1 & (TSD_SPI_R1_ILLEGAL_COMMAND | TSD_SPI_R1_COM_CRC_ERROR))
    return TSD_ERR_COMMAND;

  // Report an erase sequence the card broke off
  if (r1 & (TSD_SPI_R1_ERASE_SEQ_ERROR | TSD_SPI_R1_ERASE_RESET))
    return TSD_ERR_CARD;

  return 0;
}

// Send a command and read its response
static int send_command(struct tsd_spi *spi, struct tsd_command *command)
{
  // Count the bytes after the R1, refusing the SDIO responses
  uint32_t extra_len;
  switch (command->response_type) {
    case TSD_RESPONSE_R2:
      // Take the second status byte
      extra_len = 1;
      break;

    case TSD_RESPONSE_R3:
    case TSD_RESPONSE_R7:
      // Take the four bytes of the OCR or the interface condition
      extra_len = 4;
      break;

    case TSD_RESPONSE_R4:
    case TSD_RESPONSE_R5:
    case TSD_RESPONSE_R6:
      // Refuse a response this host does not take
      return TSD_ERR_PARAM;

    default:
      // Take the R1 alone, which every command returns
      extra_len = 0;
      break;
  }

  // Send a byte of ones so the card is ready, then the frame
  uint8_t frame[TSD_FRAME_COMMAND_LEN + 1] = {0xff};
  tsd_frame_build_command(&frame[1], command);
  int ret = spi->config.write(spi, frame, sizeof(frame));
  if (ret < 0)
    return ret;

  // Skip the byte after the frame, which may hold data after CMD12, then wait for the R1 and its 0 start bit
  uint8_t r1;
  ret = spi->config.read(spi, &r1, 1);
  if (ret < 0)
    return ret;
  ret = wait_byte(spi, 0x80, 0x80, COMMAND_TIMEOUT_MS, &r1);
  if (ret < 0)
    return ret;
  command->response[0] = r1;

  // Report the error bits of the R1, leaving it in the response, since a card that refuses a command sends no more
  ret = check_r1(r1);
  if (ret < 0)
    return ret;

  // Put an R2's second status byte above the R1, or an R3 or R7's four bytes in its place
  uint8_t extra[4];
  if (extra_len) {
    ret = spi->config.read(spi, extra, extra_len);
    if (ret < 0)
      return ret;
  }
  if (extra_len == 1)
    command->response[0] |= (uint32_t)extra[0] << 8;
  if (extra_len == 4)
    command->response[0] = (uint32_t)extra[0] << 24 | (uint32_t)extra[1] << 16 | (uint32_t)extra[2] << 8 | extra[3];

  // Wait while the card holds its output low after an R1b
  if (command->response_type == TSD_RESPONSE_R1B) {
    uint8_t byte;
    return wait_byte(spi, 0xff, 0x00, BUSY_TIMEOUT_MS, &byte);
  }

  return 0;
}

// Read one block and check its CRC16, counting it as moved once it arrives
static int read_block(struct tsd_spi *spi, struct tsd_data *data, uint8_t *buffer)
{
  // Wait for the card to start the block, refusing an error token
  uint8_t token;
  int ret = wait_byte(spi, 0xff, 0xff, DATA_TIMEOUT_MS, &token);
  if (ret < 0)
    return ret;
  if (token != TSD_SPI_TOKEN_START_BLOCK)
    return TSD_ERR_DATA;

  // Read the block and its CRC16
  uint8_t crc_bytes[2];
  ret = spi->config.read(spi, buffer, data->block_size);
  if (ret < 0)
    return ret;
  ret = spi->config.read(spi, crc_bytes, sizeof(crc_bytes));
  if (ret < 0)
    return ret;
  data->done += data->block_size;

  // Check the CRC16
  uint16_t crc = 0;
  for (uint32_t i = 0; i < data->block_size; i++)
    crc = tsd_crc16_update(crc, buffer[i]);

  return crc == (crc_bytes[0] << 8 | crc_bytes[1]) ? 0 : TSD_ERR_DATA;
}

// Write one block after a token, counting it as moved once the card takes it, then wait while the card programs it
static int write_block(struct tsd_spi *spi, struct tsd_data *data, const uint8_t *buffer, uint8_t token)
{
  // Compute the CRC16 that follows the block
  uint16_t crc = 0;
  for (uint32_t i = 0; i < data->block_size; i++)
    crc = tsd_crc16_update(crc, buffer[i]);
  uint8_t crc_bytes[2] = {(uint8_t)(crc >> 8), (uint8_t)crc};

  // Send a byte of ones, the token, the block and its CRC16
  uint8_t start[2] = {0xff, token};
  int ret          = spi->config.write(spi, start, sizeof(start));
  if (ret < 0)
    return ret;
  ret = spi->config.write(spi, buffer, data->block_size);
  if (ret < 0)
    return ret;
  ret = spi->config.write(spi, crc_bytes, sizeof(crc_bytes));
  if (ret < 0)
    return ret;

  // Check the data response, reporting a write error as the card's and anything else as a failed transfer
  uint8_t response;
  ret = spi->config.read(spi, &response, 1);
  if (ret < 0)
    return ret;
  uint32_t status = TSD_FIELD_GET(TSD_SPI_DATA_RESPONSE_STATUS, response);
  if (status == TSD_SPI_DATA_WRITE_ERROR)
    return TSD_ERR_CARD;
  if (status != TSD_SPI_DATA_ACCEPTED)
    return TSD_ERR_DATA;
  data->done += data->block_size;

  // Wait while the card holds its output low
  return wait_byte(spi, 0xff, 0x00, DATA_TIMEOUT_MS, &response);
}

// Move the blocks of a data phase, ending a multiple block write with the stop token
static int transfer_data(struct tsd_spi *spi, struct tsd_data *data)
{
  // Mark each block of a multiple block write with its own token
  bool multiple = data->blocks > 1;
  uint8_t token = multiple ? TSD_SPI_TOKEN_MULTI_WRITE : TSD_SPI_TOKEN_START_BLOCK;

  uint8_t *buffer = data->buffer;
  for (uint32_t i = 0; i < data->blocks; i++) {
    // Move one block
    int ret;
    if (data->write) {
      ret = write_block(spi, data, buffer, token);
    } else {
      ret = read_block(spi, data, buffer);
    }
    if (ret < 0)
      return ret;

    // Step to the next block
    buffer += data->block_size;
  }

  // Finish here unless a multiple block write needs its stop token
  if (!data->write || !multiple)
    return 0;

  // Send the stop token and skip the byte after it
  uint8_t byte = TSD_SPI_TOKEN_STOP_TRAN;
  int ret      = spi->config.write(spi, &byte, 1);
  if (ret < 0)
    return ret;
  ret = spi->config.read(spi, &byte, 1);
  if (ret < 0)
    return ret;

  // Wait while the card holds its output low to program the last block
  return wait_byte(spi, 0xff, 0x00, DATA_TIMEOUT_MS, &byte);
}

// Select the card, run a command and its data phase, then release the card
static int send_request(struct tsd_host *host, struct tsd_command *command, struct tsd_data *data)
{
  struct tsd_spi *spi = TSD_SPI(host);

  // Refuse an open read, since this host has no continue_read
  if (data) {
    if (data->blocks == 0)
      return TSD_ERR_PARAM;
    data->done = 0;
  }

  // Select the card and run the command
  spi->config.select(spi, true);
  int ret = send_command(spi, command);

  // Run the data phase, then end a multiple block read or a failed transfer with CMD12, keeping the first error
  if (ret == 0 && data) {
    ret = transfer_data(spi, data);
    if (ret < 0 || (!data->write && data->blocks > 1)) {
      struct tsd_command stop = {.index = TSD_CMD_STOP_TRANSMISSION, .response_type = TSD_RESPONSE_R1B};
      int stop_ret            = send_command(spi, &stop);
      if (ret == 0)
        ret = stop_ret;
    }
  }

  // Release the card, then clock a byte so it lets go of its output, keeping the first error
  spi->config.select(spi, false);
  uint8_t byte;
  int release_ret = spi->config.read(spi, &byte, 1);

  return ret < 0 ? ret : release_ret;
}

// Run the bus clock at or below a limit
static int set_clock(struct tsd_host *host, uint32_t clock_khz)
{
  struct tsd_spi *spi = TSD_SPI(host);

  // Leave the clock alone when asked to stop it, since it only runs while a transfer does
  if (clock_khz == 0)
    return 0;

  return spi->config.set_clock(spi, clock_khz);
}

// Take the one data line SPI mode has
static void set_bus_width(struct tsd_host *host, uint8_t width)
{
}

// Report the card busy while it holds its output low
static int card_busy(struct tsd_host *host)
{
  struct tsd_spi *spi = TSD_SPI(host);

  // Select the card and read a byte, which stays low while it programs
  uint8_t byte;
  spi->config.select(spi, true);
  int ret = spi->config.read(spi, &byte, 1);
  spi->config.select(spi, false);
  if (ret < 0)
    return ret;

  return byte != 0xff;
}

// Operations of the SPI host
static const struct tsd_host_api spi_api = {
  .request       = send_request,
  .set_clock     = set_clock,
  .set_bus_width = set_bus_width,
  .card_busy     = card_busy,
  .spi           = true,
};

int tsd_spi_init(struct tsd_spi *spi, struct tsd_spi_config config)
{
  // Keep how to reach the bus and fill in the host interface
  spi->host.api = &spi_api;
  spi->config   = config;

  // Start the clock at the identification rate
  int ret = config.set_clock(spi, IDENTIFICATION_CLOCK_KHZ);
  if (ret < 0)
    return ret;

  // Clock the card with it released, which it needs before its first command
  uint8_t bytes[POWER_UP_BYTES];
  config.select(spi, false);

  return config.read(spi, bytes, sizeof(bytes));
}
