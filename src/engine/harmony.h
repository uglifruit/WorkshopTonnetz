// harmony.h — choosing where to go next on the Tonnetz.
//
// The engine always holds three chords: the previous one, the current one,
// and a PLANNED next one. The plan is made the moment a chord starts, so the
// melody can lean towards it for the whole of the current chord. Until the
// commit point (config commitPhase) a change at CV In 1 can replan it; after
// that it is frozen and the melody can anticipate it safely.
//
// Choosing is not a random walk. Every reachable chord is scored on several
// musical objectives, the scores are softened by a lookahead one chord
// further, and the result is sampled from a temperature-controlled softmax.
// See ScoreCandidate() in harmony.cpp for the terms, and docs/HARMONY.md for
// what each is for.

#pragma once
#include "config.h"
#include "rng.h"
#include "tonal.h"
#include "tonnetz.h"
#include "voicing.h"

namespace tonnetz {

struct HarmonyParams
{
	float wander       = 0.30f;   // 0 = settled and tonal, 1 = chromatic and migratory
	float cvAttraction = 0.0f;    // 0..1
	bool  cvStrict     = false;   // the chord MUST contain cvPc, if reachable
	bool  cvActive     = false;   // CV In 1 patched
	int   cvPc         = 0;
	int   phraseLength = 8;       // takes effect at the next phrase
};

struct Candidate
{
	Chord   chord;
	int     move;         // index into kMoves (the shortest route)
	Voicing voicing;
	float   score;        // immediate score
	float   value;        // score + discounted depth-2 soft value
	float   prob;
	// Score breakdown (unweighted terms are not kept — these are the
	// contributions as summed), for tests and for tools/render diagnostics.
	float   vl, gravity, phrase, cv, novelty, repetition, reversal, cycle;
};

class HarmonyEngine
{
public:
	static constexpr int kHistory = 16;

	void Init(const EngineConfig *cfg, Rng *rng, TonalEstimator *tonal);

	/// Back to the home chord: history and phrase cleared, tonal centre
	/// re-seeded, a fresh next chord planned. The current voicing is kept as
	/// the starting point, so the chord glides home rather than jumping.
	void Reset();

	/// Applies new performance parameters. If the CV request changed and the
	/// plan is not yet committed, the next chord is replanned (with
	/// hysteresis: an existing plan that is still a strong option survives).
	void SetParams(const HarmonyParams &p);

	void Commit() { committed_ = true; }
	bool Committed() const { return committed_; }

	/// The planned chord becomes current; a new next chord is planned.
	void Advance();

	Chord Current()  const { return hist_[0]; }
	Chord Previous() const { return nHist_ > 1 ? hist_[1] : hist_[0]; }
	bool  HasPrevious() const { return nHist_ > 1; }
	Chord Next()     const { return next_.chord; }
	int   NextMove() const { return next_.move; }
	int   CurrentMove() const { return histMove_[0]; }
	const Voicing &CurrentVoicing() const { return voicing_; }
	const Voicing &NextVoicing()    const { return next_.voicing; }

	Chord Home()          const { return home_; }
	int   PhrasePos()     const { return phrasePos_; }
	int   PhraseLength()  const { return phraseLen_; }
	bool  NextStartsPhrase() const { return phrasePos_ + 1 >= phraseLen_; }
	float Tension()       const;   // 0..1, Tonnetz distance from home
	float Temperature()   const { return temperature_; }

	/// The candidate list from the most recent plan, probabilities included.
	int Candidates(const Candidate **out) const { *out = cands_; return nCands_; }

	// --- test hooks ---
	void SetHome(Chord h) { home_ = h; }
	void ForceCurrent(Chord c);
	void Replan() { PlanNext(false); }
	float PhraseTarget(int pos) const;

private:
	struct Context
	{
		Chord   hist[kHistory];     // hist[0] is the chord we are moving FROM
		int     histMove[kHistory]; // histMove[0] is the move that reached hist[0]
		int     nHist;
		Voicing voicing;
		int     pos;                // phrase position of the chord being chosen
	};

	void  StartPhrase();
	void  DeriveWeights();
	void  PlanNext(bool keepIfClose);
	int   Generate(const Context &ctx, float cvScale, Candidate *out) const;
	void  Score(const Context &ctx, Candidate &c, float cvScale) const;
	float SoftValue(const Context &ctx, const Candidate &c) const;
	Context MakeContext() const;

	const EngineConfig *cfg_ = nullptr;
	Rng *rng_ = nullptr;
	TonalEstimator *tonal_ = nullptr;
	HarmonyParams params_;

	// Weights derived from wander.
	float temperature_ = 1.0f, wVL_ = 1.0f, wGrav_ = 1.0f, wPhrase_ = 1.0f, wNovelty_ = 0.0f;
	int   maxNovelty_ = 0;

	Chord home_;
	Chord hist_[kHistory];
	int   histMove_[kHistory];
	int   nHist_ = 0;
	Voicing voicing_{ { 55, 60, 64 } };
	bool  haveVoicing_ = false;

	Candidate next_{};
	bool committed_ = false;

	// Phrase state, re-drawn at each phrase start.
	int   phrasePos_ = 0;
	int   phraseLen_ = 8;
	float peakDistance_ = 2.0f;
	float peakPos_ = 0.6f;
	int   corridor_ = 0;      // 0 free, 1 R/L, 2 P/L, 3 P/R
	bool  motif_ = false;

	Candidate cands_[kMaxMoves];
	int nCands_ = 0;
};

} // namespace tonnetz
