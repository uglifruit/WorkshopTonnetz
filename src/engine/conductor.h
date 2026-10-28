// conductor.h — turns the harmony and melody engines into one TickPlan per tick.
//
// This is the single entry point to the music. Core 1 calls PlanTick() to
// stay a couple of ticks ahead of the ISR; tools/render and the tests call
// exactly the same function, so what is tested is what the card plays.
//
// Musical time is counted in ticks (16th notes) and the chord phase is
// tickInChord / ticksPerChord — deterministic, independent of tempo or clock
// source, so a given seed produces the same music on the host and the card.

#pragma once
#include <stdint.h>
#include "config.h"
#include "harmony.h"
#include "melody.h"
#include "plan.h"
#include "rng.h"
#include "tonal.h"

namespace tonnetz {

/// Everything the panel and CV inputs can change, already mapped to musical
/// units (see mapping.h for knob -> parameter curves).
struct PerformParams
{
	float wander       = 0.30f;
	float glideSeconds = 0.30f;
	float anticipation = 0.50f;
	float density      = 0.55f;
	float rhythm       = 0.50f;
	int   phraseLength = 8;
	float cvAttraction = 0.50f;
	bool  cvStrict     = false;
	float inertia      = 0.50f;
	bool  cvActive     = false;
	int   cvPc         = 0;
};

class Conductor
{
public:
	void Init(const EngineConfig *cfg, uint32_t seed);

	void SetParams(const PerformParams &p);

	/// Start again from the home chord, the next plan being for `atTick`.
	void Reset(uint32_t atTick);

	/// Plan tick NextTick() and advance.
	TickPlan PlanTick();
	uint32_t NextTick() const { return nextTick_; }

	const HarmonyEngine  &Harmony() const { return harmony_; }
	HarmonyEngine        &Harmony()       { return harmony_; }
	const TonalEstimator &Tonal()   const { return tonal_; }
	const MelodyEngine   &Melody()  const { return melody_; }
	const PerformParams  &Params()  const { return params_; }

	/// Phase 0..1 of the tick about to be planned.
	float Phase() const;

private:
	const EngineConfig *cfg_ = nullptr;
	Rng rng_;
	TonalEstimator tonal_;
	HarmonyEngine harmony_;
	MelodyEngine melody_;
	PerformParams params_;

	uint32_t nextTick_ = 0;
	uint32_t chordStartTick_ = 0;
	bool     started_ = false;
};

} // namespace tonnetz
