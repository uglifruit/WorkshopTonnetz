if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico CACHE STRING "Board type")
cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME "tonnetz")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${CARD_NAME}
  src/main.cpp
  src/composer.cpp
  src/dsp/cardcore.cpp
  src/dsp/clock.cpp
  src/dsp/controls.cpp
  src/dsp/fastmath.cpp
  src/dsp/synth.cpp
  src/engine/conductor.cpp
  src/engine/harmony.cpp
  src/engine/melody.cpp
  src/engine/tonal.cpp
  src/engine/tonnetz.cpp
  src/engine/voicing.cpp)

# -Wdouble-promotion and -Wfloat-conversion are not cosmetic. The RP2040 has no
# FPU: the music engine on core 1 uses float deliberately, but a DOUBLE there,
# or any float at all in the audio ISR, is a bug these flags catch at build time.
target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)

# Worth watching every build: a sudden jump means something large landed in the
# wrong section.
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# PICO_XOSC_STARTUP_DELAY_MULTIPLIER: REQUIRED for the Workshop Computer's
# crystal. Without it the card fails to boot on a cold power-up but works from
# a warm reset. Also #defined in src/platform.h for the registry validator.
#
# PICO_DIVIDER_IN_RAM: the ISR does a 32-bit divide when a chord glide starts
# and when the BPM changes. The hardware divider wrappers live in flash by
# default; an XIP miss inside the audio interrupt is avoidable, so they move.
target_compile_definitions(${CARD_NAME} PRIVATE
  PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
  PICO_DIVIDER_IN_RAM=1)

target_include_directories(${CARD_NAME} PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/src)

# Bring-up only: CV Out 1/2 show ISR load and core-1 planning time instead of
# pitch. See src/main.cpp and docs/BRINGUP.md.
option(TONNETZ_LOAD_METER "Replace the CV outputs with CPU load meters" OFF)
set(FLASHME_NAME ${CARD_NAME})
if(TONNETZ_LOAD_METER)
  target_compile_definitions(${CARD_NAME} PRIVATE TONNETZ_LOAD_METER=1)
  # A different file name, so a meter build can never be mistaken for (or
  # overwrite) the real FLASHME/tonnetz.uf2.
  set(FLASHME_NAME ${CARD_NAME}-loadmeter)
endif()

# Run everything from RAM. Core 1 executes the music engine continuously while
# core 0 services the audio interrupt; from flash they would contend for the
# XIP cache, and a soft-float engine missing in the cache is several times
# slower. The code is small (~48 KB against 256 KB of RAM), and WorkshopTract8
# does the same for the same reason.
pico_set_binary_type(${CARD_NAME} copy_to_ram)

target_link_libraries(${CARD_NAME}
  pico_stdlib
  pico_multicore
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
  hardware_vreg
  hardware_flash)

pico_add_extra_outputs(${CARD_NAME})

# Copy the .uf2 to FLASHME/ on every successful build, so the file you drag onto
# the card is never stale (WorkshopShepard flashed a stale FLASHME once).
# FLASHME/*.uf2 is gitignored; a RELEASED binary goes in UF2/ deliberately.
add_custom_command(TARGET ${CARD_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_CURRENT_LIST_DIR}/FLASHME
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.uf2
          ${CMAKE_CURRENT_LIST_DIR}/FLASHME/${FLASHME_NAME}.uf2
  COMMENT "Copying ${CARD_NAME}.uf2 to FLASHME/${FLASHME_NAME}.uf2")

# No USB serial console in normal use, and stdio over USB pulls in TinyUSB.
pico_enable_stdio_usb(${CARD_NAME} 0)
