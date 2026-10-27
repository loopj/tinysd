#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tinysd/errors.h>
#include <tinysd/host.h>

struct tsd_sdhci;

/**
 * Where one controller lives and how it moves data, passed to tsd_sdhci_init.
 */
struct tsd_sdhci_config {
  /** Register base address, as the CPU sees it. */
  uintptr_t base;

  /** Map a buffer for DMA and return its bus address, or 0 to use the FIFO. NULL for no DMA. */
  uint32_t (*dma_map)(struct tsd_sdhci *sdhci, void *buffer, uint32_t len, bool read);

  /** Hand a buffer back to the CPU after a DMA transfer, for every buffer dma_map mapped. Set along with dma_map. */
  void (*dma_unmap)(struct tsd_sdhci *sdhci, void *buffer, uint32_t len, bool read);
};

/** Macro to cast a generic host instance to an SDHCI host instance. */
#define TSD_SDHCI(host) ((struct tsd_sdhci *)(host))

/**
 * An SD host controller, driven by polling.
 */
struct tsd_sdhci {
  /** The host interface, which card drivers take. */
  struct tsd_host host;

  /** The controller's configuration. */
  struct tsd_sdhci_config config;

  /** Base clock in kHz. */
  uint32_t base_clock_khz;

  /** Host controller specification version, from the version register. */
  uint8_t version;

  /** The card interrupt is wanted, outside the commands that mask it. */
  bool card_interrupt;
};

/**
 * Initialize an SDHCI host.
 *
 * @param sdhci the controller
 * @param config the controller's configuration
 * @return 0 or a negative tsd_error
 */
int tsd_sdhci_init(struct tsd_sdhci *sdhci, struct tsd_sdhci_config config);
