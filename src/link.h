// link.h — everything the two cores share, and the rules for sharing it.
//
//   core 0 (audio ISR)  -> core 1 (composer):  panel params, fired tick, reset
//   core 1 (composer)   -> core 0 (audio ISR): TickPlans, via a SPSC ring
//
// Every publish is "write the data, tz_dmb(), then bump the index/sequence";
// every read is "read the index/sequence, tz_dmb(), then read the data"
// (the WorkshopBio pattern). Indices only ever increase and wrap naturally.
//
// Core 1 never calls a ComputerCard accessor: the ISR copies what core 1
// needs into here. (ComputerCard's members are not volatile, so core 1 could
// cache them forever.)

#pragma once
#include <stdint.h>
#include "platform.h"
#include "engine/plan.h"

namespace tonnetz {

/// Panel/CV state published by the ISR, in raw-ish units. The composer maps
/// them to musical parameters with mapping.h.
enum ParamId : uint8_t
{
	kParamWander,       // Q12 knob, performance Main
	kParamGlide,        // Q12 knob, performance X
	kParamAntic,        // Q12 knob, performance Y
	kParamDensity,      // Q12 knob, timing X
	kParamPhraseIndex,  // 0..3 -> 2/4/8/16 chords (hysteresis applied in the ISR)
	kParamCvAttract,    // Q12 knob, advanced Main
	kParamRhythm,       // Q12 knob, advanced X
	kParamInertia,      // Q12 knob, advanced Y
	kParamCvSemitone,   // quantised CV In 1, semitones from 0 V (can be negative)
	kParamCvActive,     // CV In 1 patched
	kParamCv2,          // CV In 2, smoothed counts (+-2048)
	kNumParams
};

struct Link
{
	static constexpr uint32_t kRingSize = 16;   // power of two
	static_assert((kRingSize & (kRingSize - 1)) == 0, "ring size must be a power of two");

	TickPlan ring[kRingSize];
	volatile uint32_t head = 0;            // written by core 1
	volatile uint32_t tail = 0;            // written by the ISR

	volatile uint32_t firedTick = 0;       // last tick the ISR fired
	volatile uint32_t transportRunning = 0;
	volatile uint32_t resetEpoch = 0;      // bumped by the ISR on a reset
	volatile uint32_t restartTick = 0;     // the tick the reset music starts on
	volatile uint32_t composerReady = 0;   // core 1 has queued the first plans

	volatile uint32_t paramSeq = 0;        // odd while the ISR is writing
	volatile int32_t  params[kNumParams] = {};

	volatile uint32_t entropy = 0;         // ADC noise for the seed
	volatile uint32_t entropyReady = 0;

	volatile uint32_t planWorstUs = 0;     // core 1's slowest PlanTick (bring-up meter)

	// --- core 1 (producer) ---
	bool Full() const { return head - tail >= kRingSize; }
	void Push(const TickPlan &p)
	{
		ring[head & (kRingSize - 1)] = p;
		tz_dmb();
		head = head + 1;
	}

	// --- ISR (consumer) ---
	const TickPlan *Peek() const
	{
		if (tail == head) return nullptr;
		tz_dmb();
		return &ring[tail & (kRingSize - 1)];
	}
	void Pop()
	{
		tz_dmb();
		tail = tail + 1;
	}

	// --- params: ISR writes, core 1 reads (seqlock) ---
	void WriteParams(const int32_t *v)
	{
		paramSeq = paramSeq + 1;
		tz_dmb();
		for (int i = 0; i < kNumParams; i++) params[i] = v[i];
		tz_dmb();
		paramSeq = paramSeq + 1;
	}
	bool ReadParams(int32_t *out) const
	{
		const uint32_t s = paramSeq;
		if (s & 1u) return false;
		tz_dmb();
		for (int i = 0; i < kNumParams; i++) out[i] = params[i];
		tz_dmb();
		return paramSeq == s;
	}
};

} // namespace tonnetz
