#pragma once

#include <tinysd/bits.h>

/** OCR fields in ACMD41 and CMD5, with the 2.7 V to 3.6 V voltage window. */
#define TSD_OCR_READY                  TSD_BIT(31)
#define TSD_OCR_CCS                    TSD_BIT(30)
#define TSD_OCR_VOLTAGE_WINDOW         TSD_BITS(23, 15)

/** Position of the relative card address in CMD3's reply and in addressed commands. */
#define TSD_RCA_SHIFT                  16

/** Card status fields in an R1, from Table 4-42 of the SD specification. */
#define TSD_STATUS_OUT_OF_RANGE        TSD_BIT(31)
#define TSD_STATUS_ADDRESS_ERROR       TSD_BIT(30)
#define TSD_STATUS_BLOCK_LEN_ERROR     TSD_BIT(29)
#define TSD_STATUS_ERASE_SEQ_ERROR     TSD_BIT(28)
#define TSD_STATUS_ERASE_PARAM         TSD_BIT(27)
#define TSD_STATUS_WP_VIOLATION        TSD_BIT(26)
#define TSD_STATUS_CARD_IS_LOCKED      TSD_BIT(25)
#define TSD_STATUS_LOCK_UNLOCK_FAILED  TSD_BIT(24)
#define TSD_STATUS_COM_CRC_ERROR       TSD_BIT(23)
#define TSD_STATUS_ILLEGAL_COMMAND     TSD_BIT(22)
#define TSD_STATUS_CARD_ECC_FAILED     TSD_BIT(21)
#define TSD_STATUS_CC_ERROR            TSD_BIT(20)
#define TSD_STATUS_ERROR               TSD_BIT(19)
#define TSD_STATUS_CSD_OVERWRITE       TSD_BIT(16)
#define TSD_STATUS_WP_ERASE_SKIP       TSD_BIT(15)
#define TSD_STATUS_CARD_ECC_DISABLED   TSD_BIT(14)
#define TSD_STATUS_ERASE_RESET         TSD_BIT(13)
#define TSD_STATUS_CURRENT_STATE       TSD_BITS(12, 9)
#define TSD_STATUS_READY_FOR_DATA      TSD_BIT(8)
#define TSD_STATUS_FX_EVENT            TSD_BIT(6)
#define TSD_STATUS_APP_CMD             TSD_BIT(5)
#define TSD_STATUS_AKE_SEQ_ERROR       TSD_BIT(3)

/** R1 fields in SPI mode, from Figure 7-9 of the SD specification. */
#define TSD_SPI_R1_IN_IDLE_STATE       TSD_BIT(0)
#define TSD_SPI_R1_ERASE_RESET         TSD_BIT(1)
#define TSD_SPI_R1_ILLEGAL_COMMAND     TSD_BIT(2)
#define TSD_SPI_R1_COM_CRC_ERROR       TSD_BIT(3)
#define TSD_SPI_R1_ERASE_SEQ_ERROR     TSD_BIT(4)
#define TSD_SPI_R1_ADDRESS_ERROR       TSD_BIT(5)
#define TSD_SPI_R1_PARAMETER_ERROR     TSD_BIT(6)

/** R2 fields in SPI mode, with the R1 in bits 7 to 0, from Figure 7-10 of the SD specification. */
#define TSD_SPI_R2_CARD_IS_LOCKED      TSD_BIT(8)
#define TSD_SPI_R2_WP_ERASE_SKIP       TSD_BIT(9)
#define TSD_SPI_R2_ERROR               TSD_BIT(10)
#define TSD_SPI_R2_CC_ERROR            TSD_BIT(11)
#define TSD_SPI_R2_CARD_ECC_FAILED     TSD_BIT(12)
#define TSD_SPI_R2_WP_VIOLATION        TSD_BIT(13)
#define TSD_SPI_R2_ERASE_PARAM         TSD_BIT(14)
#define TSD_SPI_R2_OUT_OF_RANGE        TSD_BIT(15)

/** Tokens that start and stop data blocks in SPI mode, from Section 7.3.3.2 of the SD specification. */
#define TSD_SPI_TOKEN_START_BLOCK      0xfe
#define TSD_SPI_TOKEN_MULTI_WRITE      0xfc
#define TSD_SPI_TOKEN_STOP_TRAN        0xfd

/** Data response token in SPI mode and its status values, from Section 7.3.3.1 of the SD specification. */
#define TSD_SPI_DATA_RESPONSE_STATUS   TSD_BITS(3, 1)
#define TSD_SPI_DATA_ACCEPTED          2
#define TSD_SPI_DATA_CRC_ERROR         5
#define TSD_SPI_DATA_WRITE_ERROR       6

/** CMD6 high speed switch, and where its status reports the function. */
#define TSD_SWITCH_HIGH_SPEED          0x80fffff1u
#define TSD_SWITCH_STATUS_LEN          64
#define TSD_SWITCH_STATUS_GROUP1       16
#define TSD_SWITCH_STATUS_FUNCTION     TSD_BITS(3, 0)
#define TSD_SWITCH_FUNCTION_HIGH_SPEED 1

/** CMD8 fields, with the 2.7 V to 3.6 V voltage and the check pattern. */
#define TSD_IF_COND_VOLTAGE            TSD_BITS(11, 8)
#define TSD_IF_COND_PATTERN            TSD_BITS(7, 0)
#define TSD_IF_COND_VOLTAGE_2_7_3_6V   1
#define TSD_IF_COND_CHECK_PATTERN      0xaa

/** CSD structure, in response word 0, which holds CSD bits 127 to 96. */
#define TSD_CSD_STRUCTURE              TSD_BITS(31, 30)

/** CSD structure versions. */
#define TSD_CSD_VERSION_1              0
#define TSD_CSD_VERSION_2              1

/** CSD version 1 fields in response word 1, which holds CSD bits 95 to 64. */
#define TSD_CSD_V1_READ_BL_LEN         TSD_BITS(19, 16)
#define TSD_CSD_V1_C_SIZE_HIGH         TSD_BITS(9, 0)

/** CSD version 1 fields in response word 2, which holds CSD bits 63 to 32. */
#define TSD_CSD_V1_C_SIZE_LOW          TSD_BITS(31, 30)
#define TSD_CSD_V1_C_SIZE_MULT         TSD_BITS(17, 15)

/** CSD version 2 fields in response words 1 and 2. */
#define TSD_CSD_V2_C_SIZE_HIGH         TSD_BITS(5, 0)
#define TSD_CSD_V2_C_SIZE_LOW          TSD_BITS(31, 16)

/** ACMD6 argument for a 4-bit bus. */
#define TSD_ACMD6_BUS_WIDTH_4BIT       0x2u

/**
 * Current state values in the card status, from Table 4-42 of the SD specification.
 */
enum tsd_status_state {
  /** Idle State. */
  TSD_STATUS_STATE_IDLE = 0,

  /** Ready State. */
  TSD_STATUS_STATE_READY = 1,

  /** Identification State. */
  TSD_STATUS_STATE_IDENT = 2,

  /** Stand-by State. */
  TSD_STATUS_STATE_STBY = 3,

  /** Transfer State. */
  TSD_STATUS_STATE_TRAN = 4,

  /** Sending-data State. */
  TSD_STATUS_STATE_DATA = 5,

  /** Receive-data State. */
  TSD_STATUS_STATE_RCV = 6,

  /** Programming State. */
  TSD_STATUS_STATE_PRG = 7,

  /** Disconnect State. */
  TSD_STATUS_STATE_DIS = 8,
};

/**
 * Command indexes, named as in the SD specification.
 */
enum tsd_command_index {
  /** Reset the card to idle. */
  TSD_CMD_GO_IDLE_STATE = 0,

  /** Ask every card for its CID. */
  TSD_CMD_ALL_SEND_CID = 2,

  /** Ask the card for its relative address. */
  TSD_CMD_SEND_RELATIVE_ADDR = 3,

  /** Negotiate an SDIO card's operating voltage. */
  TSD_CMD_IO_SEND_OP_COND = 5,

  /** Check or switch a memory card's functions, such as high speed. */
  TSD_CMD_SWITCH_FUNC = 6,

  /** Select the card by its relative address. */
  TSD_CMD_SELECT_CARD = 7,

  /** Ask whether the card supports the host's voltage, which only version 2.00 or later cards answer. */
  TSD_CMD_SEND_IF_COND = 8,

  /** Ask the card for its CSD. */
  TSD_CMD_SEND_CSD = 9,

  /** End a multiple block transfer. */
  TSD_CMD_STOP_TRANSMISSION = 12,

  /** Ask the card for its status. */
  TSD_CMD_SEND_STATUS = 13,

  /** Set the block length of a standard capacity card. */
  TSD_CMD_SET_BLOCKLEN = 16,

  /** Read one block. */
  TSD_CMD_READ_SINGLE_BLOCK = 17,

  /** Read blocks until CMD12. */
  TSD_CMD_READ_MULTIPLE_BLOCK = 18,

  /** Write one block. */
  TSD_CMD_WRITE_BLOCK = 24,

  /** Write blocks until CMD12. */
  TSD_CMD_WRITE_MULTIPLE_BLOCK = 25,

  /** Set the first block to erase. */
  TSD_CMD_ERASE_WR_BLK_START = 32,

  /** Set the last block to erase. */
  TSD_CMD_ERASE_WR_BLK_END = 33,

  /** Erase the blocks CMD32 and CMD33 set. */
  TSD_CMD_ERASE = 38,

  /** Read or write one register byte of an SDIO card. */
  TSD_CMD_IO_RW_DIRECT = 52,

  /** Read or write a run of bytes or blocks of an SDIO card. */
  TSD_CMD_IO_RW_EXTENDED = 53,

  /** Mark the next command as an application command. */
  TSD_CMD_APP_CMD = 55,

  /** Read the card's OCR, in SPI mode only. */
  TSD_CMD_SPI_READ_OCR = 58,

  /** Turn CRC checking on or off, in SPI mode only. */
  TSD_CMD_SPI_CRC_ON_OFF = 59,
};

/**
 * Application command indexes, named as in the SD specification. Each follows CMD55.
 */
enum tsd_app_command_index {
  /** Set the bus width of a memory card. */
  TSD_ACMD_SET_BUS_WIDTH = 6,

  /** Negotiate a memory card's operating voltage and capacity. */
  TSD_ACMD_SD_SEND_OP_COND = 41,
};
