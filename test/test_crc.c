#include <stdint.h>

#include <tinysd/crc.h>

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// Test tsd_crc7 against the CMD0 example in the SD specification
static void test_crc7_cmd0()
{
  uint8_t frame[] = {0x40, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_HEX8(0x4a, tsd_crc7(frame, sizeof(frame)));
}

// Test tsd_crc7 against the CMD17 example in the SD specification
static void test_crc7_cmd17()
{
  uint8_t frame[] = {0x51, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_HEX8(0x2a, tsd_crc7(frame, sizeof(frame)));
}

// Test tsd_crc7 against the CMD17 response example in the SD specification
static void test_crc7_cmd17_response()
{
  uint8_t frame[] = {0x11, 0x00, 0x00, 0x09, 0x00};
  TEST_ASSERT_EQUAL_HEX8(0x33, tsd_crc7(frame, sizeof(frame)));
}

// Test tsd_crc16_update against the 512 bytes of 0xFF example in the SD specification
static void test_crc16_block_of_ff()
{
  uint16_t crc = 0;
  for (int i = 0; i < 512; i++)
    crc = tsd_crc16_update(crc, 0xff);
  TEST_ASSERT_EQUAL_HEX16(0x7fa1, crc);
}

int main(int argc, char **argv)
{
  UNITY_BEGIN();

  RUN_TEST(test_crc7_cmd0);
  RUN_TEST(test_crc7_cmd17);
  RUN_TEST(test_crc7_cmd17_response);
  RUN_TEST(test_crc16_block_of_ff);

  return UNITY_END();
}
