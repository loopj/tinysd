# Set minimum CMake version
cmake_minimum_required(VERSION "3.21")

# Configure project and languages
project(tinysd LANGUAGES C)

# SD card driver target
add_library(tinysd_card INTERFACE)
target_sources(tinysd_card INTERFACE src/card_init.c src/card_io.c)
target_include_directories(tinysd_card INTERFACE include)

# SDIO driver target
add_library(tinysd_sdio INTERFACE)
target_sources(tinysd_sdio INTERFACE src/sdio_init.c src/sdio_io.c)
target_include_directories(tinysd_sdio INTERFACE include)

# SDHCI host driver target
add_library(tinysd_sdhci INTERFACE)
target_sources(tinysd_sdhci INTERFACE src/host/sdhci.c)
target_include_directories(tinysd_sdhci INTERFACE include)

# SPI host driver target
add_library(tinysd_spi INTERFACE)
target_sources(tinysd_spi INTERFACE src/host/spi.c)
target_include_directories(tinysd_spi INTERFACE include)
target_link_libraries(tinysd_spi INTERFACE tinysd_frame)

# Frame and CRC helpers target, for software hosts
add_library(tinysd_frame INTERFACE)
target_sources(tinysd_frame INTERFACE src/frame.c src/crc.c)
target_include_directories(tinysd_frame INTERFACE include)

# The whole library
add_library(tinysd INTERFACE)
target_link_libraries(
  tinysd
  INTERFACE tinysd_card tinysd_sdio tinysd_sdhci tinysd_spi tinysd_frame
)

# Build tests by default only when tinysd is the top-level project
option(TINYSD_BUILD_TESTS "Build tinysd tests" ${PROJECT_IS_TOP_LEVEL})

# Include tests if TINYSD_BUILD_TESTS is enabled
if(TINYSD_BUILD_TESTS)
  include(CTest)
  add_subdirectory(test)
endif()
