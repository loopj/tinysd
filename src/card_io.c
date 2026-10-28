#include <stddef.h>
#include <stdint.h>

#include <tinysd/bits.h>
#include <tinysd/commands.h>
#include <tinysd/config.h>
#include <tinysd/delay.h>

#include <tinysd/card.h>

// next_block when no read has come before
#define NO_BLOCK UINT32_MAX

// Longest wait for the card to finish a write, in milliseconds
#define WRITE_TIMEOUT_MS 10000

// Time the card may take to erase each block, in milliseconds, from the SD specification
#define ERASE_TIMEOUT_PER_BLOCK_MS 250

// Delay between CMD13 tries while the card is busy, in microseconds
#define READY_DELAY_US 125

// Status errors that mark a failed write
#define WRITE_ERRORS            \
  (TSD_STATUS_ADDRESS_ERROR |   \
   TSD_STATUS_BLOCK_LEN_ERROR | \
   TSD_STATUS_WP_VIOLATION |    \
   TSD_STATUS_CARD_ECC_FAILED | \
   TSD_STATUS_CC_ERROR |        \
   TSD_STATUS_ERROR)

// Status errors that mark a failed erase
#define ERASE_ERRORS            \
  (WRITE_ERRORS |               \
   TSD_STATUS_ERASE_SEQ_ERROR | \
   TSD_STATUS_ERASE_PARAM |     \
   TSD_STATUS_WP_ERASE_SKIP)

// Error bits of the R2 that CMD13 returns in SPI mode
#define SPI_STATUS_ERRORS          \
  (TSD_SPI_R2_CARD_IS_LOCKED |     \
   TSD_SPI_R2_WP_ERASE_SKIP |      \
   TSD_SPI_R2_ERROR |              \
   TSD_SPI_R2_CC_ERROR |           \
   TSD_SPI_R2_CARD_ECC_FAILED |    \
   TSD_SPI_R2_WP_VIOLATION |       \
   TSD_SPI_R2_ERASE_PARAM |        \
   TSD_SPI_R2_OUT_OF_RANGE)

// Address a block by number on a high capacity card and by byte offset on the others
static uint32_t block_address(struct tsd_card *card, uint32_t block)
{
  return card->high_capacity ? block : block * TSD_CARD_BLOCK_SIZE;
}

// Move blocks with a single block command, or a multiple block command that CMD12 ends
static int transfer_blocks(struct tsd_card *card, void *buffer, uint32_t block, uint32_t count, bool write)
{
  // Refuse an empty transfer
  if (count == 0)
    return TSD_ERR_PARAM;

  // Pick the command for one block or more
  uint8_t index;
  if (count == 1) {
    index = write ? TSD_CMD_WRITE_BLOCK : TSD_CMD_READ_SINGLE_BLOCK;
  } else {
    index = write ? TSD_CMD_WRITE_MULTIPLE_BLOCK : TSD_CMD_READ_MULTIPLE_BLOCK;
  }

  // Run the command
  struct tsd_command command = {
    .index         = index,
    .argument      = block_address(card, block),
    .response_type = TSD_RESPONSE_R1,
  };
  struct tsd_data data = {
    .buffer     = buffer,
    .block_size = TSD_CARD_BLOCK_SIZE,
    .blocks     = count,
    .write      = write,
  };
  int ret = tsd_host_request(card->host, &command, &data);

  // End a multiple block transfer with CMD12, even after a failure, unless an SPI host already ended it
  if (count > 1 && !card->host->api->spi) {
    // Wait out a busy only after a write, since a read leaves the card nothing to program
    enum tsd_response stop_type = write ? TSD_RESPONSE_R1B : TSD_RESPONSE_R1;

    // Send CMD12, keeping the first error
    uint32_t status = 0;
    int stop_ret    = tsd_host_send_command(card->host, TSD_CMD_STOP_TRANSMISSION, 0, stop_type, &status);
    if (ret == 0)
      ret = stop_ret;

    // Report an error the card flagged while it took the blocks of a write
    if (ret == 0 && write && (status & WRITE_ERRORS))
      ret = TSD_ERR_CARD;
  }

  return ret;
}

// Poll the card status until it is ready for data
static int wait_ready(struct tsd_card *card, uint32_t errors, uint64_t timeout_us)
{
  uint32_t address = card->address << TSD_RCA_SHIFT;
  for (uint64_t waited_us = 0; waited_us < timeout_us; waited_us += READY_DELAY_US) {
    // Check whether the card still holds its data line low
    int ret = tsd_host_card_busy(card->host);
    if (ret < 0)
      return ret;

    // Ask a card that has let go for its status
    if (ret == 0) {
      // Read the status as an R2 in SPI mode and an R1 otherwise
      bool spi               = card->host->api->spi;
      enum tsd_response type = spi ? TSD_RESPONSE_R2 : TSD_RESPONSE_R1;
      uint32_t status        = 0;
      ret                    = tsd_host_send_command(card->host, TSD_CMD_SEND_STATUS, address, type, &status);
      if (ret < 0)
        return ret;

      // Report an error bit the card set
      if (status & (spi ? SPI_STATUS_ERRORS : errors))
        return TSD_ERR_CARD;

      // Finish in SPI mode, which has no state, or once the card is ready for data in the transfer state
      uint32_t state = TSD_FIELD_GET(TSD_STATUS_CURRENT_STATE, status);
      if (spi || ((status & TSD_STATUS_READY_FOR_DATA) && state == TSD_STATUS_STATE_TRAN))
        return 0;
    }

    // Wait before asking again
    tsd_delay_us(READY_DELAY_US);
  }

  return TSD_ERR_TIMEOUT;
}

// End an open read with CMD12, which has no busy after a read, leaving the card ready for another command
static int stop_read(struct tsd_card *card)
{
  if (!card->reading)
    return 0;

  // Mark the read closed, then close it
  card->reading = false;
  return tsd_host_send_command(card->host, TSD_CMD_STOP_TRANSMISSION, 0, TSD_RESPONSE_R1, NULL);
}

// Open a read at block with CMD18 and no count, and take count blocks from it
static int open_read(struct tsd_card *card, void *buffer, uint32_t block, uint32_t count)
{
  // Open the read, which the host leaves running
  struct tsd_command command = {
    .index         = TSD_CMD_READ_MULTIPLE_BLOCK,
    .argument      = block_address(card, block),
    .response_type = TSD_RESPONSE_R1,
  };
  struct tsd_data data = {
    .block_size = TSD_CARD_BLOCK_SIZE,
    .blocks     = 0,
  };
  int ret = tsd_host_request(card->host, &command, &data);
  if (ret < 0)
    return ret;
  card->reading = true;

  // Take the blocks asked for
  return tsd_host_continue_read(card->host, buffer, count);
}

int tsd_card_read(struct tsd_card *card, void *buffer, uint32_t block, uint32_t count)
{
  // Refuse an empty read or one past the end of the card
  if (count == 0 || block >= card->blocks || count > card->blocks - block)
    return TSD_ERR_PARAM;

  // Take more blocks from the open read when this one starts where it stands
  int ret;
  if (card->reading && block == card->next_block) {
    ret = tsd_host_continue_read(card->host, buffer, count);
    if (ret < 0) {
      stop_read(card);
      return ret;
    }
    card->next_block += count;
    return 0;
  }

  // Note whether a lone block read just came before this one
  bool follows = !card->reading && block == card->next_block;

  // End any other open read
  ret = stop_read(card);
  if (ret < 0) {
    card->next_block = NO_BLOCK;
    return ret;
  }

  // Open a read for more than one block, or for a block that follows a lone one, on a host that can stream
  bool stream = TSD_USE_OPEN_READS && card->host->api->continue_read && (count > 1 || follows);
  if (stream) {
    ret = open_read(card, buffer, block, count);
    if (ret < 0)
      stop_read(card);
  } else {
    ret = transfer_blocks(card, buffer, block, count, false);
  }

  // Note where the next read would follow on
  card->next_block = ret < 0 ? NO_BLOCK : block + count;
  return ret;
}

int tsd_card_write(struct tsd_card *card, const void *buffer, uint32_t block, uint32_t count)
{
  // Refuse a write past the end of the card
  if (block >= card->blocks || count > card->blocks - block)
    return TSD_ERR_PARAM;

  // Forget where a read would follow on, since the write comes between
  card->next_block = NO_BLOCK;

  // End an open read
  int ret = stop_read(card);
  if (ret < 0)
    return ret;

  // Write the blocks, then wait for the card to finish programming them
  ret = transfer_blocks(card, (void *)buffer, block, count, true);
  if (ret < 0)
    return ret;

  return wait_ready(card, WRITE_ERRORS, WRITE_TIMEOUT_MS * 1000);
}

int tsd_card_erase(struct tsd_card *card, uint32_t block, uint32_t count)
{
  // Refuse an empty erase or one past the end of the card
  if (count == 0 || block >= card->blocks || count > card->blocks - block)
    return TSD_ERR_PARAM;

  // Forget where a read would follow on, since the erase comes between
  card->next_block = NO_BLOCK;

  // End an open read
  int ret = stop_read(card);
  if (ret < 0)
    return ret;

  // Set the first block
  uint32_t first = block_address(card, block);
  ret            = tsd_host_send_command(card->host, TSD_CMD_ERASE_WR_BLK_START, first, TSD_RESPONSE_R1, NULL);
  if (ret < 0)
    return ret;

  // Set the last block
  uint32_t last = block_address(card, block + count - 1);
  ret           = tsd_host_send_command(card->host, TSD_CMD_ERASE_WR_BLK_END, last, TSD_RESPONSE_R1, NULL);
  if (ret < 0)
    return ret;

  // Erase them, taking an R1 since a long erase can outlast the host's busy wait
  ret = tsd_host_send_command(card->host, TSD_CMD_ERASE, 0, TSD_RESPONSE_R1, NULL);
  if (ret < 0)
    return ret;

  // Wait for the card to finish
  return wait_ready(card, ERASE_ERRORS, (uint64_t)count * ERASE_TIMEOUT_PER_BLOCK_MS * 1000);
}

int tsd_card_sync(struct tsd_card *card)
{
  // Forget where a read would follow on, and end an open read
  card->next_block = NO_BLOCK;
  return stop_read(card);
}
