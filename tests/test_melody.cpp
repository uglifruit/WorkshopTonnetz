// test_melody.cpp — Window A weights, anticipation, rhythm.

#include "test.h"
#include "engine/conductor.h"
#include "engine/melody.h"

using namespace tonnetz;

static const MelodyConfig kMel{};

static MelodyContext Ctx(Chord prev, Chord cur, Chord next, float phase)
{
	MelodyContext c{};
	c.prev = prev;
	c.cur = cur;
	c.next = next;
	c.hasPrev = true;
	c.phase = phase;
	c.tickInChord = static_cast<int>(phase * kTicksPerChord);
	c.ticksPerChord = kTicksPerChord;
	c.scaleMask = 0xAB5;   // C major
	c.scaleConfidence = 1.0f;
	c.chromatic = false;
	return c;
}

static const Chord C(0, kMajor), Em(4, kMinor), F(5, kMajor);

TEST(melody_next_weight_curve)
{
	MelodyEngine m;
	Rng r;
	m.Init(&kMel, &r);
	float prev = -1.0f;
	for (int i = 0; i <= 20; i++)
	{
		const float w = m.NextWeight(i / 20.0f, 1.0f);
		CHECK(w >= prev);   // monotonic
		prev = w;
	}
	CHECK_NEAR(m.NextWeight(0.0f, 1.0f), kMel.initialNextWeight, 1e-6);
	CHECK_NEAR(m.NextWeight(1.0f, 1.0f), kMel.maximumNextWeight, 1e-6);
	CHECK_NEAR(m.NextWeight(1.0f, 0.0f), kMel.initialNextWeight, 1e-6);   // no anticipation: flat
	// Smooth, not a halfway switch: a quarter of the way it has barely moved.
	CHECK(m.NextWeight(0.25f, 1.0f) < 0.3f);
}

/// Probability mass on pitch classes that belong to the NEXT chord but not the
/// current one, plus approach notes into them (here: F and A, and E->F).
static float FutureMass(float phase, float antic, bool strong)
{
	MelodyEngine m;
	Rng r;
	m.Init(&kMel, &r);
	MelodyParams p;
	p.anticipation = antic;
	float w[12];
	m.PitchClassWeights(Ctx(C, Em, F, phase), p, strong, w);
	return w[5] + w[9];   // F, A (C is shared with nothing in Em)
}

TEST(melody_anticipation_grows_through_chord)
{
	// F and A are diatonic to the inferred C major, so they have a baseline
	// from the scale term whatever the anticipation setting. What anticipation
	// controls is how far that GROWS towards the chord change.
	const float early = FutureMass(0.05f, 1.0f, false);
	const float late = FutureMass(0.95f, 1.0f, false);
	const float noneEarly = FutureMass(0.05f, 0.0f, false);
	const float noneLate = FutureMass(0.95f, 0.0f, false);
	NOTE("Em with F next, weak tick: future-tone mass early %.3f -> late %.3f; anticipation 0: %.3f -> %.3f",
		early, late, noneEarly, noneLate);
	CHECK(late > 1.7f * early);
	CHECK(late > 1.7f * noneLate);
	CHECK_NEAR(noneLate, noneEarly, 0.01);   // no anticipation: no growth
}

TEST(melody_anticipations_prefer_weak_ticks)
{
	const float weak = FutureMass(0.9f, 1.0f, false);
	const float strong = FutureMass(0.9f, 1.0f, true);
	NOTE("late in Em: future mass weak tick %.3f, strong tick %.3f", weak, strong);
	CHECK(weak > 1.5f * strong);
}

TEST(melody_current_chord_keeps_presence)
{
	MelodyEngine m;
	Rng r;
	m.Init(&kMel, &r);
	MelodyParams p;
	p.anticipation = 1.0f;
	for (float phase : { 0.0f, 0.5f, 0.99f })
		for (bool strong : { false, true })
		{
			float w[12];
			m.PitchClassWeights(Ctx(C, Em, F, phase), p, strong, w);
			float cur = w[4] + w[7] + w[11];
			float sum = 0.0f;
			for (float x : w) sum += x;
			CHECK_NEAR(sum, 1.0, 1e-4);
			CHECK(cur >= kMel.curFloor - 1e-4f);
		}
}

TEST(melody_e_to_f_semitone_approach)
{
	// Late in Em before F, E (a current chord tone AND a semitone below F)
	// should outweigh G (a current chord tone that is not an approach).
	MelodyEngine m;
	Rng r;
	m.Init(&kMel, &r);
	MelodyParams p;
	p.anticipation = 1.0f;
	float w[12];
	m.PitchClassWeights(Ctx(C, Em, F, 0.9f), p, false, w);
	NOTE("late Em->F weights: E %.3f G %.3f B %.3f F %.3f A %.3f C %.3f", w[4], w[7], w[11], w[5], w[9], w[0]);
	CHECK(w[4] > w[7]);
	// Early on, the future barely shows.
	m.PitchClassWeights(Ctx(C, Em, F, 0.05f), p, false, w);
	CHECK(w[5] < w[4] * 0.4f);
}

TEST(melody_lines_are_mostly_stepwise_and_chordal)
{
	PerformParams p;
	p.density = 0.6f;
	EngineConfig cfg;
	Conductor c;
	c.Init(&cfg, 99);
	c.SetParams(p);
	c.Reset(0);
	int notes = 0, steps = 0, leaps = 0, chordTones = 0, last = -1, repeats = 0, inRange = 0;
	int strongNotes = 0, strongChord = 0;
	for (int t = 0; t < kTicksPerChord * 400; t++)
	{
		const uint32_t tick = c.NextTick();
		const TickPlan pl = c.PlanTick();
		if (pl.melAction == kMelOn || pl.melAction == kMelLegato)
		{
			const int n = pl.melNote;
			notes++;
			inRange += n >= cfg.melody.low && n <= cfg.melody.high;
			const bool ct = c.Harmony().Current().Contains(n % 12);
			chordTones += ct;
			if ((tick % 8) == 0) { strongNotes++; strongChord += ct; }
			if (last >= 0)
			{
				const int d = std::abs(n - last);
				steps += d >= 1 && d <= 2;
				leaps += d > 4;
				repeats += d == 0;
			}
			last = n;
		}
	}
	NOTE("%d notes: %d%% steps, %d%% leaps>4, %d%% repeats, %d%% chord tones (%d%% on strong ticks)",
		notes, steps * 100 / notes, leaps * 100 / notes, repeats * 100 / notes, chordTones * 100 / notes,
		strongChord * 100 / (strongNotes ? strongNotes : 1));
	CHECK(inRange == notes);
	CHECK(steps > leaps * 2);
	CHECK(repeats * 100 / notes < 20);
	CHECK(chordTones * 100 / notes >= 45);
	CHECK(strongChord * 100 / strongNotes > chordTones * 100 / notes);
}

TEST(melody_density_controls_activity)
{
	auto onsets = [](float density) {
		PerformParams p;
		p.density = density;
		EngineConfig cfg;
		Conductor c;
		c.Init(&cfg, 5);
		c.SetParams(p);
		c.Reset(0);
		int n = 0;
		for (int t = 0; t < kTicksPerChord * 100; t++)
		{
			const TickPlan pl = c.PlanTick();
			n += pl.melAction == kMelOn;
		}
		return n;
	};
	const int lo = onsets(0.1f), mid = onsets(0.5f), hi = onsets(0.95f);
	NOTE("onsets per 100 chords: density 0.1 -> %d, 0.5 -> %d, 0.95 -> %d", lo, mid, hi);
	CHECK(lo < mid && mid < hi);
	CHECK(lo > 0);
}

TEST(melody_rhythm_grid)
{
	CHECK(MelodyEngine::GridTicks(0.0f) == 4);
	CHECK(MelodyEngine::GridTicks(0.5f) == 2);
	CHECK(MelodyEngine::GridTicks(1.0f) == 1);
	// At the eighth-note grid no note starts on an odd 16th.
	PerformParams p;
	p.rhythm = 0.5f;
	p.density = 0.9f;
	EngineConfig cfg;
	Conductor c;
	c.Init(&cfg, 6);
	c.SetParams(p);
	c.Reset(0);
	int offGrid = 0;
	for (int t = 0; t < kTicksPerChord * 50; t++)
	{
		const uint32_t tick = c.NextTick();
		const TickPlan pl = c.PlanTick();
		if (pl.melAction == kMelOn && (tick % 2) != 0) offGrid++;
	}
	CHECK(offGrid == 0);
}
