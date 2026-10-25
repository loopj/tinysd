#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tinysd/errors.h>
#include <tinysd/host.h>

/** Bytes in each block the card driver reads and writes. */
#define TSD_CARD_BLOCK_SIZE 512

/**
 * An SD memory card.
 */
struct tsd_card {
  /** The host the card sits on, which the caller started. */
  struct tsd_host *host;

  /** Relative card address. */
  uint32_t address;

  /** The card takes block numbers as addresses, rather than byte offsets. */
  bool high_capacity;

  /** Number of blocks the card holds. */
  uint32_t blocks;

  /** A multiple block read is open, for the next tsd_card_read to continue. */
  bool reading;

  /** The block a read would continue from, after an open read or a lone block read. */
  uint32_t next_block;
};

/**
 * Initialize a card at a bus width and clock.
 *
 * @param card the card
 * @param host the host the card sits on, started by the caller
 * @param width the bus width, 1 or 4
 * @param clock_khz the highest clock in kHz, held to 25000 for a card without high speed
 * @return 0 or a negative tsd_error
 */
int tsd_card_init(struct tsd_card *card, struct tsd_host *host, uint8_t width, uint32_t clock_khz);

/**
 * Read blocks.
 *
 * On a host that offers continue_read, the read stays open for the next read to continue. A write, an erase, a read
 * elsewhere or tsd_card_sync ends it.
 *
 * @param card the card
 * @param buffer receives count blocks, aligned as the host requires
 * @param block the first block
 * @param count the number of blocks
 * @return 0 or a negative tsd_error
 */
int tsd_card_read(struct tsd_card *card, void *buffer, uint32_t block, uint32_t count);

/**
 * Write blocks.
 *
 * @param card the card
 * @param buffer count blocks to write, aligned as the host requires
 * @param block the first block
 * @param count the number of blocks
 * @return 0 or a negative tsd_error
 */
int tsd_card_write(struct tsd_card *card, const void *buffer, uint32_t block, uint32_t count);

/**
 * Erase blocks.
 *
 * @param card the card
 * @param block the first block
 * @param count the number of blocks
 * @return 0 or a negative tsd_error
 */
int tsd_card_erase(struct tsd_card *card, uint32_t block, uint32_t count);

/**
 * End any open read.
 *
 * @param card the card
 * @return 0 or a negative tsd_error
 */
int tsd_card_sync(struct tsd_card *card);
