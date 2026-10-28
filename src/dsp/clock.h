// clock.h — the transport's tick source: internal tempo or Pulse In 1.
//
// One tick = one 16th note; four ticks a beat. Process() runs every sample
// and returns true on the sample a tick fires, with Beat() saying whether it
// was a beat (the first 16th of four).
//
// INTERNAL: a sample counter against a Q8 period derived from BPM.
//
// EXTERNAL (Pulse In 1, one pulse per quarter note): each rising edge is a
// beat tick; the three 16ths between are scheduled from the measured pulse
// period (median of the last three intervals). If the clock speeds up and an
// edge arrives before those three have fired, the beat still lands on the
// edge and the transport skips ahead to the next beat (the caller rounds the
// tick count up to a multiple of four) rather than bunching ticks together.
//
// NO DOUBLE ADVANCE on a source change: an edge that arrives within a guard
// window after a beat tick has just fired is taken to BE that beat — it
// re-aligns the 16th grid but fires nothing.
//
// STATES. Jack detection (the normalisation probe) tells us whether a cable
// is patched, so "no pulses" can mean two different things:
//   - unpatched           -> Internal, always.
//   - patched, pulses stop -> after max(2 periods, 2 s), kClockFallback
//     decides: Hold (default: the music waits for the clock), Freewheel at the
//     last external tempo, or the Internal tempo. The next edge resumes
//     External.

#pragma once
#include <stdint.h>
#include "dsp_config.h"
#include "platform.h"

namespace tonnetz {

class ClockEngine
{
public:
	enum class State : uint8_t { Internal, External, Lost };

	void Init(uint32_t internalPeriodQ8);

	/// Samples per 16th, Q8. Takes effect from the next tick.
	void SetInternalPeriod(uint32_t periodQ8) { internalPeriodQ8_ = periodQ8; }

	bool Process(bool risingEdge, bool patched);

	bool  Beat() const { return beat_; }
	State GetState() const { return state_; }
	bool  Running() const { return !(state_ == State::Lost && kClockFallback == ClockFallback::Hold); }
	uint32_t SamplesSinceTick() const { return sinceTick_; }
	uint32_t ExternalPeriod() const { return extPeriod_; }   // samples per beat

	/// Samples per 16th, Q8, for a BPM in Q4 (bpm * 16). 720000/bpm samples per
	/// 16th; the Q8 numerator 2.95e9 still fits a uint32.
	static uint32_t PeriodQ8FromBpmQ4(uint32_t bpmQ4) { return 2949120000u / bpmQ4; }

private:
	bool FireTick(bool beat);

	State    state_ = State::Internal;
	uint32_t internalPeriodQ8_ = 0;
	uint32_t accQ8_ = 0;
	uint32_t sinceTick_ = 0;
	uint32_t sinceEdge_ = 0;
	uint32_t intervals_[3] = { 0, 0, 0 };
	int      nIntervals_ = 0;
	uint32_t extPeriod_ = 0;     // samples per beat
	uint32_t subPeriod_ = 0;     // samples per 16th
	uint32_t subAcc_ = 0;
	int      subLeft_ = 0;
	int      tickInBeat_ = 0;
	bool     beat_ = false;
	bool     lastTickBeat_ = false;
};

} // namespace tonnetz
