#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tinysd/bits.h>
#include <tinysd/errors.h>
#include <tinysd/host.h>

/** Function 0, which holds the CCCR and the CIS. */
#define TSD_SDIO_COMMON_FUNCTION             0

/** R4 fields in CMD5's reply. */
#define TSD_SDIO_R4_FUNCTIONS                TSD_BITS(30, 28)
#define TSD_SDIO_R4_MEMORY_PRESENT           TSD_BIT(27)

/** R5 fields in CMD52 and CMD53 replies, from Table 5-1 of the SDIO specification. */
#define TSD_SDIO_R5_COM_CRC_ERROR            TSD_BIT(15)
#define TSD_SDIO_R5_ILLEGAL_COMMAND          TSD_BIT(14)
#define TSD_SDIO_R5_IO_CURRENT_STATE         TSD_BITS(13, 12)
#define TSD_SDIO_R5_ERROR                    TSD_BIT(11)
#define TSD_SDIO_R5_FUNCTION_NUMBER          TSD_BIT(9)
#define TSD_SDIO_R5_OUT_OF_RANGE             TSD_BIT(8)
#define TSD_SDIO_R5_DATA                     TSD_BITS(7, 0)

/** CMD52 and CMD53 argument fields, with data for CMD52 and block mode, increment and count for CMD53. */
#define TSD_SDIO_ARG_WRITE                   TSD_BIT(31)
#define TSD_SDIO_ARG_FUNCTION                TSD_BITS(30, 28)
#define TSD_SDIO_ARG_BLOCK_MODE              TSD_BIT(27)
#define TSD_SDIO_ARG_INCREMENT               TSD_BIT(26)
#define TSD_SDIO_ARG_ADDRESS                 TSD_BITS(25, 9)
#define TSD_SDIO_ARG_COUNT                   TSD_BITS(8, 0)
#define TSD_SDIO_ARG_DATA                    TSD_BITS(7, 0)

/** CCCR register offsets in function 0, from Table 6-1 of the SDIO specification. */
#define TSD_SDIO_CCCR_SDIO_REVISION          0x00
#define TSD_SDIO_CCCR_SD_REVISION            0x01
#define TSD_SDIO_CCCR_IO_ENABLE              0x02
#define TSD_SDIO_CCCR_IO_READY               0x03
#define TSD_SDIO_CCCR_INT_ENABLE             0x04
#define TSD_SDIO_CCCR_INT_PENDING            0x05
#define TSD_SDIO_CCCR_IO_ABORT               0x06
#define TSD_SDIO_CCCR_BUS_INTERFACE_CONTROL  0x07
#define TSD_SDIO_CCCR_CARD_CAPABILITY        0x08
#define TSD_SDIO_CCCR_COMMON_CIS_POINTER     0x09
#define TSD_SDIO_CCCR_BUS_SUSPEND            0x0c
#define TSD_SDIO_CCCR_FUNCTION_SELECT        0x0d
#define TSD_SDIO_CCCR_EXEC_FLAGS             0x0e
#define TSD_SDIO_CCCR_READY_FLAGS            0x0f
#define TSD_SDIO_CCCR_FN0_BLOCK_SIZE         0x10
#define TSD_SDIO_CCCR_POWER_CONTROL          0x12
#define TSD_SDIO_CCCR_BUS_SPEED_SELECT       0x13
#define TSD_SDIO_CCCR_UHS_I_SUPPORT          0x14
#define TSD_SDIO_CCCR_DRIVER_STRENGTH        0x15
#define TSD_SDIO_CCCR_INTERRUPT_EXTENSION    0x16

/** CCCR revision register field. */
#define TSD_SDIO_REVISION_CCCR               TSD_BITS(3, 0)

/** CCCR revision that adds the bus speed select register. */
#define TSD_SDIO_REVISION_CCCR_1_20          2

/** Interrupt enable register fields, with one enable per function above the master enable. */
#define TSD_SDIO_INT_ENABLE_FUNCTIONS        TSD_BITS(7, 1)
#define TSD_SDIO_INT_ENABLE_MASTER           TSD_BIT(0)

/** I/O abort register fields. */
#define TSD_SDIO_IO_ABORT_RES                TSD_BIT(3)
#define TSD_SDIO_IO_ABORT_FUNCTION           TSD_BITS(2, 0)

/** Bus interface control register fields. */
#define TSD_SDIO_BUS_CD_DISABLE              TSD_BIT(7)
#define TSD_SDIO_BUS_SCSI                    TSD_BIT(6)
#define TSD_SDIO_BUS_ECSI                    TSD_BIT(5)
#define TSD_SDIO_BUS_WIDTH                   TSD_BITS(1, 0)

/** Values of TSD_SDIO_BUS_WIDTH. */
#define TSD_SDIO_BUS_WIDTH_1BIT              0
#define TSD_SDIO_BUS_WIDTH_4BIT              2
#define TSD_SDIO_BUS_WIDTH_8BIT              3

/** Card capability register fields. */
#define TSD_SDIO_CAPABILITY_4BLS             TSD_BIT(7)
#define TSD_SDIO_CAPABILITY_LSC              TSD_BIT(6)

/** Bus speed select register fields. */
#define TSD_SDIO_SPEED_EHS                   TSD_BIT(1)
#define TSD_SDIO_SPEED_SHS                   TSD_BIT(0)

/** CIS tuple codes, from Table 16-2 of the SDIO specification. */
#define TSD_SDIO_CISTPL_NULL                 0x00
#define TSD_SDIO_CISTPL_MANFID               0x20
#define TSD_SDIO_CISTPL_FUNCE                0x22
#define TSD_SDIO_CISTPL_END                  0xff

/** Address of a function's basic registers in function 0. */
#define TSD_SDIO_FBR_BASE(function)          ((uint32_t)(function) * 0x100)

/** FBR register offsets within a function's basic registers, from Table 6-3 of the SDIO specification. */
#define TSD_SDIO_FBR_INTERFACE_CODE          0x00
#define TSD_SDIO_FBR_EXTENDED_INTERFACE_CODE 0x01
#define TSD_SDIO_FBR_POWER_SELECTION         0x02
#define TSD_SDIO_FBR_CIS_POINTER             0x09
#define TSD_SDIO_FBR_CSA_POINTER             0x0c
#define TSD_SDIO_FBR_CSA_DATA_WINDOW         0x0f
#define TSD_SDIO_FBR_BLOCK_SIZE              0x10

/**
 * Fields from an SDIO card's common CIS.
 */
struct tsd_sdio_cis {
  /** Manufacturer id, or 0 when the CIS has none. */
  uint16_t manufacturer_id;

  /** Card id, or 0 when the CIS has none. */
  uint16_t card_id;

  /** Fastest default speed clock in kHz, or 0 when the CIS has none. */
  uint32_t max_clock_khz;
};

/**
 * An SDIO card.
 */
struct tsd_sdio {
  /** The host the card sits on, which the caller started. */
  struct tsd_host *host;

  /** Relative card address, 0 until the card has one. */
  uint32_t address;

  /** Fields from the card's common CIS. */
  struct tsd_sdio_cis cis;

  /** Number of I/O functions, not counting function 0. */
  uint8_t functions;

  /** Each function's largest block size from the CIS, or 0 when it has none. */
  uint16_t max_block_size[8];
};

/**
 * Read a card's ids, then reset its I/O, for a card another driver may own.
 *
 * @param host the host the card sits on, started by the caller, who also releases it
 * @param cis receives the fields from the card's common CIS
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_identify(struct tsd_host *host, struct tsd_sdio_cis *cis);

/**
 * Initialize a card at a bus width and clock.
 *
 * @param card the card
 * @param host the host the card sits on, started by the caller
 * @param width the bus width the host can run, 1 or 4
 * @param clock_khz the highest clock in kHz, held lower when the card cannot run it
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_init(struct tsd_sdio *card, struct tsd_host *host, uint8_t width, uint32_t clock_khz);

/**
 * Enable a function and wait for it to become ready.
 *
 * @param card the card
 * @param function the function, 0 to 7
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_enable_function(struct tsd_sdio *card, uint8_t function);

/**
 * Wait for a function to report ready.
 *
 * @param card the card
 * @param function the function, 0 to 7
 * @param timeout_ms the longest wait in milliseconds
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_wait_ready(struct tsd_sdio *card, uint8_t function, uint32_t timeout_ms);

/**
 * Set a function's block size.
 *
 * @param card the card
 * @param function the function, 0 to 7
 * @param block_size the block size in bytes
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_set_block_size(struct tsd_sdio *card, uint8_t function, uint16_t block_size);

/**
 * Let a function raise the card interrupt, with the master enable.
 *
 * @param card the card
 * @param function the function, 0 to 7
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_enable_interrupt(struct tsd_sdio *card, uint8_t function);

/**
 * Enable or disable the card interrupt. Each command masks it while it runs.
 *
 * @param card the card
 * @param on true to enable it
 */
void tsd_sdio_set_card_interrupt(struct tsd_sdio *card, bool on);

/**
 * Read one byte with CMD52.
 *
 * @param card the card
 * @param function the function, 0 to 7
 * @param address the register address, up to 17 bits
 * @param value receives the byte
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_read_byte(struct tsd_sdio *card, uint8_t function, uint32_t address, uint8_t *value);

/**
 * Write one byte with CMD52.
 *
 * @param card the card
 * @param function the function, 0 to 7
 * @param address the register address, up to 17 bits
 * @param value the byte
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_write_byte(struct tsd_sdio *card, uint8_t function, uint32_t address, uint8_t value);

/**
 * Read one byte from function 0, which holds the CCCR, the FBRs and the CIS.
 *
 * @param card the card
 * @param address the register address, up to 17 bits
 * @param value receives the byte
 * @return 0 or a negative tsd_error
 */
static inline int tsd_sdio_read_f0(struct tsd_sdio *card, uint32_t address, uint8_t *value)
{
  return tsd_sdio_read_byte(card, TSD_SDIO_COMMON_FUNCTION, address, value);
}

/**
 * Write one byte to function 0, which holds the CCCR, the FBRs and the CIS.
 *
 * @param card the card
 * @param address the register address, up to 17 bits
 * @param value the byte
 * @return 0 or a negative tsd_error
 */
static inline int tsd_sdio_write_f0(struct tsd_sdio *card, uint32_t address, uint8_t value)
{
  return tsd_sdio_write_byte(card, TSD_SDIO_COMMON_FUNCTION, address, value);
}

/**
 * Transfer a buffer with CMD53 from an incrementing address, whole blocks in block mode and the rest in byte mode.
 *
 * @param card the card
 * @param function the function, 0 to 7
 * @param address the register address, up to 17 bits
 * @param write true to write to the card
 * @param buffer the data, word aligned
 * @param len the length in bytes, any length that stays within the 17-bit address range
 * @param block_size the function's block size, or 0 to move everything in byte mode
 * @param done receives the bytes the card moved, or NULL
 * @return 0 or a negative tsd_error
 */
int tsd_sdio_transfer(struct tsd_sdio *card, uint8_t function, uint32_t address, bool write, void *buffer, uint32_t len,
                      uint16_t block_size, uint32_t *done);
