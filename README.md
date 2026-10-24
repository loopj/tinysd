<h1 align="center">
  <img src="images/tinysd-2x.png" valign="middle" alt="">
  TinySD
</h1>

Dependency-free C library for SD cards and SDIO on embedded and bare-metal systems.

## Features

- C implementation, no external dependencies
- No dynamic allocation, all state in caller-owned structs
- SD memory card driver (SDSC, SDHC and SDXC) with high speed support
- SDIO card driver with byte and block transfers, card interrupts, and high speed support
- SDHCI host driver for SD Host Controller versions 1 to 3
- Simple interface for adding your own SD hosts
- CRC and framing helpers for software hosts (bit-banged, PIO, etc)
- Polling API, no threads, callbacks or interrupts

## Installation

Copy `tinysd` into your project, or add it as a git submodule.

If you are using CMake:

```cmake
# Add the library directory
add_subdirectory(tinysd)

# Link the targets you need, or tinysd for everything, eg:
target_link_libraries(my_app tinysd_card tinysd_sdhci)
```

Otherwise, add `include/` to your include path and compile what you need from `src/`.

## Usage

### Required Functions

Your application must provide `tsd_delay_us`, which TinySD uses to wait on the card, such as while it powers up.

```c
#include <tinysd.h>

void tsd_delay_us(uint32_t microseconds)
{
  // Spin or sleep for at least microseconds
}
```

### Initializing a Host

A host drives the SD bus, and the card drivers talk to it through `struct tsd_host`.

For example, if using the built-in SDHCI host controller:

```c
static struct tsd_sdhci sdhci;

// Start the host controller, moving data through its FIFO
struct tsd_sdhci_config config = {.base = SDHCI_BASE};
int ret = tsd_sdhci_init(&sdhci, config);

// Pass this to the card drivers
struct tsd_host *host = TSD_HOST(&sdhci);
```

For other hosts, see [Writing Your Own Host](#writing-your-own-host).

### Using SD Memory Cards

Bring the card up at the bus width and highest clock you want. It runs slower if the card cannot keep up.

```c
static struct tsd_card card;
static uint32_t buffer[TSD_CARD_BLOCK_SIZE / 4];

// Bring the card up on four data lines at up to 50 MHz
int ret = tsd_card_init(&card, host, 4, 50000);

// Read block 0, change it and write it back
ret = tsd_card_read(&card, buffer, 0, 1);
ret = tsd_card_write(&card, buffer, 0, 1);

// End any open read, such as before powering down
ret = tsd_card_sync(&card);
```

### Bring Your Own Filesystem

TinySD reads and writes raw blocks, so it is usually paired with a filesystem such as [FatFs](https://elm-chan.org/fsw/ff/), [littlefs](https://github.com/littlefs-project/littlefs), etc.

For example, when using FatFs:

| FatFs                              | TinySD                |
| ---------------------------------- | --------------------- |
| `disk_initialize`                  | `tsd_card_init`       |
| `disk_read`                        | `tsd_card_read`       |
| `disk_write`                       | `tsd_card_write`      |
| `CTRL_SYNC` in `disk_ioctl`        | `tsd_card_sync`       |
| `GET_SECTOR_SIZE` in `disk_ioctl`  | `TSD_CARD_BLOCK_SIZE` |
| `GET_SECTOR_COUNT` in `disk_ioctl` | `card.blocks`         |


### Using SDIO Devices

Bring the card up, then enable the function your device uses and set its block size.

```c
static struct tsd_sdio sdio;
static uint32_t buffer[64];

// Bring the card up on four data lines at up to 50 MHz
int ret = tsd_sdio_init(&sdio, host, 4, 50000);

// Enable function 1 with 64-byte blocks
ret = tsd_sdio_enable_function(&sdio, 1);
ret = tsd_sdio_set_block_size(&sdio, 1, 64);

// Read one register, then a run of bytes
uint8_t value;
ret = tsd_sdio_read_byte(&sdio, 1, 0x00, &value);
ret = tsd_sdio_transfer(&sdio, 1, 0x100, false, buffer, sizeof(buffer), 64, NULL);
```

After init, the ids from the card's CIS are in `sdio.manufacturer_id` and `sdio.card_id`. To read them without bringing the card up, call `tsd_sdio_identify`.

## Running from RAM

To speed up transfers, or to keep them running while flash is unavailable, place `card_io.c`, `sdio_io.c`, `frame.c` and `crc.c` in RAM from your linker script, along with their read-only data. Everything they call must be in RAM too, including your host's operations, `tsd_delay_us` and compiler helpers such as `memcpy`.

## Writing Your Own Host

For systems without an SDHCI controller, you can write your own host, such as one that bit-bangs the bus or drives it with PIO. Hosts like these can build and check frames with `frame.h` and `crc.h`.

Make a struct that embeds `struct tsd_host` as its first field, and point it at a table of your host's operations.

```c
struct my_host {
  struct tsd_host base;
};

static const struct tsd_host_api my_api = {
  .request       = my_request,
  .set_clock     = my_set_clock,
  .set_bus_width = my_set_bus_width,
};

static struct my_host my_host = {.base.api = &my_api};
```

Then pass `TSD_HOST(&my_host)` to the card drivers.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
