#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tinysd/errors.h>
#include <tinysd/host.h>

struct tsd_spi;

/**
 * How the host reaches the card's SPI bus, passed to tsd_spi_init.
 */
struct tsd_spi_config {
  /** Send bytes, ignoring what comes back. Return 0 or a negative tsd_error. */
  int (*write)(struct tsd_spi *spi, const uint8_t *buffer, uint32_t len);

  /** Receive bytes while sending 0xff. Return 0 or a negative tsd_error. */
  int (*read)(struct tsd_spi *spi, uint8_t *buffer, uint32_t len);

  /** Drive chip select low to select the card, or high to release it. */
  void (*select)(struct tsd_spi *spi, bool selected);

  /** Run the clock at the highest rate at or below a limit in kHz. Return 0 or a negative tsd_error. */
  int (*set_clock)(struct tsd_spi *spi, uint32_t clock_khz);
};

/** Macro to cast a generic host instance to an SPI host instance. */
#define TSD_SPI(host) ((struct tsd_spi *)(host))

/**
 * A card in SPI mode on an SPI bus.
 */
struct tsd_spi {
  /** The host interface, which card drivers take. */
  struct tsd_host host;

  /** The bus's configuration. */
  struct tsd_spi_config config;
};

/**
 * Initialize an SPI host.
 *
 * @param spi the host
 * @param config the bus's configuration
 * @return 0 or a negative tsd_error
 */
int tsd_spi_init(struct tsd_spi *spi, struct tsd_spi_config config);
