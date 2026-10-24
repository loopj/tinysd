#pragma once

#include <stdint.h>

#include <tinysd/host.h>

/** Bytes in a command frame. */
#define TSD_FRAME_COMMAND_LEN 6

/** Bytes in a 48-bit response. */
#define TSD_FRAME_SHORT_RESPONSE_LEN 6

/** Bytes in a 136-bit response. */
#define TSD_FRAME_LONG_RESPONSE_LEN 17

/** CRC status a card returns on DAT0 for a written block it accepted. */
#define TSD_FRAME_CRC_STATUS_ACCEPTED 0x2

/**
 * Build a command's frame, from its start bits to its CRC7 and end bit.
 *
 * @param frame receives TSD_FRAME_COMMAND_LEN bytes
 * @param command the command
 */
void tsd_frame_build_command(uint8_t *frame, const struct tsd_command *command);

/**
 * Give the bytes a response takes on the bus.
 *
 * @param response_type the response type
 * @return TSD_FRAME_SHORT_RESPONSE_LEN, TSD_FRAME_LONG_RESPONSE_LEN, or 0 for no response
 */
uint32_t tsd_frame_response_len(enum tsd_response response_type);

/**
 * Check a response read from its start bits and put its payload in the command.
 *
 * @param command the command, which gives the response type to check and receives the response
 * @param bytes the response, tsd_frame_response_len bytes from its start bits
 * @return 0, or TSD_ERR_COMMAND when its CRC or index does not match
 */
int tsd_frame_decode_response(struct tsd_command *command, const uint8_t *bytes);
