#include <stdint.h>

#include <tinysd/card.h>
#include <tinysd/commands.h>
#include <tinysd/errors.h>

#include "unity.h"

#include "harness.h"

// The card under test
static struct tsd_card card;

// Blocks the tests read and write, which the fake host leaves alone
static uint8_t buffer[8 * TSD_CARD_BLOCK_SIZE];

// Script a version 1 CSD
static void set_csd_v1(uint32_t c_size, uint32_t c_size_mult, uint32_t read_bl_len)
{
  uint32_t *csd = host.replies[TSD_CMD_SEND_CSD].response;
  csd[0]        = 0;
  csd[1]        = read_bl_len << 16 | c_size >> 2;
  csd[2]        = (c_size & 3) << 30 | c_size_mult << 15;
}

// Script a version 2 CSD
static void set_csd_v2(uint32_t c_size)
{
  uint32_t *csd = host.replies[TSD_CMD_SEND_CSD].response;
  csd[0]        = 0x40000000;
  csd[1]        = c_size >> 16;
  csd[2]        = (c_size & 0xffff) << 16;
}

// Bring up a high capacity card
static void init_high_capacity(void)
{
  set_csd_v2(0x001010);
  TEST_ASSERT_EQUAL_INT(0, tsd_card_init(&card, &host.base, 1, 400));
}

void setUp(void)
{
  // Clear the fake host
  harness_reset();

  // Script a version 2 card that answers every init command, leaving the CSD to each test
  host.replies[TSD_CMD_SEND_IF_COND].response[0]       = 0x1aa;
  host.replies[TSD_ACMD_SD_SEND_OP_COND].response[0]   = TSD_OCR_READY | TSD_OCR_CCS;
  host.replies[TSD_CMD_SEND_RELATIVE_ADDR].response[0] = 0x00010000;

  // Script a card ready for data in the transfer state, for the CMD13 after a write or an erase
  host.replies[TSD_CMD_SEND_STATUS].response[0] =
    TSD_STATUS_READY_FOR_DATA | TSD_FIELD_PREP(TSD_STATUS_CURRENT_STATE, TSD_STATUS_STATE_TRAN);
}

void tearDown(void)
{
}

// Test the 32 MB version 1 example in the SD specification, C_SIZE 2000 and C_SIZE_MULT 3 with 512-byte blocks
static void test_capacity_v1_example(void)
{
  set_csd_v1(2000, 3, 9);
  TEST_ASSERT_EQUAL_INT(0, tsd_card_init(&card, &host.base, 1, 400));
  TEST_ASSERT_EQUAL_UINT32(64032, card.blocks);
}

// Test the smallest SDHC card in the SD specification, C_SIZE 001010h
static void test_capacity_smallest_sdhc(void)
{
  set_csd_v2(0x001010);
  TEST_ASSERT_EQUAL_INT(0, tsd_card_init(&card, &host.base, 1, 400));
  TEST_ASSERT_EQUAL_UINT32(4211712, card.blocks);
}

// Test the largest SDXC card in the SD specification, C_SIZE 3FFEFFh
static void test_capacity_largest_sdxc(void)
{
  set_csd_v2(0x3ffeff);
  TEST_ASSERT_EQUAL_INT(0, tsd_card_init(&card, &host.base, 1, 400));
  TEST_ASSERT_EQUAL_UINT32(4294705152u, card.blocks);
}

// Test that a version 3 CSD, which only SDUC cards use, is refused
static void test_capacity_v3_refused(void)
{
  host.replies[TSD_CMD_SEND_CSD].response[0] = 0x80000000;
  TEST_ASSERT_EQUAL_INT(TSD_ERR_COMMAND, tsd_card_init(&card, &host.base, 1, 400));
}

// Test that a version 1 card, which does not answer CMD8, is offered no high capacity and gets 512-byte blocks
static void test_init_version1(void)
{
  host.replies[TSD_CMD_SEND_IF_COND].ret = TSD_ERR_TIMEOUT;
  set_csd_v1(2000, 3, 9);

  TEST_ASSERT_EQUAL_INT(0, tsd_card_init(&card, &host.base, 1, 400));
  TEST_ASSERT_FALSE(card.high_capacity);
  TEST_ASSERT_FALSE(harness_find(TSD_ACMD_SD_SEND_OP_COND)->argument & TSD_OCR_CCS);
  TEST_ASSERT_EQUAL_UINT32(TSD_CARD_BLOCK_SIZE, harness_find(TSD_CMD_SET_BLOCKLEN)->argument);
}

// Test that a standard capacity card is read by byte offset
static void test_read_standard_capacity_by_offset(void)
{
  host.replies[TSD_ACMD_SD_SEND_OP_COND].response[0] = TSD_OCR_READY;
  set_csd_v1(2000, 3, 9);
  TEST_ASSERT_EQUAL_INT(0, tsd_card_init(&card, &host.base, 1, 400));

  TEST_ASSERT_EQUAL_INT(0, tsd_card_read(&card, buffer, 3, 1));
  TEST_ASSERT_EQUAL_UINT32(3 * TSD_CARD_BLOCK_SIZE, harness_find(TSD_CMD_READ_SINGLE_BLOCK)->argument);
}

// Test that a high capacity card is read by block number
static void test_read_high_capacity_by_block(void)
{
  init_high_capacity();

  TEST_ASSERT_EQUAL_INT(0, tsd_card_read(&card, buffer, 3, 1));
  TEST_ASSERT_EQUAL_UINT32(3, harness_find(TSD_CMD_READ_SINGLE_BLOCK)->argument);
}

// Test that sequential reads continue one open read, and that a write closes it first
static void test_open_read(void)
{
  init_high_capacity();

  TEST_ASSERT_EQUAL_INT(0, tsd_card_read(&card, buffer, 0, 4));
  TEST_ASSERT_EQUAL_INT(0, tsd_card_read(&card, buffer, 4, 4));
  TEST_ASSERT_EQUAL_INT(1, harness_count(TSD_CMD_READ_MULTIPLE_BLOCK));
  TEST_ASSERT_EQUAL_UINT32(8, host.continued_blocks);

  TEST_ASSERT_EQUAL_INT(0, tsd_card_write(&card, buffer, 100, 1));
  TEST_ASSERT_EQUAL_INT(1, harness_count(TSD_CMD_STOP_TRANSMISSION));
  TEST_ASSERT_FALSE(card.reading);
}

// Test that a multiple block write ends with CMD12 as an R1b, and reports an error bit in its status
static void test_multiple_block_write(void)
{
  init_high_capacity();

  TEST_ASSERT_EQUAL_INT(0, tsd_card_write(&card, buffer, 0, 4));
  TEST_ASSERT_EQUAL_INT(TSD_RESPONSE_R1B, harness_find(TSD_CMD_STOP_TRANSMISSION)->response_type);

  host.replies[TSD_CMD_STOP_TRANSMISSION].response[0] = TSD_STATUS_WP_VIOLATION;
  TEST_ASSERT_EQUAL_INT(TSD_ERR_CARD, tsd_card_write(&card, buffer, 0, 4));
}

// Test that an erase sets its first and last blocks, then erases without a busy
static void test_erase(void)
{
  init_high_capacity();

  TEST_ASSERT_EQUAL_INT(0, tsd_card_erase(&card, 8, 8));
  TEST_ASSERT_EQUAL_UINT32(8, harness_find(TSD_CMD_ERASE_WR_BLK_START)->argument);
  TEST_ASSERT_EQUAL_UINT32(15, harness_find(TSD_CMD_ERASE_WR_BLK_END)->argument);
  TEST_ASSERT_EQUAL_INT(TSD_RESPONSE_R1, harness_find(TSD_CMD_ERASE)->response_type);
}

// Test that an erase sends CMD13 only once the card lets go of its data line
static void test_erase_waits_while_busy(void)
{
  init_high_capacity();
  host.busy_checks = 3;

  TEST_ASSERT_EQUAL_INT(0, tsd_card_erase(&card, 8, 8));
  TEST_ASSERT_EQUAL_INT(0, host.busy_checks);
  TEST_ASSERT_EQUAL_INT(1, harness_count(TSD_CMD_SEND_STATUS));
}

// Test that an erase reports an error bit the card sets once it finishes
static void test_erase_error(void)
{
  init_high_capacity();
  host.replies[TSD_CMD_SEND_STATUS].response[0] = TSD_STATUS_ERASE_PARAM;

  TEST_ASSERT_EQUAL_INT(TSD_ERR_CARD, tsd_card_erase(&card, 8, 8));
}

// Test that a read, write or erase past the end is refused without a command
static void test_past_the_end(void)
{
  init_high_capacity();
  int sent = host.command_count;

  TEST_ASSERT_EQUAL_INT(TSD_ERR_PARAM, tsd_card_read(&card, buffer, card.blocks - 1, 2));
  TEST_ASSERT_EQUAL_INT(TSD_ERR_PARAM, tsd_card_write(&card, buffer, card.blocks, 1));
  TEST_ASSERT_EQUAL_INT(TSD_ERR_PARAM, tsd_card_erase(&card, card.blocks - 1, 2));
  TEST_ASSERT_EQUAL_INT(sent, host.command_count);
}

int main(int argc, char **argv)
{
  UNITY_BEGIN();

  RUN_TEST(test_capacity_v1_example);
  RUN_TEST(test_capacity_smallest_sdhc);
  RUN_TEST(test_capacity_largest_sdxc);
  RUN_TEST(test_capacity_v3_refused);
  RUN_TEST(test_init_version1);
  RUN_TEST(test_read_standard_capacity_by_offset);
  RUN_TEST(test_read_high_capacity_by_block);
  RUN_TEST(test_open_read);
  RUN_TEST(test_multiple_block_write);
  RUN_TEST(test_erase);
  RUN_TEST(test_erase_waits_while_busy);
  RUN_TEST(test_erase_error);
  RUN_TEST(test_past_the_end);

  return UNITY_END();
}
