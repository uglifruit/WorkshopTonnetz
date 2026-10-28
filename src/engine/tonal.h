// tonal.h — what scale are we in right now, and where is home?
//
// Two of the three harmonic memories live here (the third, Window A, is the
// melody's prev/current/next weighting — see melody.h):
//
//   Window B — LOCAL SCALE. The last few chords plus the planned next one,
//              scored against 7 scale types x 12 roots. Responsive: it is
//              allowed to change chord by chord, with a little hysteresis.
//              If no seven-note collection explains the harmony well enough,
//              Chromatic() is set and the melody falls back to chord tones and
//              chromatic approaches instead of forcing a misleading scale.
//
//   Window C — TONAL CENTRE. A leaky accumulator over 24 tonic hypotheses
//              (12 major, 12 minor), fed by how each arriving chord functions
//              in each key, by cadences, and by phrase position. Slow: the
//              inertia control sets its memory from ~4 to ~16 chords, and the
//              reported tonic needs a clear margin to change.
//
// "A scale containing the notes" (Window B) and "the note that feels like
// home" (Window C) are deliberately separate answers.

#pragma once
#include <stdint.h>
#include "config.h"
#include "tonnetz.h"

namespace tonnetz {

enum ScaleType : uint8_t
{
	kIonian, kAeolian, kDorian, kMixolydian,
	kHarmonicMinor, kHarmonicMajor,
	kMelodicMinor,          // the ascending collection: 1 2 b3 4 5 6 7
	kNumScaleTypes
};

const char *ScaleTypeName(int type);
uint16_t ScaleMask(int type, int root);

/// Key index 0..23 uses the same layout as Chord::Index(): tonic*2 + minor.
/// How well a chord functions in a key, 0..1 (I = 1, V = 0.85, borrowed iv =
/// 0.35, unrelated = 0). Shared with the harmony engine's gravity term.
float KeyFunction(Chord c, int key);

class TonalEstimator
{
public:
	void Init(const TonalConfig *cfg) { cfg_ = cfg; }
	void Reset(Chord home);
	void SetInertia(float inertia) { inertia_ = inertia; }

	/// A chord has started sounding. `metricWeight` is the phrase-position
	/// emphasis (phrase starts and ends weigh more).
	void OnChordArrived(Chord c, float metricWeight);
	/// The harmony engine's planned next chord (may change until committed).
	void SetPlanned(Chord next);

	// --- Window B ---
	int      ScaleType() const { return scaleType_; }
	int      ScaleRoot() const { return scaleRoot_; }
	uint16_t ScaleMaskNow() const { return ScaleMask(scaleType_, scaleRoot_); }
	float    ScaleConfidence() const { return scaleFit_; }
	bool     Chromatic() const { return scaleFit_ < cfg_->chromaticBelow; }
	float    ScaleFit(int type, int root) const;   // raw fit, for tests

	// --- Window C ---
	const float *KeyProbs() const { return keyProb_; }
	int   Tonic() const { return tonic_; }        // key index
	float TonicConfidence() const { return keyProb_[tonic_]; }

private:
	void UpdateScale();
	void UpdateKeys();
	void Evidence(float ev[12]) const;
	float Fit(const float ev[12], uint16_t mask) const;

	const TonalConfig *cfg_ = nullptr;
	float inertia_ = 0.5f;

	// Window B history, most recent first.
	static constexpr int kMaxB = 8;
	Chord recent_[kMaxB];
	int   nRecent_ = 0;
	Chord planned_;
	bool  hasPlanned_ = false;
	int   scaleType_ = kIonian;
	int   scaleRoot_ = 0;
	float scaleFit_ = 1.0f;

	// Window C.
	float keyEvidence_[kNumChords];
	float evidenceWeight_ = 0.0f;
	float keyProb_[kNumChords];
	int   tonic_ = 0;
};

} // namespace tonnetz
