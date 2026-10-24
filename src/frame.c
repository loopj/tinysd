#include <stdbool.h>
#include <stdint.h>

#include <tinysd/crc.h>
#include <tinysd/errors.h>

#include <tinysd/frame.h>

void tsd_frame_build_command(uint8_t *frame, const struct tsd_command *command)
{
  // Put the start and transmission bits above the index, then the argument most significant byte first
  frame[0] = (uint8_t)(0x40 | (command->index & 0x3f));
  frame[1] = (uint8_t)(command->argument >> 24);
  frame[2] = (uint8_t)(command->argument >> 16);
  frame[3] = (uint8_t)(command->argument >> 8);
  frame[4] = (uint8_t)command->argument;

  // Close with the CRC7 and the end bit
  frame[5] = (uint8_t)((tsd_crc7(frame, TSD_FRAME_COMMAND_LEN - 1) << 1) | 1);
}

uint32_t tsd_frame_response_len(enum tsd_response response_type)
{
  switch (response_type) {
    case TSD_RESPONSE_NONE:
      // Nothing comes back
      return 0;

    case TSD_RESPONSE_R2:
      // The CID or CSD comes back in 136 bits
      return TSD_FRAME_LONG_RESPONSE_LEN;

    default:
      // Everything else comes back in 48 bits
      return TSD_FRAME_SHORT_RESPONSE_LEN;
  }
}

int tsd_frame_decode_response(struct tsd_command *command, const uint8_t *bytes)
{
  enum tsd_response type = command->response_type;

  // Take nothing from a command without a response
  if (type == TSD_RESPONSE_NONE)
    return 0;

  // Check an R2's CRC, computed over every byte but the first
  if (type == TSD_RESPONSE_R2 && tsd_crc7(bytes + 1, TSD_FRAME_LONG_RESPONSE_LEN - 2) != bytes[16] >> 1)
    return TSD_ERR_COMMAND;

  // Check a short response's CRC and index, apart from an R3 or R4, which carries neither
  bool short_checked = type != TSD_RESPONSE_R2 && type != TSD_RESPONSE_R3 && type != TSD_RESPONSE_R4;
  if (short_checked && tsd_crc7(bytes, TSD_FRAME_SHORT_RESPONSE_LEN - 1) != bytes[5] >> 1)
    return TSD_ERR_COMMAND;
  if (short_checked && (bytes[0] & 0x3f) != command->index)
    return TSD_ERR_COMMAND;

  // Take the payload a word at a time, most significant byte first, four words from an R2 and one from the rest
  uint32_t words = type == TSD_RESPONSE_R2 ? 4 : 1;
  for (uint32_t i = 0; i < words; i++) {
    const uint8_t *word  = bytes + 1 + i * 4;
    command->response[i] = ((uint32_t)word[0] << 24) | ((uint32_t)word[1] << 16) | ((uint32_t)word[2] << 8) | word[3];
  }

  // Clear an R2's CRC byte, so every host returns the same 120 bits
  if (type == TSD_RESPONSE_R2)
    command->response[3] &= ~0xffu;

  return 0;
}
