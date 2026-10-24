#include <stdint.h>

#include <tinysd/bits.h>

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// Test getting and preparing a field in the middle of a word
static void test_field_middle()
{
  TEST_ASSERT_EQUAL_HEX32(0x5, TSD_FIELD_GET(TSD_BITS(11, 8), 0x00000500u));
  TEST_ASSERT_EQUAL_HEX32(0x00000500, TSD_FIELD_PREP(TSD_BITS(11, 8), 0x5));
}

// Test getting and preparing a single bit
static void test_field_single_bit()
{
  TEST_ASSERT_EQUAL_HEX32(1, TSD_FIELD_GET(TSD_BIT(31), 0x80000000u));
  TEST_ASSERT_EQUAL_HEX32(0x80000000, TSD_FIELD_PREP(TSD_BIT(31), 1));
}

// Test getting and preparing a field that spans the whole word
static void test_field_whole_word()
{
  TEST_ASSERT_EQUAL_HEX32(0xffffffff, TSD_BITS(31, 0));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, TSD_FIELD_GET(TSD_BITS(31, 0), 0x12345678u));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, TSD_FIELD_PREP(TSD_BITS(31, 0), 0x12345678u));
}

int main(int argc, char **argv)
{
  UNITY_BEGIN();

  RUN_TEST(test_field_middle);
  RUN_TEST(test_field_single_bit);
  RUN_TEST(test_field_whole_word);

  return UNITY_END();
}
