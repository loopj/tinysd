#pragma once

#include <stdint.h>

/** CRC16 of each byte value, which tsd_crc16_update reads. */
extern const uint16_t tsd_crc16_table[256];

/**
 * Compute the CRC7 that command and response frames carry.
 *
 * @param bytes the bytes to compute it over
 * @param len the number of bytes
 * @return the CRC in the low seven bits
 */
uint8_t tsd_crc7(const uint8_t *bytes, uint32_t len);

/**
 * Fold one byte into a running CRC16, the CRC each data line carries.
 *
 * @param crc the running CRC, or 0 to start one
 * @param byte the next byte the line carries
 * @return the updated CRC
 */
static inline uint16_t tsd_crc16_update(uint16_t crc, uint8_t byte)
{
  return tsd_crc16_table[(crc >> 8) ^ byte] ^ (uint16_t)(crc << 8);
}

/**
 * Interleave four data lines' CRCs into the order the 4-bit bus sends them.
 *
 * @param line_crc the four data lines' CRCs
 * @param crc filled with four 16-bit words to send first to last
 */
void tsd_crc16_interleave(const uint16_t *line_crc, uint16_t *crc);
