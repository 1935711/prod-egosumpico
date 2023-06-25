cmake_minimum_required(VERSION 3.12)

# Pull in PICO SDK (must be before project)
set(ENV{PICO_SDK_PATH} ../lib/pico-sdk)
include(lib/pico-sdk/external/pico_sdk_import.cmake)

# We also need PICO EXTRAS
set(ENV{PICO_EXTRAS_PATH} ../lib/pico-extras)
include(lib/pico-extras/external/pico_extras_import.cmake)

project(egosumpico C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS_RELEASE "-Ofast -Wall -ffast-math")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -Wall -ffast-math")
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1) # For clangd.

set(PICO_NO_FPGA_CHECK 1)
set(PICO_RP2040_B0_SUPPORTED 0)
set(PICO_PLATFORM rp2040)
set(PICO_BOARD vgaboard)
set(PICO_DEFAULT_BINARY_TYPE no_flash)

# Initialize the Pico SDK
pico_sdk_init()

add_executable(egosumpico
        src/main.c
        )
target_compile_definitions(egosumpico PRIVATE
        PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA=1
        TURBO_BOOST=1

        PICO_AUDIO_I2S_MONO_INPUT=1

        USE_AUDIO_I2S=1
        PICO_AUDIO_I2S_DATA_PIN=26
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=27

        PICO_SD_CLK_PIN=5
        PICO_SD_CMD_PIN=18
        PICO_SD_DAT0_PIN=19

        # Channel 0 and PIO 0 are used for VGA.
        PICO_AUDIO_I2S_DMA_IRQ=1
        PICO_AUDIO_I2S_PIO=1

        # PICO_DEFAULT_UART=1
        # PICO_DEFAULT_UART_TX_PIN=20
        # PICO_DEFAULT_UART_RX_PIN=21
        NDEBUG
        )
target_link_libraries(egosumpico PRIVATE
        pico_stdlib
        pico_audio_i2s
        pico_scanvideo_dpi
        pico_multicore
        )

pico_add_extra_outputs(egosumpico)
