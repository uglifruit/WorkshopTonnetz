// test_tonal.cpp — scale (Window B) and tonal centre (Window C) inference.

#include "test.h"
#include "engine/tonal.h"

using namespace tonnetz;

static const TonalConfig kCfg{};

static TonalEstimator Fresh(Chord home = Chord(0, kMajor))
{
	TonalEstimator t;
	t.Init(&kCfg);
	t.SetInertia(0.5f);
	t.Reset(home);
	return t;
}

static void Play(TonalEstimator &t, std::initializer_list<Chord> chords)
{
	for (const Chord &c : chords) t.OnChordArrived(c, 1.0f);
}

static const Chord C(0, kMajor), Cm(0, kMinor), Dm(2, kMinor), E(4, kMajor), Em(4, kMinor),
	F(5, kMajor), Fm(5, kMinor), Fs(6, kMajor), G(7, kMajor), Am(9, kMinor), Ab(8, kMajor),
	Eb(3, kMajor), Bb(10, kMajor);

TEST(tonal_scale_masks)
{
	CHECK(ScaleMask(kIonian, 0) == 0xAB5);           // C D E F G A B
	CHECK(ScaleMask(kAeolian, 9) == ScaleMask(kIonian, 0));
	CHECK(ScaleMask(kDorian, 2) == ScaleMask(kIonian, 0));
	CHECK(ScaleMask(kMixolydian, 7) == ScaleMask(kIonian, 0));
	// A harmonic minor: A B C D E F G#
	const uint16_t ahm = ScaleMask(kHarmonicMinor, 9);
	CHECK(ahm == ((1 << 9) | (1 << 11) | (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 8)));
	// A melodic minor (ascending): A B C D E F# G#
	const uint16_t amm = ScaleMask(kMelodicMinor, 9);
	CHECK(amm == ((1 << 9) | (1 << 11) | (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8)));
	for (int t = 0; t < kNumScaleTypes; t++)
		for (int r = 0; r < 12; r++)
		{
			int n = 0;
			for (int pc = 0; pc < 12; pc++) n += (ScaleMask(t, r) >> pc) & 1;
			CHECK(n == 7);
		}
}

TEST(tonal_c_em_is_ambiguous)
{
	// No history beyond a neutral start: reset to a key that is NOT a
	// candidate, so the estimate comes only from the two chords.
	TonalEstimator t = Fresh(Chord(6, kMajor));
	Play(t, { C, Em });
	const float *p = t.KeyProbs();
	float top = 0.0f;
	for (int k = 0; k < kNumChords; k++) if (p[k] > top) top = p[k];
	NOTE("C -> Em: P(C)=%.2f P(G)=%.2f P(Em)=%.2f P(Am)=%.2f, top=%.2f",
		p[C.Index()], p[G.Index()], p[Em.Index()], p[Am.Index()], top);
	CHECK(top < 0.55f);
	// C major and G major collections both contain C E G B: the fits tie.
	CHECK_NEAR(t.ScaleFit(kIonian, 0), t.ScaleFit(kIonian, 7), 1e-6);
}

TEST(tonal_c_em_f_supports_c_major)
{
	TonalEstimator t = Fresh(Chord(6, kMajor));
	Play(t, { C, Em, F });
	NOTE("C -> Em -> F: scale %s on %d, fit %.2f, tonic key %d",
		ScaleTypeName(t.ScaleType()), t.ScaleRoot(), t.ScaleConfidence(), t.Tonic());
	CHECK(t.ScaleMaskNow() == ScaleMask(kIonian, 0));
	CHECK(t.ScaleConfidence() > 0.95f);
	CHECK(!t.Chromatic());
	// It now strictly beats the G major collection (F natural).
	CHECK(t.ScaleFit(kIonian, 0) > t.ScaleFit(kIonian, 7) + 0.1f);
}

TEST(tonal_am_e_is_harmonic_minor)
{
	TonalEstimator t = Fresh(Chord(9, kMinor));
	Play(t, { Am, E });
	NOTE("Am -> E: scale %s on %d, fit %.2f; tonic key %d", ScaleTypeName(t.ScaleType()), t.ScaleRoot(),
		t.ScaleConfidence(), t.Tonic());
	CHECK(t.ScaleType() == kHarmonicMinor);
	CHECK(t.ScaleRoot() == 9);
	CHECK(t.Tonic() == Am.Index());
}

TEST(tonal_borrowed_iv_keeps_c)
{
	TonalEstimator t = Fresh();
	Play(t, { C, Fm, G, C });
	NOTE("C -> Fm -> G -> C: tonic key %d (C=%d), conf %.2f, scale %s on %d",
		t.Tonic(), C.Index(), t.TonicConfidence(), ScaleTypeName(t.ScaleType()), t.ScaleRoot());
	CHECK(t.Tonic() == C.Index());
	// And even from a neutral start the cadence establishes C.
	TonalEstimator u = Fresh(Chord(6, kMajor));
	Play(u, { C, Fm, G, C });
	CHECK(u.Tonic() == C.Index());
}

TEST(tonal_chromatic_chord_does_not_reset_tonic)
{
	TonalEstimator t = Fresh();
	Play(t, { C, Am, F, G, C, Dm, G, C });
	CHECK(t.Tonic() == C.Index());
	Play(t, { Fs });   // tritone-remote chromatic intrusion
	NOTE("after F#: tonic %d, P(C)=%.2f, scale fit %.2f chromatic=%d", t.Tonic(),
		t.KeyProbs()[C.Index()], t.ScaleConfidence(), (int)t.Chromatic());
	CHECK(t.Tonic() == C.Index());
	Play(t, { Ab, Eb });   // borrowed colour
	CHECK(t.Tonic() == C.Index());
}

TEST(tonal_sustained_modulation_eventually_moves)
{
	TonalEstimator t = Fresh();
	Play(t, { C, F, G, C });
	for (int i = 0; i < 6; i++) Play(t, { Bb, Eb, F, Bb });
	NOTE("after six Bb cadences: tonic %d (Bb=%d)", t.Tonic(), Bb.Index());
	CHECK(t.Tonic() == Bb.Index());
}

TEST(tonal_inertia_slows_change)
{
	auto stepsToMove = [](float inertia) {
		TonalEstimator t;
		t.Init(&kCfg);
		t.SetInertia(inertia);
		t.Reset(C);
		for (int i = 0; i < 6; i++) t.OnChordArrived(i % 2 ? G : C, 1.0f);
		int n = 0;
		while (t.Tonic() != Bb.Index() && n < 200)
		{
			t.OnChordArrived(n % 4 == 0 ? Bb : (n % 4 == 1 ? Eb : (n % 4 == 2 ? F : Bb)), 1.0f);
			n++;
		}
		return n;
	};
	const int fast = stepsToMove(0.0f), slow = stepsToMove(1.0f);
	NOTE("chords to adopt Bb: inertia 0 -> %d, inertia 1 -> %d", fast, slow);
	CHECK(slow > fast);
}
