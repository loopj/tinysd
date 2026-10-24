# Fetch the Unity test framework
if(NOT unity_FOUND)
  include(FetchContent)
  FetchContent_Declare(Unity GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git)
  FetchContent_MakeAvailable(Unity)
endif()

# Little helper function to create a test
function(add_tinysd_test TEST_NAME)
  # Create the test executable
  add_executable(${TEST_NAME} ${ARGN})

  # Enable compiler warnings for the test and library sources
  target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)

  # Link against the frame and CRC helpers and Unity
  target_link_libraries(${TEST_NAME} tinysd_frame unity::framework)

  # Add the test to CTest
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Bit field macro tests
add_tinysd_test(test_bits test_bits.c)

# CRC tests
add_tinysd_test(test_crc test_crc.c)

# Command and response frame tests
add_tinysd_test(test_frame test_frame.c)

# SD memory card driver tests, against the fake host in harness.h
add_tinysd_test(test_card test_card.c)
target_link_libraries(test_card tinysd_card)

# SDIO card driver tests, against the fake host in harness.h
add_tinysd_test(test_sdio test_sdio.c)
target_link_libraries(test_sdio tinysd_sdio)
