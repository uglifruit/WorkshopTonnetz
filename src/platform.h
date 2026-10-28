// platform.h — the only file that knows whether we are on the card or the host.
//
// Everything under src/ compiles in two places: arm-none-eabi for the RP2040,
// and a desktop compiler (zig c++) for tests/ and tools/render. Build the host
// side with -DTONNETZ_HOST.

#pragma once
#include <stdint.h>

#ifdef TONNETZ_HOST

#include <atomic>
#define __not_in_flash_func(x) x
#define __not_in_flash(x)
static inline void tz_dmb() { std::atomic_thread_fence(std::memory_order_seq_cst); }

#else

#include "pico.h"
#include "hardware/sync.h"
/// Data memory barrier between the cores. Every cross-core publish is
/// "write the data, tz_dmb(), then bump the sequence number" — see link.h.
static inline void tz_dmb() { __dmb(); }

#endif

// REQUIRED for the Workshop Computer's crystal; also set in CMakeLists.txt.
// Kept here as a #define because the upstream registry validator greps the
// source for this form and does not see the CMake flag.
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif
