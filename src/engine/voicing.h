// voicing.h — where in the register each of the three chord voices goes.
//
// Voices keep their identity: voice 0 is the low voice, 1 the middle, 2 the
// top, and each moves as little as it can to reach a tone of the new chord.
// The search is exhaustive — 6 assignments of the chord's pitch classes to the
// voices x every octave placement inside the register — about 160 candidates,
// which is cheap enough to run for every candidate chord the harmony engine
// scores.

#pragma once
#include "config.h"
#include "tonnetz.h"

namespace tonnetz {

struct Voicing
{
	int note[3];   // MIDI note per voice, voice 0 lowest
};

/// A sensible root-position-ish starting voicing near the register centre.
Voicing InitialVoicing(Chord c, const VoicingConfig &cfg);

/// Best voicing of `c` reached from `prev`. If `cost` is non-null it receives
/// the total cost, in semitone-equivalents (the voice-leading score).
Voicing VoiceLead(const Voicing &prev, Chord c, const VoicingConfig &cfg, float *cost = nullptr);

/// The movement-plus-penalties cost of going from `prev` to `next`. Exposed so
/// tests can check that the chosen voicing is the minimum.
float VoicingCost(const Voicing &prev, const Voicing &next, const VoicingConfig &cfg);

} // namespace tonnetz
