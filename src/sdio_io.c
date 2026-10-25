#include <stddef.h>

#include <tinysd/bits.h>
#include <tinysd/commands.h>
#include <tinysd/delay.h>

#include <tinysd/sdio.h>

// Delay between tries while a function becomes ready, in microseconds
#define READY_DELAY_US 10000

// Longest wait for a function just enabled to report ready, in milliseconds
#define ENABLE_TIMEOUT_MS 1000

// Largest transfer in byte mode, encoded as a count of 0
#define BYTE_MODE_MAX 512

// Most blocks one block mode transfer moves, since a count of 0 means an endless transfer
#define BLOCK_MODE_MAX 511

// Highest register address the argument's 17-bit field holds
#define ADDRESS_MAX 0x1ffffu

// Highest function number the argument's 3-bit field holds
#define FUNCTION_MAX 7

// Turn the error flags of an R5 into a tsd_error
static int check_response(uint32_t response)
{
  // Report a general error first, then a bad function or argument
  if (response & TSD_SDIO_R5_ERROR)
    return TSD_ERR_CARD;
  if (response & (TSD_SDIO_R5_FUNCTION_NUMBER | TSD_SDIO_R5_OUT_OF_RANGE))
    return TSD_ERR_PARAM;
  return 0;
}

// Run one CMD53, in block mode when block_size is set and in byte mode otherwise, with done the bytes moved
static int run_extended(struct tsd_sdio *card, uint8_t function, uint32_t address, bool write, uint8_t *buffer,
                        uint32_t len, uint16_t block_size, uint32_t *done)
{
  // Build the argument, counting blocks in block mode and bytes in byte mode, 512 as 0
  uint32_t count    = block_size ? len / block_size : (len == BYTE_MODE_MAX ? 0 : len);
  uint32_t argument = TSD_FIELD_PREP(TSD_SDIO_ARG_FUNCTION, function);
  argument |= TSD_FIELD_PREP(TSD_SDIO_ARG_ADDRESS, address);
  argument |= TSD_FIELD_PREP(TSD_SDIO_ARG_COUNT, count);
  argument |= TSD_SDIO_ARG_INCREMENT;
  if (write)
    argument |= TSD_SDIO_ARG_WRITE;
  if (block_size)
    argument |= TSD_SDIO_ARG_BLOCK_MODE;

  // Run CMD53, byte mode as one block of the whole length
  struct tsd_command command = {
    .index         = TSD_CMD_IO_RW_EXTENDED,
    .argument      = argument,
    .response_type = TSD_RESPONSE_R5,
  };
  struct tsd_data data = {
    .buffer     = buffer,
    .block_size = block_size ? block_size : (uint16_t)len,
    .blocks     = block_size ? len / block_size : 1,
    .write      = write,
  };
  int ret = tsd_host_request(card->host, &command, &data);
  *done   = data.done;
  if (ret < 0)
    return ret;

  // Report an error the card flagged
  return check_response(command.response[0]);
}

int tsd_sdio_enable_function(struct tsd_sdio *card, uint8_t function)
{
  // Refuse a function the card cannot have
  if (function > FUNCTION_MAX)
    return TSD_ERR_PARAM;

  // Set the function's enable bit
  uint8_t enabled;
  int ret = tsd_sdio_read_f0(card, TSD_SDIO_CCCR_IO_ENABLE, &enabled);
  if (ret < 0)
    return ret;
  ret = tsd_sdio_write_f0(card, TSD_SDIO_CCCR_IO_ENABLE, enabled | TSD_BIT(function));
  if (ret < 0)
    return ret;

  // Wait for the card to report the function ready once it has brought it up
  return tsd_sdio_wait_ready(card, function, ENABLE_TIMEOUT_MS);
}

int tsd_sdio_wait_ready(struct tsd_sdio *card, uint8_t function, uint32_t timeout_ms)
{
  // Refuse a function the card cannot have
  if (function > FUNCTION_MAX)
    return TSD_ERR_PARAM;

  // Read the ready bits until the function's is set or the time is up
  for (uint32_t waited_us = 0; waited_us < timeout_ms * 1000; waited_us += READY_DELAY_US) {
    uint8_t ready;
    int ret = tsd_sdio_read_f0(card, TSD_SDIO_CCCR_IO_READY, &ready);
    if (ret < 0)
      return ret;
    if (ready & TSD_BIT(function))
      return 0;
    tsd_delay_us(READY_DELAY_US);
  }

  return TSD_ERR_TIMEOUT;
}

int tsd_sdio_set_block_size(struct tsd_sdio *card, uint8_t function, uint16_t block_size)
{
  // Refuse a function the card cannot have
  if (function > FUNCTION_MAX)
    return TSD_ERR_PARAM;

  // Find the size register, which function 0 keeps in the CCCR and the others in their FBR
  uint32_t address = function ? TSD_SDIO_FBR_BASE(function) + TSD_SDIO_FBR_BLOCK_SIZE : TSD_SDIO_CCCR_FN0_BLOCK_SIZE;

  // Write the size, low byte first
  int ret = tsd_sdio_write_f0(card, address, (uint8_t)block_size);
  if (ret < 0)
    return ret;
  ret = tsd_sdio_write_f0(card, address + 1, (uint8_t)(block_size >> 8));
  if (ret < 0)
    return ret;

  // Read it back, since a card may refuse a size it cannot do
  uint8_t low;
  uint8_t high;
  ret = tsd_sdio_read_f0(card, address, &low);
  if (ret < 0)
    return ret;
  ret = tsd_sdio_read_f0(card, address + 1, &high);
  if (ret < 0)
    return ret;
  if ((low | (high << 8)) != block_size)
    return TSD_ERR_PARAM;

  return 0;
}

int tsd_sdio_enable_interrupt(struct tsd_sdio *card, uint8_t function)
{
  // Refuse a function the card cannot have
  if (function > FUNCTION_MAX)
    return TSD_ERR_PARAM;

  // Add the function's bit and the master bit to the enables
  uint8_t enabled;
  int ret = tsd_sdio_read_f0(card, TSD_SDIO_CCCR_INT_ENABLE, &enabled);
  if (ret < 0)
    return ret;
  return tsd_sdio_write_f0(card, TSD_SDIO_CCCR_INT_ENABLE, enabled | TSD_SDIO_INT_ENABLE_MASTER | TSD_BIT(function));
}

void tsd_sdio_set_card_interrupt(struct tsd_sdio *card, bool on)
{
  // Let the host mask it around each command
  tsd_host_set_card_interrupt(card->host, on);
}

int tsd_sdio_read_byte(struct tsd_sdio *card, uint8_t function, uint32_t address, uint8_t *value)
{
  // Refuse a function the card cannot have
  if (function > FUNCTION_MAX)
    return TSD_ERR_PARAM;

  // Refuse an address the argument cannot hold
  if (address > ADDRESS_MAX)
    return TSD_ERR_PARAM;

  // Read the byte with CMD52
  uint32_t argument = TSD_FIELD_PREP(TSD_SDIO_ARG_FUNCTION, function) | TSD_FIELD_PREP(TSD_SDIO_ARG_ADDRESS, address);
  uint32_t response;
  int ret = tsd_host_send_command(card->host, TSD_CMD_IO_RW_DIRECT, argument, TSD_RESPONSE_R5, &response);
  if (ret < 0)
    return ret;

  // Report an error the card flagged, or take the byte
  ret = check_response(response);
  if (ret < 0)
    return ret;
  *value = (uint8_t)TSD_FIELD_GET(TSD_SDIO_R5_DATA, response);

  return 0;
}

int tsd_sdio_write_byte(struct tsd_sdio *card, uint8_t function, uint32_t address, uint8_t value)
{
  // Refuse a function the card cannot have
  if (function > FUNCTION_MAX)
    return TSD_ERR_PARAM;

  // Refuse an address the argument cannot hold
  if (address > ADDRESS_MAX)
    return TSD_ERR_PARAM;

  // Write the byte with CMD52
  uint32_t argument = TSD_FIELD_PREP(TSD_SDIO_ARG_FUNCTION, function);
  argument |= TSD_FIELD_PREP(TSD_SDIO_ARG_ADDRESS, address);
  argument |= TSD_FIELD_PREP(TSD_SDIO_ARG_DATA, value);
  argument |= TSD_SDIO_ARG_WRITE;
  uint32_t response;
  int ret = tsd_host_send_command(card->host, TSD_CMD_IO_RW_DIRECT, argument, TSD_RESPONSE_R5, &response);
  if (ret < 0)
    return ret;

  // Report an error the card flagged
  return check_response(response);
}

int tsd_sdio_transfer(struct tsd_sdio *card, uint8_t function, uint32_t address, bool write, void *buffer, uint32_t len,
                      uint16_t block_size, uint32_t *done)
{
  // Refuse a function the card cannot have
  if (function > FUNCTION_MAX)
    return TSD_ERR_PARAM;

  // Refuse an empty transfer and one that runs past the highest address the argument holds
  if (len == 0 || address > ADDRESS_MAX || len - 1 > ADDRESS_MAX - address)
    return TSD_ERR_PARAM;

  // Move whole blocks in block mode, as many as one command takes, then the rest in byte mode
  uint8_t *bytes = buffer;
  uint32_t moved = 0;
  int ret        = 0;
  while (moved < len && ret == 0) {
    // Size the next command, never more in byte mode than a block or 512 bytes
    uint32_t remaining        = len - moved;
    uint16_t chunk_block_size = 0;
    uint32_t chunk;
    if (block_size && remaining >= block_size) {
      uint32_t blocks  = remaining / block_size < BLOCK_MODE_MAX ? remaining / block_size : BLOCK_MODE_MAX;
      chunk            = blocks * block_size;
      chunk_block_size = block_size;
    } else {
      chunk = remaining < BYTE_MODE_MAX ? remaining : BYTE_MODE_MAX;
      if (block_size && chunk > block_size)
        chunk = block_size;
    }

    // Run it at the address the transfer has reached, counting what it moved
    uint32_t chunk_moved = 0;
    ret = run_extended(card, function, address + moved, write, bytes + moved, chunk, chunk_block_size, &chunk_moved);
    moved += chunk_moved;
  }

  // Report the bytes moved
  if (done)
    *done = moved;

  return ret;
}
