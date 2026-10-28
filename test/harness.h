#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <tinysd/bits.h>
#include <tinysd/commands.h>
#include <tinysd/delay.h>
#include <tinysd/host.h>
#include <tinysd/sdio.h>

#include "unity.h"

// Most commands one test records
#define HARNESS_MAX_COMMANDS 64

// Command indexes the script covers
#define HARNESS_COMMAND_INDEXES 64

// Bytes of function 0 that CMD52 reaches, enough for the CCCR and a short CIS
#define HARNESS_F0_SIZE 0x2000

// A command the fake host received
struct recorded_command {
  uint8_t index;
  uint32_t argument;
  enum tsd_response response_type;

  // Blocks in the data phase, 0 without one or for an open read
  uint32_t blocks;

  bool write;
};

// What the fake host returns for one command index
struct scripted_reply {
  int ret;
  uint32_t response[4];

  // Bytes a read's data phase receives, or NULL to leave the buffer alone
  const uint8_t *data;
};

// A host that records each command and answers it from the script
struct fake_host {
  struct tsd_host base;
  struct scripted_reply replies[HARNESS_COMMAND_INDEXES];
  struct recorded_command commands[HARNESS_MAX_COMMANDS];
  int command_count;

  // Clock last set, in kHz
  uint32_t clock_khz;

  // Blocks taken through continue_read
  uint32_t continued_blocks;

  // Busy checks left before the card lets go
  int busy_checks;

  // Function 0 registers and CIS, which CMD52 reads and writes
  uint8_t f0[HARNESS_F0_SIZE];
};

// The fake host the driver under test talks to, set up by harness_reset()
static struct fake_host host;

// Delay nothing, since the fake host answers at once
void tsd_delay_us(uint32_t microseconds)
{
}

// Record a command and answer it from the script
static int fake_request(struct tsd_host *host, struct tsd_command *command, struct tsd_data *data)
{
  struct fake_host *fake = (struct fake_host *)host;

  // Record the command
  TEST_ASSERT_LESS_THAN_MESSAGE(HARNESS_MAX_COMMANDS, fake->command_count, "too many commands");
  struct recorded_command *recorded = &fake->commands[fake->command_count++];
  recorded->index                   = command->index;
  recorded->argument                = command->argument;
  recorded->response_type           = command->response_type;
  recorded->blocks                  = data ? data->blocks : 0;
  recorded->write                   = data && data->write;

  // Answer CMD52 from function 0, with any flags the script holds
  if (command->index == TSD_CMD_IO_RW_DIRECT) {
    uint32_t function = TSD_FIELD_GET(TSD_SDIO_ARG_FUNCTION, command->argument);
    uint32_t address  = TSD_FIELD_GET(TSD_SDIO_ARG_ADDRESS, command->argument);
    memcpy(command->response, fake->replies[command->index].response, sizeof(command->response));
    if (function == 0 && address < HARNESS_F0_SIZE) {
      if (command->argument & TSD_SDIO_ARG_WRITE)
        fake->f0[address] = (uint8_t)command->argument;
      command->response[0] |= fake->f0[address];
    }
    return 0;
  }

  // Answer from the script, counting every byte moved when it succeeds
  struct scripted_reply *reply = &fake->replies[command->index];
  memcpy(command->response, reply->response, sizeof(command->response));
  if (data)
    data->done = reply->ret == 0 ? data->blocks * data->block_size : 0;

  // Fill a read from the script's data
  if (data && !data->write && reply->data)
    memcpy(data->buffer, reply->data, data->blocks * data->block_size);

  return reply->ret;
}

// Count the blocks taken from an open read
static int fake_continue_read(struct tsd_host *host, void *buffer, uint32_t count)
{
  struct fake_host *fake = (struct fake_host *)host;
  fake->continued_blocks += count;
  return 0;
}

// Record the clock
static int fake_set_clock(struct tsd_host *host, uint32_t clock_khz)
{
  struct fake_host *fake = (struct fake_host *)host;
  fake->clock_khz        = clock_khz;
  return 0;
}

// Accept any bus width
static void fake_set_bus_width(struct tsd_host *host, uint8_t width)
{
}

// Report the card busy for the scripted number of checks
static int fake_card_busy(struct tsd_host *host)
{
  struct fake_host *fake = (struct fake_host *)host;
  if (fake->busy_checks == 0)
    return 0;

  // Count down one check while the card stays busy
  fake->busy_checks--;

  return 1;
}

// Operations of the fake host
static const struct tsd_host_api fake_api = {
  .request       = fake_request,
  .set_clock     = fake_set_clock,
  .set_bus_width = fake_set_bus_width,
  .card_busy     = fake_card_busy,
  .continue_read = fake_continue_read,
};

// Operations of the fake host in SPI mode
static const struct tsd_host_api fake_spi_api = {
  .request       = fake_request,
  .set_clock     = fake_set_clock,
  .set_bus_width = fake_set_bus_width,
  .card_busy     = fake_card_busy,
  .spi           = true,
};

// Clear the script and the record, call from setUp()
static inline void harness_reset(void)
{
  memset(&host, 0, sizeof(host));
  host.base.api = &fake_api;
}

// Run the fake host in SPI mode, after harness_reset()
static inline void harness_use_spi(void)
{
  host.base.api = &fake_spi_api;
}

// Return the last recorded command with an index, or NULL when none was sent
static inline struct recorded_command *harness_find(uint8_t index)
{
  for (int i = host.command_count - 1; i >= 0; i--) {
    if (host.commands[i].index == index)
      return &host.commands[i];
  }
  return NULL;
}

// Count the recorded commands with an index
static inline int harness_count(uint8_t index)
{
  int count = 0;
  for (int i = 0; i < host.command_count; i++) {
    if (host.commands[i].index == index)
      count++;
  }
  return count;
}
