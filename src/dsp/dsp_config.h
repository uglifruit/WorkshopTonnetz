// dsp_config.h — constants for the audio ISR side.
//
// Integer-only, because everything here runs inside ProcessSample() on a core
// with no FPU. Musical tunables live in engine/config.h instead.

#pragma once
#include <stdint.h>

namespace tonnetz {

/// Control work (knobs, CV, pitch ramps) runs once per this many samples:
/// 48 kHz / 16 = 3 kHz. Power of two so the counter is a mask.
constexpr int kControlBlock = 16;
constexpr int kControlRate  = 48000 / kControlBlock;

// --- Synth levels -----------------------------------------------------------
// Three chord voices at 0.30 each peak at 0.90 of full scale when every phase
// lines up (they rarely do); the melody at 0.80. Out is Q15 >> 4 -> +-2047.
constexpr int32_t kChordGainQ15  = 9830;    // 0.30
constexpr int32_t kMelodyGainQ15 = 26214;   // 0.80
constexpr int32_t kChordTriMixQ15  = 6554;  // 0.20 triangle in the chord voices
constexpr int32_t kMelodyTriMixQ15 = 13107; // 0.40 triangle in the melody

// --- Melody envelope (Q30 level) ---------------------------------------------
constexpr int32_t kEnvOne          = 1 << 30;
constexpr int32_t kAttackSamples   = 192;               // 4 ms linear attack
constexpr int32_t kSustainLevel    = (kEnvOne / 10) * 7; // 0.70
constexpr int     kDecayShift      = 12;   // tau 4096 samples = 85 ms
constexpr int     kReleaseShift    = 11;   // tau 2048 samples = 43 ms (-60 dB ~ 300 ms)
constexpr int32_t kEnvFloor        = 1 << 13;   // ~ -102 dB: below the DAC's last bit
constexpr int     kLegatoGlideBlocks = 30;      // 10 ms portamento on a legato step

// Pitches are Q16 semitones (MIDI note << 16). The interpolated increment
// table is exact on every semitone and good to < 1 cent between them, and its
// 8-bit interpolation stays inside uint32 only up to MIDI 96 — so pitches are
// clamped to this range. Both registers sit far inside it.
constexpr int kPitchMin = 12;
constexpr int kPitchMax = 96;

// --- Clock ------------------------------------------------------------------
enum class ClockFallback : uint8_t
{
	Hold,        // patched clock stops -> the music holds (chord sustains, melody releases)
	Freewheel,   // ... -> keep going at the last external tempo
	Internal,    // ... -> keep going at the internal BPM
};
/// What happens when Pulse In 1 is patched but its pulses stop arriving.
/// Unpatching the jack always returns to the internal clock.
constexpr ClockFallback kClockFallback = ClockFallback::Hold;

constexpr uint32_t kMinPulseInterval = 2400;     // 20 ms: faster edges are bounce, ignored
constexpr uint32_t kMaxPulseInterval = 4 * 48000; // 4 s: slower = not a clock, period not updated
constexpr uint32_t kLostMinSamples   = 2 * 48000; // lost after max(2 periods, 2 s)

// --- Pulse outputs ----------------------------------------------------------
constexpr int32_t kTriggerSamples = 240;   // 5 ms

// --- CV In 1 (1 V/oct) ------------------------------------------------------
// CVIn is +-2048 for about +-6 V, i.e. 341.33 counts/V, so one semitone is
// 28.44 counts = 256/9: semitone * 256 = counts * 9 exactly. CV inputs are not
// factory-calibrated; trim these if a keyboard reads sharp/flat (see
// docs/BRINGUP.md).
constexpr int32_t kCvOffsetCounts = 0;       // counts read at 0 V
constexpr int32_t kCvScaleQ12     = 4096;    // 1.0 = nominal 1 V/oct
constexpr int32_t kCvHysteresisQ8 = 64;      // quarter semitone beyond the half-way point
constexpr int     kCvSettleTicks  = 24;      // 8 ms at control rate before a new note counts
// CV In 2: +-5 V sweeps the whole wander range.
constexpr int32_t kCv2FullScale   = 1707;    // counts at 5 V

// --- Panel ------------------------------------------------------------------
constexpr int kSwitchSettleTicks = 60;      // 20 ms debounce
constexpr int kPageSettleTicks   = 36;      // 12 ms: Up<->Middle transit is not a page visit
constexpr int kTapMaxTicks       = 1200;    // a Down press shorter than 400 ms is a TAP (reset)
constexpr int32_t kPickupBand    = 164;     // 4% of travel

} // namespace tonnetz
