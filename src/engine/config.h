// config.h — every musical range, weight and default in one place.
//
// The engine reads its tunables from an EngineConfig instance rather than from
// scattered constants, so an experiment is "change a number here, re-run
// tools/render", never a restructure. Weights are starting points chosen by
// ear and by the render statistics, not truths; docs/HARMONY.md explains what
// each one pulls against.
//
// The timing-grid constants at the top are compile-time because both cores
// must agree on them: the ISR counts ticks, core 1 plans them.

#pragma once
#include <stdint.h>

namespace tonnetz {

// ---------------------------------------------------------------------------
// Timing grid — shared by the ISR transport and the composer
// ---------------------------------------------------------------------------

constexpr int kSampleRate     = 48000;
constexpr int kTicksPerBeat   = 4;                       // one tick = one 16th note
constexpr int kBeatsPerChord  = 4;
constexpr int kTicksPerChord  = kTicksPerBeat * kBeatsPerChord;

/// How many ticks ahead of the ISR core 1 keeps the plan queue filled. This is
/// also the real-time latency between a control/CV change and the music
/// hearing it, so it is kept small: 2 ticks is 125 ms at 120 BPM.
constexpr int kPlanLookaheadTicks = 2;

/// Seed for the generative RNG. 0 = seed from card ID + ADC noise at boot
/// (different every run); any other value makes the card repeat itself
/// exactly, which is what you want when chasing a musical bug.
constexpr uint32_t kDeterministicSeed = 0;

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------

/// Endpoints for parameters that the Main ("wander") knob sweeps. Each field
/// is {conservative, adventurous}; the engine interpolates by wander 0..1.
struct Range { float lo, hi; };

struct HarmonyConfig
{
	// Home key at boot and after reset.
	int  homeRoot  = 0;      // pitch class, 0 = C
	bool homeMinor = false;

	// Softmax temperature, in score units. Low = nearly always the best
	// candidate; high = the candidates' scores barely matter.
	Range temperature  { 0.35f, 1.60f };

	// Weights of the score terms (see harmony.cpp ScoreCandidate).
	Range wVoiceLeading { 1.20f, 0.60f };
	Range wGravity      { 3.00f, 0.45f };   // compatibility with the inferred key
	Range homeShare     { 0.65f, 0.35f };   // part of gravity aimed at HOME rather than the inferred key
	Range wPhrase       { 1.30f, 0.55f };
	Range wNovelty      { -0.90f, 0.35f };  // per novelty rank; negative = compound moves discouraged
	float wRepetition    = 0.90f;
	float repetitionDecay = 0.80f;         // per chord of age
	float wReversal      = 2.20f;          // going straight back where we came from
	float motifReversalScale = 0.20f;      // reversal penalty when a phrase is in "motif" mode
	float wCycle         = 0.80f;          // continuing this phrase's chosen P/L/R alternation
	float resolveBonus   = 2.50f;          // landing on home at the phrase's last chord (x gravity share)
	float dominantBonus  = 0.80f;          // home's dominant on the penultimate chord
	float wCvMembership  = 3.00f;          // x attraction: candidate CONTAINS the CV pitch class
	float wCvTonic       = 1.00f;          // x attraction: candidate is ROOTED on it (separate concept)

	// Novelty availability: compound moves of rank >= 1 only exist above this
	// wander, rank 2 above the second.
	float novelty1Wander = 0.20f;
	float novelty2Wander = 0.55f;

	// Phrase contour: target Tonnetz distance from home peaks at peakPos of
	// the phrase. Peak distance is jittered per phrase so phrases differ.
	Range peakDistance  { 1.60f, 4.00f };
	float peakJitter     = 0.60f;
	float peakPos        = 0.60f;
	float peakPosJitter  = 0.12f;

	// Per-phrase probabilities.
	float motifProb      = 0.15f;
	// Corridor (P/L/R alternation) choice per phrase: {free, RL, PL, PR}.
	float corridorLo[4] = { 0.50f, 0.40f, 0.05f, 0.05f };
	float corridorHi[4] = { 0.40f, 0.20f, 0.20f, 0.20f };

	// Home migration: above this wander, a confident inferred tonic may become
	// the new home at a phrase boundary.
	float migrateWander  = 0.60f;
	float migrateRate    = 1.20f;   // probability per phrase = (wander - migrateWander) * rate
	float migrateMinConf = 0.30f;   // tonic probability; wandering harmony is rarely more certain

	// Lookahead search.
	int   beamWidth      = 6;       // depth-1 candidates extended to depth 2 (core-1 time ~ beam)
	float futureDiscount = 0.50f;   // weight of the depth-2 soft value

	// Commitment: after this fraction of the current chord the planned next
	// chord is frozen. In REAL time it commits kPlanLookaheadTicks earlier.
	float commitPhase    = 0.78f;
	// Replanning hysteresis: on a CV-driven replan the existing plan is kept
	// if its new probability is at least this fraction of the best one's.
	float replanKeepRatio = 0.50f;
};

struct TonalConfig
{
	// Window B — local scale.
	int   windowB        = 5;       // recent chords considered
	float decayB         = 0.70f;   // weight per chord of age
	float plannedWeight  = 0.55f;   // the planned n+1
	float outsidePenalty = 1.50f;   // per unit of evidence outside the scale
	float chromaticBelow = 0.72f;   // fit below this = "no seven-note scale explains it"
	Range scaleMargin   { 0.03f, 0.14f };  // hysteresis, by inertia

	// Window C — tonal centre.
	Range decayC        { 0.80f, 0.95f };  // by inertia; ~5 to ~20 chord memory
	float beta           = 8.0f;    // softmax sharpness over normalised evidence
	float cadenceBonus   = 0.80f;   // V -> I
	float fifthBonus     = 0.40f;   // any root falling a fifth onto the tonic triad
	float plagalBonus    = 0.30f;   // IV/iv -> I
	Range tonicMargin   { 0.25f, 0.80f };  // reported tonic switches only past this ratio
};

struct MelodyConfig
{
	int   low  = 60;
	int   high = 84;
	float registerCentre = 72.0f;
	float registerWidth  = 8.0f;

	// Window A weights.
	float prevWeight        = 0.35f;
	float curWeight         = 1.00f;
	float initialNextWeight = 0.10f;
	float maximumNextWeight = 1.20f;  // the brief's 0.9 measured too timid against the scale term
	float curEase           = 0.30f;  // current-chord weight given up at full anticipation, chord end
	float scaleWeight       = 0.45f;
	float approachWeight    = 0.70f;
	float chromaticApproach = 0.30f;  // approach notes outside chord and scale
	float curFloor          = 0.40f;  // current chord tones keep at least this share
	float strongNonChord    = 0.50f;  // non-chord tones on strong ticks
	float strongAnticipation = 0.25f; // future-only tones on strong ticks
	float phraseEndBoost    = 1.30f;

	// Rhythm.
	float suspensionProb = 0.35f;
	float legatoProb     = 0.45f;
	float phraseEndRest  = 0.50f;
};

struct VoicingConfig
{
	int   low  = 48;
	int   high = 76;
	float crossingPenalty = 6.0f;
	float leapThreshold   = 5.0f;   // semitones per voice before extra cost
	float leapPenalty     = 0.5f;   // per semitone beyond the threshold
	float maxSpan         = 19.0f;
	float spanPenalty     = 0.6f;
	float unisonPenalty   = 8.0f;
	float centre          = 62.0f;  // mean pitch the voicing drifts back towards
	float centrePull      = 0.10f;
};

struct EngineConfig
{
	HarmonyConfig harmony;
	TonalConfig   tonal;
	MelodyConfig  melody;
	VoicingConfig voicing;
};

static inline float Lerp(Range r, float t) { return r.lo + (r.hi - r.lo) * t; }

} // namespace tonnetz
