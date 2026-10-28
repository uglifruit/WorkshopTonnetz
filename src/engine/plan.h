// plan.h — the one message core 1 sends the audio ISR.
//
// Core 1 decides everything musical ahead of time and posts a TickPlan per
// tick (16th note). The ISR applies a plan the moment its tick fires, so the
// timing is sample-accurate while all the thinking happens elsewhere. See
// link.h for the queue and docs/HARMONY.md for the timing story.

#pragma once
#include <stdint.h>

namespace tonnetz {

enum PlanFlags : uint8_t
{
	kPlanChordChange = 1 << 0,   // chord[] holds new voice targets
	kPlanCommitted   = 1 << 1,   // the next chord is frozen (LED 4)
	kPlanPhraseStart = 1 << 2,
};

enum MelodyAction : uint8_t
{
	kMelNone   = 0,   // keep doing whatever the melody voice is doing
	kMelOn     = 1,   // attack a new note
	kMelLegato = 2,   // change pitch without re-attacking
	kMelOff    = 3,   // release
};

struct TickPlan
{
	uint32_t epoch;          // reset generation; stale plans are discarded
	uint32_t tick;
	uint32_t glideSamples;   // chord pitch ramp length, for a chord change
	uint8_t  flags;
	uint8_t  chord[3];       // MIDI note per chord voice (voice 0 lowest)
	uint8_t  chordRoot;      // MIDI note of the current chord's root (CV Out 1)
	uint8_t  melAction;
	uint8_t  melNote;        // MIDI note, for kMelOn / kMelLegato
	uint8_t  tension;        // 0..255, distance from home (LED 3)
};

} // namespace tonnetz
