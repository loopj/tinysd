#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tinysd/bits.h>
#include <tinysd/commands.h>
#include <tinysd/errors.h>
#include <tinysd/sdio.h>

#include "unity.h"

#include "harness.h"

// Where the tests put the common CIS in function 0
#define COMMON_CIS 0x1000

// The card under test
static struct tsd_sdio card;

// Bytes the tests transfer, which the fake host leaves alone
static uint8_t buffer[1024];

// Put a common CIS in function 0 and point the CCCR at it
static void script_cis(const uint8_t *cis, size_t len)
{
  memcpy(&host.f0[COMMON_CIS], cis, len);
  host.f0[TSD_SDIO_CCCR_COMMON_CIS_POINTER]     = (uint8_t)COMMON_CIS;
  host.f0[TSD_SDIO_CCCR_COMMON_CIS_POINTER + 1] = (uint8_t)(COMMON_CIS >> 8);
}

// Bring the card up on one data line
static void init_card(void)
{
  TEST_ASSERT_EQUAL(0, tsd_sdio_init(&card, &host.base, 1, 400));
}

void setUp(void)
{
  // Clear the fake host
  harness_reset();

  // Script a card that powers up at once and takes address 1
  host.replies[TSD_CMD_IO_SEND_OP_COND].response[0]    = TSD_OCR_READY;
  host.replies[TSD_CMD_SEND_RELATIVE_ADDR].response[0] = 0x00010000;

  // Give it a common CIS that holds only the end tuple
  uint8_t cis[] = {0xff};
  script_cis(cis, sizeof(cis));
}

void tearDown(void)
{
}

// Test that init walks past another tuple to the manufacturer tuple, with the JEDEC example in the SDIO specification
static void test_manufacturer_from_cis(void)
{
  uint8_t cis[] = {
    0x21, 0x02, 0x0c, 0x00,             // CISTPL_FUNCID, which the walk steps over
    0x20, 0x04, 0x89, 0x00, 0x34, 0x12, // CISTPL_MANFID, JEDEC code 89h as 0089h, card 1234h
    0xff,                               // CISTPL_END
  };
  script_cis(cis, sizeof(cis));

  init_card();
  TEST_ASSERT_EQUAL_HEX16(0x0089, card.manufacturer_id);
  TEST_ASSERT_EQUAL_HEX16(0x1234, card.card_id);
}

// Test that a transfer moves whole blocks in block mode, then the rest in byte mode from where they ended
static void test_transfer_split(void)
{
  init_card();
  int first = host.command_count;

  uint32_t done = 0;
  TEST_ASSERT_EQUAL(0, tsd_sdio_transfer(&card, 1, 0, false, buffer, 1000, 256, &done));
  TEST_ASSERT_EQUAL_UINT32(1000, done);
  TEST_ASSERT_EQUAL(first + 2, host.command_count);

  uint32_t block_mode = host.commands[first].argument;
  TEST_ASSERT_TRUE(block_mode & TSD_SDIO_ARG_BLOCK_MODE);
  TEST_ASSERT_EQUAL_UINT32(3, TSD_FIELD_GET(TSD_SDIO_ARG_COUNT, block_mode));

  uint32_t byte_mode = host.commands[first + 1].argument;
  TEST_ASSERT_FALSE(byte_mode & TSD_SDIO_ARG_BLOCK_MODE);
  TEST_ASSERT_EQUAL_UINT32(768, TSD_FIELD_GET(TSD_SDIO_ARG_ADDRESS, byte_mode));
  TEST_ASSERT_EQUAL_UINT32(232, TSD_FIELD_GET(TSD_SDIO_ARG_COUNT, byte_mode));
}

// Test that a 512-byte transfer in byte mode encodes its count as 0
static void test_byte_mode_512(void)
{
  init_card();

  TEST_ASSERT_EQUAL(0, tsd_sdio_transfer(&card, 1, 0, false, buffer, 512, 0, NULL));
  TEST_ASSERT_EQUAL_UINT32(0, TSD_FIELD_GET(TSD_SDIO_ARG_COUNT, harness_find(TSD_CMD_IO_RW_EXTENDED)->argument));
}

// Test that a function past 7 or an address past the 17-bit field is refused without a command
static void test_argument_out_of_range(void)
{
  init_card();
  int sent = host.command_count;

  uint8_t value;
  TEST_ASSERT_EQUAL(TSD_ERR_PARAM, tsd_sdio_read_byte(&card, 8, 0x10, &value));
  TEST_ASSERT_EQUAL(TSD_ERR_PARAM, tsd_sdio_read_byte(&card, 1, 0x20000, &value));
  TEST_ASSERT_EQUAL(sent, host.command_count);
}

// Test that an error flag in a CMD52 or CMD53 reply fails the access
static void test_response_flags(void)
{
  init_card();

  uint8_t value;
  host.replies[TSD_CMD_IO_RW_DIRECT].response[0] = TSD_SDIO_R5_OUT_OF_RANGE;
  TEST_ASSERT_EQUAL(TSD_ERR_PARAM, tsd_sdio_read_byte(&card, 1, 0x10, &value));

  host.replies[TSD_CMD_IO_RW_EXTENDED].response[0] = TSD_SDIO_R5_ERROR;
  TEST_ASSERT_EQUAL(TSD_ERR_CARD, tsd_sdio_transfer(&card, 1, 0, false, buffer, 4, 0, NULL));
}

// Test that a card is switched to high speed before a clock past 25 MHz, and one without it stays at 25 MHz
static void test_high_speed(void)
{
  host.f0[TSD_SDIO_CCCR_SDIO_REVISION]    = TSD_SDIO_REVISION_CCCR_1_20;
  host.f0[TSD_SDIO_CCCR_BUS_SPEED_SELECT] = TSD_SDIO_SPEED_SHS;
  TEST_ASSERT_EQUAL(0, tsd_sdio_init(&card, &host.base, 4, 50000));
  TEST_ASSERT_TRUE(host.f0[TSD_SDIO_CCCR_BUS_SPEED_SELECT] & TSD_SDIO_SPEED_EHS);
  TEST_ASSERT_EQUAL_UINT32(50000, host.clock_khz);

  host.f0[TSD_SDIO_CCCR_BUS_SPEED_SELECT] = 0;
  TEST_ASSERT_EQUAL(0, tsd_sdio_init(&card, &host.base, 4, 50000));
  TEST_ASSERT_EQUAL_UINT32(25000, host.clock_khz);
}

int main(int argc, char **argv)
{
  UNITY_BEGIN();

  RUN_TEST(test_manufacturer_from_cis);
  RUN_TEST(test_transfer_split);
  RUN_TEST(test_byte_mode_512);
  RUN_TEST(test_argument_out_of_range);
  RUN_TEST(test_response_flags);
  RUN_TEST(test_high_speed);

  return UNITY_END();
}
