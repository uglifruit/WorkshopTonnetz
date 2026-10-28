// test_voicing.cpp — minimum-movement voice leading.

#include "test.h"
#include "engine/voicing.h"

using namespace tonnetz;

static VoicingConfig Cfg() { return VoicingConfig{}; }

static int Movement(const Voicing &a, const Voicing &b)
{
	int m = 0;
	for (int v = 0; v < 3; v++) m += std::abs(a.note[v] - b.note[v]);
	return m;
}

TEST(voicing_shared_notes_stay_put)
{
	// C major (G3 C4 E4) -> E minor keeps E and G and moves C down to B.
	const VoicingConfig cfg = Cfg();
	const Voicing c{ { 55, 60, 64 } };
	const Voicing em = VoiceLead(c, Chord(4, kMinor), cfg);
	CHECK(em.note[0] == 55);   // G stays
	CHECK(em.note[1] == 59);   // C -> B
	CHECK(em.note[2] == 64);   // E stays
	CHECK(Movement(c, em) == 1);
}

TEST(voicing_plr_move_one_voice)
{
	// Every single P/L/R from a voiced triad moves exactly one voice, by 1 or 2.
	const VoicingConfig cfg = Cfg();
	for (int i = 0; i < kNumChords; i++)
	{
		const Chord c = Chord::FromIndex(i);
		const Voicing v = VoiceLead(InitialVoicing(c, cfg), c, cfg);
		for (int op = 0; op < 3; op++)
		{
			const Voicing w = VoiceLead(v, ApplyOp(c, static_cast<Op>(op)), cfg);
			int moved = 0;
			for (int k = 0; k < 3; k++) moved += v.note[k] != w.note[k];
			CHECK(moved == 1);
			CHECK(Movement(v, w) <= 2);
		}
	}
}

TEST(voicing_stays_in_register_and_is_minimal)
{
	const VoicingConfig cfg = Cfg();
	Voicing v = InitialVoicing(Chord(0, kMajor), cfg);
	uint32_t s = 12345;
	int worstMean = 0;
	for (int step = 0; step < 2000; step++)
	{
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		const Chord c = Chord::FromIndex(static_cast<int>(s % kNumChords));
		float cost = 0.0f;
		const Voicing w = VoiceLead(v, c, cfg, &cost);
		for (int k = 0; k < 3; k++)
		{
			CHECK(w.note[k] >= cfg.low && w.note[k] <= cfg.high);
			CHECK(c.Contains(w.note[k] % 12));
		}
		CHECK(w.note[0] < w.note[1] && w.note[1] < w.note[2]);   // no crossing, no unison
		// Brute force: nothing in the register beats the chosen cost.
		int t[3];
		c.Tones(t);
		float best = 1e30f;
		for (int a = cfg.low; a <= cfg.high; a++)
			for (int b = cfg.low; b <= cfg.high; b++)
				for (int d = cfg.low; d <= cfg.high; d++)
				{
					if (!c.Contains(a % 12) || !c.Contains(b % 12) || !c.Contains(d % 12)) continue;
					if (((1 << (a % 12)) | (1 << (b % 12)) | (1 << (d % 12))) != c.Mask()) continue;
					const float k = VoicingCost(v, Voicing{ { a, b, d } }, cfg);
					if (k < best) best = k;
				}
		CHECK(cost <= best + 1e-4f);
		const int mean = (w.note[0] + w.note[1] + w.note[2]) / 3;
		if (std::abs(mean - 62) > worstMean) worstMean = std::abs(mean - 62);
		v = w;
	}
	// A RANDOM chord sequence is the worst case for drift (real progressions
	// are mostly parsimonious). 48..76 allows a mean 12 either side of 62.
	NOTE("random walk of 2000 chords: worst voicing mean offset from centre = %d semitones", worstMean);
	CHECK(worstMean <= 12);
}
