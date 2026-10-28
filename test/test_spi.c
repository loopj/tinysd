#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <tinysd/commands.h>
#include <tinysd/crc.h>
#include <tinysd/delay.h>
#include <tinysd/errors.h>
#include <tinysd/host/spi.h>

#include "unity.h"

// Most bytes one test queues for the card to send or records from the host
#define FAKE_BUS_LEN 2048

// Bytes in the blocks the tests move
#define BLOCK_SIZE 512

// A bus that records what the host sends and answers reads from a queue, then with 0xff once it runs out
struct fake_bus {
  struct tsd_spi spi;

  // Bytes the host sent
  uint8_t sent[FAKE_BUS_LEN];
  uint32_t sent_len;

  // Bytes the card sends, and how many the host has read
  uint8_t replies[FAKE_BUS_LEN];
  uint32_t reply_len;
  uint32_t reply_idx;

  // Chip select and clock as the host last set them
  bool selected;
  uint32_t clock_khz;
};

// The bus the host under test drives
static struct fake_bus bus;

// Blocks the tests read and write
static uint8_t blocks[2 * BLOCK_SIZE];

// Delay nothing, since the fake bus answers at once
void tsd_delay_us(uint32_t microseconds)
{
}

// Record the bytes the host sends
static int fake_write(struct tsd_spi *spi, const uint8_t *buffer, uint32_t len)
{
  TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(FAKE_BUS_LEN, bus.sent_len + len, "too many bytes sent");
  memcpy(&bus.sent[bus.sent_len], buffer, len);
  bus.sent_len += len;

  return 0;
}

// Answer from the queue, then with 0xff
static int fake_read(struct tsd_spi *spi, uint8_t *buffer, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++)
    buffer[i] = bus.reply_idx < bus.reply_len ? bus.replies[bus.reply_idx++] : 0xff;

  return 0;
}

// Record chip select
static void fake_select(struct tsd_spi *spi, bool selected)
{
  bus.selected = selected;
}

// Record the clock
static int fake_set_clock(struct tsd_spi *spi, uint32_t clock_khz)
{
  bus.clock_khz = clock_khz;

  return 0;
}

// Queue bytes for the card to send
static void queue(const uint8_t *bytes, uint32_t len)
{
  TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(FAKE_BUS_LEN, bus.reply_len + len, "too many bytes queued");
  memcpy(&bus.replies[bus.reply_len], bytes, len);
  bus.reply_len += len;
}

// Queue one block and its CRC16, with the CRC16 spoiled when asked
static void queue_block(const uint8_t *block, bool bad_crc)
{
  uint16_t crc = 0;
  for (uint32_t i = 0; i < BLOCK_SIZE; i++)
    crc = tsd_crc16_update(crc, block[i]);
  if (bad_crc)
    crc ^= 1;

  uint8_t token = TSD_SPI_TOKEN_START_BLOCK;
  queue(&token, 1);
  queue(block, BLOCK_SIZE);
  uint8_t crc_bytes[] = {crc >> 8, crc & 0xff};
  queue(crc_bytes, sizeof(crc_bytes));
}

// Run a command with no data phase and return its response word
static int send_command(uint8_t index, uint32_t argument, enum tsd_response response_type, uint32_t *response)
{
  return tsd_host_send_command(&bus.spi.host, index, argument, response_type, response);
}

// Run a one or two block transfer of the test blocks
static int transfer(uint8_t index, uint32_t count, bool write, uint32_t *done)
{
  struct tsd_command command = {.index = index, .response_type = TSD_RESPONSE_R1};
  struct tsd_data data       = {.buffer = blocks, .block_size = BLOCK_SIZE, .blocks = count, .write = write};
  int ret                    = tsd_host_request(&bus.spi.host, &command, &data);
  *done                      = data.done;

  return ret;
}

void setUp(void)
{
  // Clear the bus and start the host on it
  memset(&bus, 0, sizeof(bus));
  struct tsd_spi_config config = {
    .write     = fake_write,
    .read      = fake_read,
    .select    = fake_select,
    .set_clock = fake_set_clock,
  };
  TEST_ASSERT_EQUAL_INT(0, tsd_spi_init(&bus.spi, config));

  // Fill the test blocks with a pattern
  for (uint32_t i = 0; i < sizeof(blocks); i++)
    blocks[i] = (uint8_t)(i * 7);
}

void tearDown(void)
{
}

// Test that init clocks the card at 400 kHz with it released, and marks the host as SPI
static void test_init(void)
{
  TEST_ASSERT_EQUAL_UINT32(400, bus.clock_khz);
  TEST_ASSERT_FALSE(bus.selected);
  TEST_ASSERT_EQUAL_UINT32(0, bus.sent_len);
  TEST_ASSERT_TRUE(bus.spi.host.api->spi);
}

// Test that CMD0 goes out after a byte of ones with the CRC7 from the SD specification, and takes an idle R1
static void test_cmd0(void)
{
  uint8_t replies[] = {0xff, 0xff, TSD_SPI_R1_IN_IDLE_STATE};
  queue(replies, sizeof(replies));

  uint32_t response = 0;
  TEST_ASSERT_EQUAL_INT(0, send_command(TSD_CMD_GO_IDLE_STATE, 0, TSD_RESPONSE_NONE, &response));
  TEST_ASSERT_EQUAL_HEX32(TSD_SPI_R1_IN_IDLE_STATE, response);

  uint8_t expected[] = {0xff, 0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), bus.sent_len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, bus.sent, sizeof(expected));
  TEST_ASSERT_FALSE(bus.selected);
}

// Test that an R7 leaves its four bytes in the response word
static void test_r7(void)
{
  uint8_t replies[] = {0xff, TSD_SPI_R1_IN_IDLE_STATE, 0x00, 0x00, 0x01, 0xaa};
  queue(replies, sizeof(replies));

  uint32_t response = 0;
  TEST_ASSERT_EQUAL_INT(0, send_command(TSD_CMD_SEND_IF_COND, 0x1aa, TSD_RESPONSE_R7, &response));
  TEST_ASSERT_EQUAL_HEX32(0x1aa, response);
}

// Test that an R2 puts its second status byte above the R1
static void test_r2(void)
{
  uint8_t replies[] = {0xff, 0x00, 0x20};
  queue(replies, sizeof(replies));

  uint32_t response = 0;
  TEST_ASSERT_EQUAL_INT(0, send_command(TSD_CMD_SEND_STATUS, 0, TSD_RESPONSE_R2, &response));
  TEST_ASSERT_EQUAL_HEX32(TSD_SPI_R2_WP_VIOLATION, response);
}

// Test that the error bits of an R1 become tsd_errors, and that a card that never answers times out
static void test_r1_errors(void)
{
  uint8_t illegal[] = {0xff, TSD_SPI_R1_IN_IDLE_STATE | TSD_SPI_R1_ILLEGAL_COMMAND};
  queue(illegal, sizeof(illegal));
  struct tsd_command command = {.index = TSD_CMD_SEND_IF_COND, .argument = 0x1aa, .response_type = TSD_RESPONSE_R7};
  TEST_ASSERT_EQUAL_INT(TSD_ERR_COMMAND, tsd_host_request(&bus.spi.host, &command, NULL));
  TEST_ASSERT_EQUAL_HEX32(TSD_SPI_R1_IN_IDLE_STATE | TSD_SPI_R1_ILLEGAL_COMMAND, command.response[0]);

  uint8_t address[] = {0xff, TSD_SPI_R1_ADDRESS_ERROR};
  queue(address, sizeof(address));
  TEST_ASSERT_EQUAL_INT(TSD_ERR_PARAM, send_command(TSD_CMD_SET_BLOCKLEN, 0, TSD_RESPONSE_R1, NULL));

  TEST_ASSERT_EQUAL_INT(TSD_ERR_TIMEOUT, send_command(TSD_CMD_SEND_STATUS, 0, TSD_RESPONSE_R2, NULL));
}

// Test that an R1b reads through the busy bytes, leaving the last byte for the release after it
static void test_r1b_busy(void)
{
  uint8_t replies[] = {0xff, 0x00, 0x00, 0x00, 0xff, 0x55};
  queue(replies, sizeof(replies));

  TEST_ASSERT_EQUAL_INT(0, send_command(TSD_CMD_STOP_TRANSMISSION, 0, TSD_RESPONSE_R1B, NULL));
  TEST_ASSERT_EQUAL_UINT32(sizeof(replies), bus.reply_idx);
}

// Test that the card reads as busy while it holds its output low, with it selected and then released
static void test_card_busy(void)
{
  uint8_t replies[] = {0x00, 0xff};
  queue(replies, sizeof(replies));

  TEST_ASSERT_EQUAL_INT(1, tsd_host_card_busy(&bus.spi.host));
  TEST_ASSERT_FALSE(bus.selected);
  TEST_ASSERT_EQUAL_INT(0, tsd_host_card_busy(&bus.spi.host));
  TEST_ASSERT_EQUAL_UINT32(0, bus.sent_len);
}

// Test that a read takes a block after its token and checks its CRC16, counting a block with a bad CRC as moved
static void test_read_block(void)
{
  uint8_t expected[BLOCK_SIZE];
  memcpy(expected, blocks, BLOCK_SIZE);

  uint8_t response[] = {0xff, 0x00, 0xff, 0xff};
  queue(response, sizeof(response));
  queue_block(expected, false);
  memset(blocks, 0, sizeof(blocks));

  uint32_t done = 0;
  TEST_ASSERT_EQUAL_INT(0, transfer(TSD_CMD_READ_SINGLE_BLOCK, 1, false, &done));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, blocks, BLOCK_SIZE);
  TEST_ASSERT_EQUAL_UINT32(BLOCK_SIZE, done);

  queue(response, sizeof(response));
  queue_block(expected, true);
  TEST_ASSERT_EQUAL_INT(TSD_ERR_DATA, transfer(TSD_CMD_READ_SINGLE_BLOCK, 1, false, &done));
  TEST_ASSERT_EQUAL_UINT32(BLOCK_SIZE, done);
}

// Test that a multiple block read ends with CMD12 while the card is still selected
static void test_read_multiple(void)
{
  uint8_t expected[2 * BLOCK_SIZE];
  memcpy(expected, blocks, sizeof(expected));

  uint8_t response[] = {0xff, 0x00};
  queue(response, sizeof(response));
  queue_block(expected, false);
  queue_block(&expected[BLOCK_SIZE], false);
  uint8_t stop[] = {0xff, 0x00, 0xff};
  queue(stop, sizeof(stop));
  memset(blocks, 0, sizeof(blocks));

  uint32_t done = 0;
  TEST_ASSERT_EQUAL_INT(0, transfer(TSD_CMD_READ_MULTIPLE_BLOCK, 2, false, &done));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, blocks, sizeof(expected));
  TEST_ASSERT_EQUAL_UINT32(2 * BLOCK_SIZE, done);
  TEST_ASSERT_EQUAL_UINT32(14, bus.sent_len);
  TEST_ASSERT_EQUAL_HEX8(0x40 | TSD_CMD_STOP_TRANSMISSION, bus.sent[8]);
}

// Test that a multiple block write marks each block with its token and ends with the stop token
static void test_write_multiple(void)
{
  uint8_t replies[] = {
    0xff, 0x00,       // the byte after the frame, then the R1
    0x05, 0x00, 0xff, // the first block accepted, then the busy
    0x05, 0x00, 0xff, // the second block accepted, then the busy
    0xff, 0x00, 0xff, // the byte after the stop token, then the busy
  };
  queue(replies, sizeof(replies));

  uint32_t done = 0;
  TEST_ASSERT_EQUAL_INT(0, transfer(TSD_CMD_WRITE_MULTIPLE_BLOCK, 2, true, &done));
  TEST_ASSERT_EQUAL_UINT32(2 * BLOCK_SIZE, done);

  uint32_t first  = 7;
  uint32_t second = first + 2 + BLOCK_SIZE + 2;
  TEST_ASSERT_EQUAL_HEX8(0xff, bus.sent[first]);
  TEST_ASSERT_EQUAL_HEX8(TSD_SPI_TOKEN_MULTI_WRITE, bus.sent[first + 1]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(blocks, &bus.sent[first + 2], BLOCK_SIZE);
  TEST_ASSERT_EQUAL_HEX8(TSD_SPI_TOKEN_MULTI_WRITE, bus.sent[second + 1]);
  TEST_ASSERT_EQUAL_HEX8(TSD_SPI_TOKEN_STOP_TRAN, bus.sent[bus.sent_len - 1]);
}

// Test that a rejected block reports a CRC error as a failed transfer and a write error as the card's, then sends CMD12
static void test_write_rejected(void)
{
  uint32_t done     = 0;
  uint8_t crc_bad[] = {0xff, 0x00, 0x0b, 0xff, 0x00, 0xff};
  queue(crc_bad, sizeof(crc_bad));
  TEST_ASSERT_EQUAL_INT(TSD_ERR_DATA, transfer(TSD_CMD_WRITE_BLOCK, 1, true, &done));
  TEST_ASSERT_EQUAL_UINT32(0, done);
  TEST_ASSERT_EQUAL_HEX8(0x40 | TSD_CMD_STOP_TRANSMISSION, bus.sent[bus.sent_len - 6]);

  uint8_t write_bad[] = {0xff, 0x00, 0x0d, 0xff, 0x00, 0xff};
  queue(write_bad, sizeof(write_bad));
  TEST_ASSERT_EQUAL_INT(TSD_ERR_CARD, transfer(TSD_CMD_WRITE_BLOCK, 1, true, &done));
}

int main(int argc, char **argv)
{
  UNITY_BEGIN();

  RUN_TEST(test_init);
  RUN_TEST(test_cmd0);
  RUN_TEST(test_r7);
  RUN_TEST(test_r2);
  RUN_TEST(test_r1_errors);
  RUN_TEST(test_r1b_busy);
  RUN_TEST(test_card_busy);
  RUN_TEST(test_read_block);
  RUN_TEST(test_read_multiple);
  RUN_TEST(test_write_multiple);
  RUN_TEST(test_write_rejected);

  return UNITY_END();
}
