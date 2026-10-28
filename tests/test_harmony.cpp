// test_harmony.cpp — chord selection, lookahead/commitment, CV steering.

#include "test.h"
#include "engine/conductor.h"

using namespace tonnetz;

static EngineConfig gCfg;

static Conductor Make(uint32_t seed, PerformParams p = PerformParams{})
{
	Conductor c;
	c.Init(&gCfg, seed);
	c.SetParams(p);
	c.Reset(0);
	return c;
}

/// Plan `chords` chord changes, returning the sequence of current chords.
static int Run(Conductor &c, int chords, Chord *out)
{
	int n = 0;
	while (n < chords)
	{
		const TickPlan p = c.PlanTick();
		if (p.flags & kPlanChordChange) out[n++] = c.Harmony().Current();
	}
	return n;
}

TEST(harmony_destinations_are_reachable_and_valid)
{
	for (float wander : { 0.0f, 0.5f, 1.0f })
	{
		PerformParams p;
		p.wander = wander;
		Conductor c = Make(7, p);
		Chord prev = c.Harmony().Current();
		for (int i = 0; i < 400; i++)
		{
			const Chord next = c.Harmony().Next();
			CHECK(next != prev);
			CHECK(next.root < 12);
			bool reachable = false;
			for (int m = 0; m < kNumMoves; m++)
				if (ApplyMove(prev, kMoves[m]) == next) reachable = true;
			CHECK(reachable);
			c.Harmony().Advance();
			prev = c.Harmony().Current();
		}
	}
}

TEST(harmony_softmax_probabilities_sum_to_one)
{
	Conductor c = Make(3);
	for (int i = 0; i < 50; i++)
	{
		const Candidate *cands;
		const int n = c.Harmony().Candidates(&cands);
		CHECK(n >= 3);
		double sum = 0.0;
		for (int k = 0; k < n; k++)
		{
			CHECK(cands[k].prob >= 0.0f && cands[k].prob <= 1.0f);
			CHECK(std::isfinite(cands[k].value));
			sum += cands[k].prob;
		}
		CHECK_NEAR(sum, 1.0, 1e-4);
		c.Harmony().Advance();
	}
}

/// Fraction of chord changes that go straight back to the chord before last.
static float OscillationRate(float wander, uint32_t seed)
{
	PerformParams p;
	p.wander = wander;
	Conductor c = Make(seed, p);
	Chord seq[600];
	Run(c, 600, seq);
	int back = 0;
	for (int i = 2; i < 600; i++) back += seq[i] == seq[i - 2];
	return static_cast<float>(back) / 598.0f;
}

TEST(harmony_avoids_two_chord_oscillation)
{
	const float lo = OscillationRate(0.1f, 11), hi = OscillationRate(0.9f, 11);
	NOTE("A-B-A rate: wander 0.1 -> %.1f%%, wander 0.9 -> %.1f%%", lo * 100.0f, hi * 100.0f);
	// An unweighted walk over ~8-13 moves would go back ~10% of the time; a
	// fixed P/L/R walk 33%. Repetition is discouraged, not forbidden.
	CHECK(lo < 0.12f);
	CHECK(hi < 0.12f);
}

TEST(harmony_wander_controls_distance_from_home)
{
	auto stats = [](float wander, float &meanDist, int &distinct) {
		PerformParams p;
		p.wander = wander;
		Conductor c = Make(21, p);
		Chord seq[800];
		Run(c, 800, seq);
		float d = 0.0f;
		bool seen[kNumChords] = {};
		distinct = 0;
		for (const Chord &x : seq)
		{
			d += static_cast<float>(Distance(x, Chord(0, kMajor)));
			if (!seen[x.Index()]) { seen[x.Index()] = true; distinct++; }
		}
		meanDist = d / 800.0f;
	};
	float dLo, dHi;
	int nLo, nHi;
	stats(0.0f, dLo, nLo);
	stats(1.0f, dHi, nHi);
	NOTE("wander 0: mean distance from C %.2f, %d distinct chords; wander 1: %.2f, %d distinct", dLo, nLo, dHi, nHi);
	CHECK(dHi > dLo + 0.5f);
	CHECK(nHi >= nLo);
}

TEST(harmony_conservative_settings_stay_tonal)
{
	PerformParams p;
	p.wander = 0.0f;
	Conductor c = Make(5, p);
	Chord seq[400];
	Run(c, 400, seq);
	int diatonic = 0;
	const uint16_t cMajor = 0xAB5;
	for (const Chord &x : seq) diatonic += (x.Mask() & ~cMajor) == 0;
	int tonicC = c.Tonal().Tonic() == Chord(0, kMajor).Index();
	NOTE("wander 0: %d%% of chords diatonic to C; final tonic is C: %d", diatonic * 100 / 400, tonicC);
	CHECK(diatonic * 100 / 400 >= 65);
}

TEST(harmony_phrases_return_home)
{
	PerformParams p;
	p.wander = 0.2f;
	p.phraseLength = 8;
	Conductor c = Make(9, p);
	int phrases = 0, resolved = 0;
	for (int i = 0; i < 64 * 8; i++)
	{
		c.Harmony().Advance();
		const HarmonyEngine &h = c.Harmony();
		if (h.PhrasePos() == h.PhraseLength() - 1)
		{
			phrases++;
			resolved += h.Current() == h.Home();
		}
	}
	NOTE("wander 0.2: %d of %d phrases end on home", resolved, phrases);
	CHECK(resolved * 100 / phrases >= 50);
}

TEST(harmony_next_chord_available_before_boundary)
{
	Conductor c = Make(4);
	// Every tick of a chord already knows the next chord.
	Chord planned = c.Harmony().Next();
	for (int t = 0; t < kTicksPerChord * 20; t++)
	{
		const TickPlan p = c.PlanTick();
		if ((p.flags & kPlanChordChange) && t > 0)
		{
			CHECK(c.Harmony().Current() == planned);   // the chord that arrived is the one planned
			// and voices land on the planned voicing
			for (int v = 0; v < 3; v++) CHECK(p.chord[v] == c.Harmony().CurrentVoicing().note[v]);
		}
		planned = c.Harmony().Next();
	}
}

TEST(harmony_commitment_freezes_plan)
{
	PerformParams p;
	p.cvActive = true;
	p.cvAttraction = 1.0f;
	p.cvStrict = true;
	p.cvPc = 0;
	Conductor c = Make(8, p);
	int changedAfterCommit = 0, changedBefore = 0;
	for (int chord = 0; chord < 200; chord++)
	{
		for (int t = 0; t < kTicksPerChord; t++)
		{
			const Chord before = c.Harmony().Next();
			// Wiggle the CV pitch class every tick.
			p.cvPc = (chord * 5 + t * 7) % 12;
			c.SetParams(p);
			const Chord after = c.Harmony().Next();
			if (c.Harmony().Committed()) changedAfterCommit += before != after;
			else changedBefore += before != after;
			c.PlanTick();
		}
	}
	NOTE("CV-driven replans: %d before commitment, %d after", changedBefore, changedAfterCommit);
	CHECK(changedAfterCommit == 0);
	CHECK(changedBefore > 0);
}

TEST(harmony_commit_point_is_configured_phase)
{
	Conductor c = Make(2);
	for (int t = 0; t < kTicksPerChord * 3; t++)
	{
		const float phase = c.Phase();
		const TickPlan p = c.PlanTick();
		if (phase >= 1.0f) continue;   // boundary tick: phase resets inside PlanTick
		const bool shouldCommit = phase >= gCfg.harmony.commitPhase;
		CHECK(((p.flags & kPlanCommitted) != 0) == shouldCommit);
	}
}

static float CvHitRate(float attraction, bool strict, int pc, uint32_t seed)
{
	PerformParams p;
	p.wander = 0.4f;
	p.cvActive = attraction > 0.0f;
	p.cvAttraction = attraction;
	p.cvStrict = strict;
	p.cvPc = pc;
	Conductor c = Make(seed, p);
	int hits = 0;
	const int n = 2000;
	for (int i = 0; i < n; i++)
	{
		c.Harmony().Advance();
		hits += c.Harmony().Current().Contains(pc);
	}
	return static_cast<float>(hits) / static_cast<float>(n);
}

TEST(harmony_cv_attraction_raises_membership)
{
	const int pc = 1;   // C#: not in C major, so it has to be sought
	const float none = CvHitRate(0.0f, false, pc, 31);
	const float weak = CvHitRate(0.3f, false, pc, 31);
	const float strong = CvHitRate(1.0f, false, pc, 31);
	const float strict = CvHitRate(1.0f, true, pc, 31);
	NOTE("chords containing C#: none %.0f%%, weak %.0f%%, strong %.0f%%, strict %.0f%%",
		none * 100, weak * 100, strong * 100, strict * 100);
	CHECK(weak > none);
	CHECK(strong > weak);
	CHECK(strict >= strong);
	CHECK(strict > 0.75f);
}

TEST(harmony_cv_is_attraction_not_transposition)
{
	// With CV asking for E, the chords should CONTAIN E far more than they are
	// ROOTED on E: membership, not transposition.
	PerformParams p;
	p.cvActive = true;
	p.cvAttraction = 1.0f;
	p.cvPc = 4;
	Conductor c = Make(41, p);
	int contains = 0, rooted = 0;
	for (int i = 0; i < 1000; i++)
	{
		c.Harmony().Advance();
		contains += c.Harmony().Current().Contains(4);
		rooted += c.Harmony().Current().root == 4;
	}
	NOTE("CV=E: contains E %d/1000, rooted on E %d/1000", contains, rooted);
	CHECK(contains > 2 * rooted);
}

TEST(harmony_deterministic_seed_repeats)
{
	Conductor a = Make(1234), b = Make(1234);
	for (int i = 0; i < kTicksPerChord * 40; i++)
	{
		const TickPlan pa = a.PlanTick(), pb = b.PlanTick();
		CHECK(pa.flags == pb.flags && pa.melNote == pb.melNote && pa.melAction == pb.melAction);
		CHECK(pa.chord[0] == pb.chord[0] && pa.chord[1] == pb.chord[1] && pa.chord[2] == pb.chord[2]);
	}
}

TEST(harmony_reset_returns_home)
{
	PerformParams p;
	p.wander = 1.0f;
	Conductor c = Make(77, p);
	for (int i = 0; i < kTicksPerChord * 13 + 5; i++) c.PlanTick();
	c.Reset(1000);
	CHECK(c.NextTick() == 1000);
	const TickPlan first = c.PlanTick();
	CHECK(first.tick == 1000);
	CHECK((first.flags & kPlanChordChange) != 0);
	CHECK(c.Harmony().Current() == Chord(gCfg.harmony.homeRoot, kMajor));
	CHECK(c.Harmony().PhrasePos() == 0);
}
