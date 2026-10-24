#include <stdint.h>

#include <tinysd/crc.h>
#include <tinysd/errors.h>
#include <tinysd/frame.h>

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// Test tsd_frame_build_command against the CMD0 example in the SD specification
static void test_build_cmd0()
{
  struct tsd_command command = {.index = 0, .argument = 0};
  uint8_t frame[TSD_FRAME_COMMAND_LEN];
  tsd_frame_build_command(frame, &command);

  uint8_t expected[] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, sizeof(expected));
}

// Test tsd_frame_response_len for each response length
static void test_response_len()
{
  TEST_ASSERT_EQUAL_UINT32(0, tsd_frame_response_len(TSD_RESPONSE_NONE));
  TEST_ASSERT_EQUAL_UINT32(TSD_FRAME_SHORT_RESPONSE_LEN, tsd_frame_response_len(TSD_RESPONSE_R1));
  TEST_ASSERT_EQUAL_UINT32(TSD_FRAME_LONG_RESPONSE_LEN, tsd_frame_response_len(TSD_RESPONSE_R2));
}

// Test tsd_frame_decode_response against the CMD17 response example in the SD specification
static void test_decode_cmd17_response()
{
  struct tsd_command command = {.index = 17, .response_type = TSD_RESPONSE_R1};
  uint8_t bytes[]            = {0x11, 0x00, 0x00, 0x09, 0x00, 0x67};

  TEST_ASSERT_EQUAL_INT(0, tsd_frame_decode_response(&command, bytes));
  TEST_ASSERT_EQUAL_HEX32(0x00000900, command.response[0]);
}

// Test tsd_frame_decode_response refuses a response with a bad CRC
static void test_decode_bad_crc()
{
  struct tsd_command command = {.index = 17, .response_type = TSD_RESPONSE_R1};
  uint8_t bytes[]            = {0x11, 0x00, 0x00, 0x09, 0x00, 0x65};

  TEST_ASSERT_EQUAL_INT(TSD_ERR_COMMAND, tsd_frame_decode_response(&command, bytes));
}

// Test that an R2 is checked over every byte but the first, and comes back without its CRC byte
static void test_decode_r2(void)
{
  struct tsd_command command = {.index = 9, .response_type = TSD_RESPONSE_R2};
  uint8_t bytes[TSD_FRAME_LONG_RESPONSE_LEN];
  bytes[0] = 0x3f;
  for (int i = 1; i < 16; i++)
    bytes[i] = (uint8_t)i;
  bytes[16] = (uint8_t)((tsd_crc7(bytes + 1, 15) << 1) | 1);

  TEST_ASSERT_EQUAL_INT(0, tsd_frame_decode_response(&command, bytes));
  TEST_ASSERT_EQUAL_HEX32(0x01020304, command.response[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0d0e0f00, command.response[3]);
}

int main(int argc, char **argv)
{
  UNITY_BEGIN();

  RUN_TEST(test_build_cmd0);
  RUN_TEST(test_response_len);
  RUN_TEST(test_decode_cmd17_response);
  RUN_TEST(test_decode_bad_crc);
  RUN_TEST(test_decode_r2);

  return UNITY_END();
}
