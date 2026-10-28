// melody.h — the monophonic line on Audio Out 2.
//
// Window A of the harmonic memory lives here: the previous, current and
// planned NEXT chord, weighted per pitch class. The next chord's weight grows
// through the current chord (the anticipation curve, NextWeight()), so early
// in a chord the line sits on the harmony that is sounding and late in it the
// line increasingly prepares the one that is coming:
//
//     nextWeight = initial + anticipation * smoothstep(phase) * (max - initial)
//
// Notes the next chord SHARES with the current one are always safe; notes that
// only belong to the future chord are true anticipations and are steered onto
// weak ticks; semitone/tone neighbours of next-chord tones are approach notes,
// which resolve when the chord arrives.
//
// The melody's rhythm runs on the same tick grid as the chords but is chosen
// independently: onsets are drawn per grid tick from density and metric
// strength, durations from density, and a note may be held across a chord
// boundary (a common tone, or a suspension that then resolves down by step).

#pragma once
#include <stdint.h>
#include "config.h"
#include "plan.h"
#include "rng.h"
#include "tonnetz.h"

namespace tonnetz {

struct MelodyContext
{
	Chord    prev, cur, next;
	bool     hasPrev;
	float    phase;            // 0..1 through the current chord
	int      tickInChord;
	int      ticksPerChord;
	uint16_t scaleMask;        // Window B
	float    scaleConfidence;
	bool     chromatic;
	bool     nextStartsPhrase;
	bool     lastChordOfPhrase;
	bool     chordBoundary;    // this tick is the first of a new chord
};

struct MelodyParams
{
	float anticipation = 0.5f;   // 0..1, Y on the performance page
	float density      = 0.55f;  // 0..1, how often notes start
	float rhythm       = 0.5f;   // 0..1, grid: quarters / eighths / sixteenths
};

struct MelodyEvent
{
	uint8_t action = kMelNone;
	uint8_t note = 0;
};

class MelodyEngine
{
public:
	void Init(const MelodyConfig *cfg, Rng *rng);
	void Reset();

	MelodyEvent Tick(const MelodyContext &ctx, const MelodyParams &p);

	/// The 12 pitch-class weights at this moment (before register/interval
	/// shaping), normalised to sum 1. Exposed for tests.
	void PitchClassWeights(const MelodyContext &ctx, const MelodyParams &p, bool strong, float out[12]) const;
	float NextWeight(float phase, float anticipation) const;

	bool Sounding() const { return sounding_; }
	int  LastNote() const { return lastNote_; }

	/// Grid step in ticks for a rhythm setting: 4 (quarters), 2 (eighths), 1 (16ths).
	static int GridTicks(float rhythm);

private:
	int  ChooseNote(const MelodyContext &ctx, const MelodyParams &p, bool strong);
	int  ChooseDuration(const MelodyParams &p, int grid);
	static float Metric(int tickInChord);

	const MelodyConfig *cfg_ = nullptr;
	Rng *rng_ = nullptr;

	float registerWeight_[128];   // gaussian pull to registerCentre, built in Init()

	bool sounding_ = false;
	int  note_ = 0;
	int  ticksLeft_ = 0;
	int  lastNote_ = -1;
	int  lastInterval_ = 0;
	bool lastWasNonChord_ = false;
	bool suspension_ = false;
	int  recent_[3] = { -1, -1, -1 };
};

} // namespace tonnetz
