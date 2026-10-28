#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Response format of a command, named as in the SD and SDIO specifications.
 */
enum tsd_response {
  /** No response. */
  TSD_RESPONSE_NONE,

  /** 48 bits with the card status, CRC and index checked. */
  TSD_RESPONSE_R1,

  /** R1, then busy on DAT0 until the card lets go. */
  TSD_RESPONSE_R1B,

  /** 136 bits with the CID or CSD, CRC checked. */
  TSD_RESPONSE_R2,

  /** 48 bits with the OCR from ACMD41, no CRC or index. */
  TSD_RESPONSE_R3,

  /** 48 bits with the OCR from CMD5, no CRC or index. */
  TSD_RESPONSE_R4,

  /** 48 bits with the reply to CMD52 or CMD53, CRC and index checked. */
  TSD_RESPONSE_R5,

  /** 48 bits with the relative address from CMD3, CRC and index checked. */
  TSD_RESPONSE_R6,

  /** 48 bits with the interface condition from CMD8, CRC and index checked. */
  TSD_RESPONSE_R7,
};

/**
 * One command, which receives its response.
 */
struct tsd_command {
  /** Command index. */
  uint8_t index;

  /** Argument. */
  uint32_t argument;

  /** Response format. */
  enum tsd_response response_type;

  /** Receives the response, the payload of a 48-bit one in response[0] or bits 127 to 8 of an R2 across all four. */
  uint32_t response[4];
};

/**
 * The data phase of a command, which the host completes before the command returns.
 */
struct tsd_data {
  /** The data, aligned as the host requires. */
  void *buffer;

  /** Bytes in each block. */
  uint16_t block_size;

  /** Blocks to move, or 0 to open a read that continue_read takes from until CMD12. */
  uint32_t blocks;

  /** The data moves to the card. */
  bool write;

  /** Receives the bytes the card moved, a read block that failed its CRC included. */
  uint32_t done;
};

/** Macro to cast a concrete host instance to a generic host instance. */
#define TSD_HOST(host) ((struct tsd_host *)(host))

/**
 * Base of every host, embedded first in each concrete host.
 */
struct tsd_host {
  /** The host's operations. */
  const struct tsd_host_api *api;
};

/**
 * Operations every host implements.
 */
struct tsd_host_api {
  /**
   * Send a command and run its whole data phase before returning, or only open a read when data has 0 blocks.
   *
   * @param host the host
   * @param command the command, which receives its response
   * @param data the data phase, which receives the bytes the card moved, or NULL for a command without one
   * @return 0 or a negative tsd_error, TSD_ERR_TIMEOUT when no response comes
   */
  int (*request)(struct tsd_host *host, struct tsd_command *command, struct tsd_data *data);

  /**
   * Run the clock at the highest rate at or below a limit.
   *
   * @param host the host
   * @param clock_khz the limit in kHz, 0 to stop the clock
   * @return 0 or a negative tsd_error
   */
  int (*set_clock)(struct tsd_host *host, uint32_t clock_khz);

  /**
   * Select the bus width.
   *
   * @param host the host
   * @param width the bus width, 1 or 4
   */
  void (*set_bus_width)(struct tsd_host *host, uint8_t width);

  /**
   * Report whether the card holds its data line low while it programs or erases.
   *
   * @param host the host
   * @return 1 while the card is busy, 0 once it is free, or a negative tsd_error
   */
  int (*card_busy)(struct tsd_host *host);

  /**
   * Enable or disable the card interrupt. Optional, and NULL for a host without a card interrupt.
   *
   * @param host the host
   * @param on true to enable it
   */
  void (*set_card_interrupt)(struct tsd_host *host, bool on);

  /**
   * Take the next blocks of a read that request opened, with no command. Optional, and NULL for a host that cannot
   * pause the card between blocks.
   *
   * @param host the host
   * @param buffer receives count blocks, aligned as the host requires
   * @param count the number of blocks
   * @return 0 or a negative tsd_error
   */
  int (*continue_read)(struct tsd_host *host, void *buffer, uint32_t count);

  /** The host runs the card in SPI mode. */
  bool spi;
};

/**
 * Send a command and run its data phase.
 *
 * @param host the host
 * @param command the command, which receives its response
 * @param data the data phase, which receives the bytes moved, or NULL for none
 * @return 0 or a negative tsd_error, TSD_ERR_TIMEOUT when no response comes
 */
static inline int tsd_host_request(struct tsd_host *host, struct tsd_command *command, struct tsd_data *data)
{
  return host->api->request(host, command, data);
}

/**
 * Send a command with no data phase and take the first word of its response.
 *
 * @param host the host
 * @param index the command index
 * @param argument the argument
 * @param response_type the response format
 * @param response receives the first response word, or NULL to drop it
 * @return 0 or a negative tsd_error, TSD_ERR_TIMEOUT when no response comes
 */
static inline int tsd_host_send_command(struct tsd_host *host, uint8_t index, uint32_t argument,
                                        enum tsd_response response_type, uint32_t *response)
{
  // Run the command on the host
  struct tsd_command command = {
    .index         = index,
    .argument      = argument,
    .response_type = response_type,
  };
  int ret = tsd_host_request(host, &command, NULL);

  // Return the response word when asked for, even after an error, since it may say why
  if (response)
    *response = command.response[0];

  return ret;
}

/**
 * Run the clock at the highest rate at or below a limit.
 *
 * @param host the host
 * @param clock_khz the limit in kHz, 0 to stop the clock
 * @return 0 or a negative tsd_error
 */
static inline int tsd_host_set_clock(struct tsd_host *host, uint32_t clock_khz)
{
  return host->api->set_clock(host, clock_khz);
}

/**
 * Select the bus width.
 *
 * @param host the host
 * @param width the bus width, 1 or 4
 */
static inline void tsd_host_set_bus_width(struct tsd_host *host, uint8_t width)
{
  host->api->set_bus_width(host, width);
}

/**
 * Report whether the card holds its data line low while it programs or erases.
 *
 * @param host the host
 * @return 1 while the card is busy, 0 once it is free, or a negative tsd_error
 */
static inline int tsd_host_card_busy(struct tsd_host *host)
{
  return host->api->card_busy(host);
}

/**
 * Enable or disable the card interrupt, on a host that has one.
 *
 * @param host the host
 * @param on true to enable it
 */
static inline void tsd_host_set_card_interrupt(struct tsd_host *host, bool on)
{
  // Do nothing on a host without a card interrupt
  if (host->api->set_card_interrupt)
    host->api->set_card_interrupt(host, on);
}

/**
 * Take the next blocks of an open read, on a host that offers continue_read.
 *
 * @param host the host
 * @param buffer receives count blocks
 * @param count the number of blocks
 * @return 0 or a negative tsd_error
 */
static inline int tsd_host_continue_read(struct tsd_host *host, void *buffer, uint32_t count)
{
  return host->api->continue_read(host, buffer, count);
}
