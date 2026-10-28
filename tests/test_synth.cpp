// test_synth.cpp — oscillators, glide, envelope and output levels.

#include "test.h"
#include "dsp/fastmath.h"
#include "dsp/synth.h"

using namespace tonnetz;

static double IncToHz(uint32_t inc) { return static_cast<double>(inc) * 48000.0 / 4294967296.0; }

TEST(synth_pitch_table_accuracy)
{
	DspInit();
	CHECK_NEAR(IncToHz(PitchToInc(69 << 16)), 440.0, 0.01);
	CHECK_NEAR(IncToHz(PitchToInc(60 << 16)), 261.6256, 0.01);
	double worst = 0.0;
	for (int32_t p = 36 << 16; p <= 90 << 16; p += 997)
	{
		const double want = 440.0 * std::pow(2.0, (p / 65536.0 - 69.0) / 12.0);
		const double cents = 1200.0 * std::log2(IncToHz(PitchToInc(p)) / want);
		if (std::fabs(cents) > worst) worst = std::fabs(cents);
	}
	NOTE("worst interpolation error between semitones: %.3f cents", worst);
	CHECK(worst < 1.0);
}

TEST(synth_glide_time_and_landing)
{
	DspInit();
	Synth s;
	s.Init();
	const uint8_t a[3] = { 48, 52, 55 }, b[3] = { 60, 64, 67 };
	s.SetChord(a, 0);
	s.Control();
	CHECK(s.ChordPitch(0) == (48 << 16));           // zero glide: immediate
	s.SetChord(b, 48000);                            // one second
	int blocks = 0;
	while (s.ChordPitch(0) != (60 << 16) && blocks < 10000) { s.Control(); blocks++; }
	CHECK(blocks == 48000 / kControlBlock);          // arrives exactly on time
	CHECK(s.ChordPitch(1) == (64 << 16) && s.ChordPitch(2) == (67 << 16));
}

TEST(synth_retarget_mid_glide_is_continuous)
{
	DspInit();
	Synth s;
	s.Init();
	const uint8_t c1[3] = { 48, 52, 55 }, c2[3] = { 72, 76, 79 }, c3[3] = { 50, 53, 57 };
	s.SetChord(c1, 0);
	s.Control();
	s.SetChord(c2, 4 * 48000);
	int32_t prev = s.ChordPitch(0), maxStep = 0;
	for (int i = 0; i < 2000; i++) { s.Control(); maxStep = std::max(maxStep, std::abs(s.ChordPitch(0) - prev)); prev = s.ChordPitch(0); }
	const int32_t midway = s.ChordPitch(0);
	CHECK(midway > (48 << 16) && midway < (72 << 16));
	// A new chord arrives mid-glide: the next step starts from where it IS.
	s.SetChord(c3, 2 * 48000);
	s.Control();
	CHECK(std::abs(s.ChordPitch(0) - midway) <= maxStep + 1);
	for (int i = 0; i < 7000; i++) { s.Control(); maxStep = std::max(maxStep, std::abs(s.ChordPitch(0) - prev)); prev = s.ChordPitch(0); }
	CHECK(s.ChordPitch(0) == (50 << 16));
	NOTE("largest pitch step per control block during 4 s glides: %.4f semitones", maxStep / 65536.0);
	CHECK(maxStep < (1 << 16) / 50);   // well under 2 cents per 1/3 ms
}

/// Largest sample-to-sample change and peak over n samples of a chord.
static void Measure(Synth &s, int n, int &maxJump, int &peak, double &rms, bool melody)
{
	int prev = melody ? s.RenderMelody() : s.RenderChord();
	maxJump = 0;
	peak = 0;
	double acc = 0.0;
	for (int i = 0; i < n; i++)
	{
		if ((i & 15) == 8) s.Control();
		const int x = melody ? s.RenderMelody() : s.RenderChord();
		maxJump = std::max(maxJump, std::abs(x - prev));
		peak = std::max(peak, std::abs(x));
		acc += static_cast<double>(x) * x;
		prev = x;
	}
	rms = std::sqrt(acc / n);
}

TEST(synth_chord_changes_do_not_click)
{
	DspInit();
	Synth s;
	s.Init();
	int worstJump = 0, worstPeak = 0;
	double rmsSum = 0.0;
	uint32_t r = 99;
	for (int k = 0; k < 60; k++)
	{
		uint8_t n[3];
		for (int v = 0; v < 3; v++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; n[v] = static_cast<uint8_t>(48 + v * 9 + r % 9); }
		s.SetChord(n, 0);   // instantaneous: the hardest case
		int j, p;
		double rms;
		Measure(s, 4000, j, p, rms, false);
		worstJump = std::max(worstJump, j);
		worstPeak = std::max(worstPeak, p);
		rmsSum += rms;
	}
	NOTE("chord out over 60 instant changes: peak %d, mean rms %.0f, largest sample step %d", worstPeak, rmsSum / 60, worstJump);
	CHECK(worstPeak < 2047);          // never touches the rail
	CHECK(rmsSum / 60 > 400.0);       // and is not timid
	CHECK(worstJump < 260);           // phase-continuous: no step beyond the waveform's own slope
}

TEST(synth_melody_envelope_is_click_free)
{
	DspInit();
	Synth s;
	s.Init();
	int j, p;
	double rms;
	s.MelodyOn(72, false);
	Measure(s, 2000, j, p, rms, true);
	const int attackJump = j;
	s.MelodyOn(79, false);           // retrigger while sounding
	Measure(s, 2000, j, p, rms, true);
	const int retrigJump = j;
	const int peakSustain = p;
	s.MelodyOn(77, true);            // legato
	Measure(s, 2000, j, p, rms, true);
	s.MelodyOff();
	Measure(s, 48000, j, p, rms, true);
	NOTE("melody: attack step %d, retrigger step %d, peak %d", attackJump, retrigJump, peakSustain);
	CHECK(attackJump < 250);
	CHECK(retrigJump < 250);
	CHECK(peakSustain < 2047 && peakSustain > 1000);
	CHECK(s.MelodyEnvelope() == 0);  // release reaches silence
	CHECK(s.RenderMelody() == 0);
}

TEST(synth_all_four_oscillators_run)
{
	DspInit();
	Synth s;
	s.Init();
	const uint8_t n[3] = { 52, 59, 64 };
	s.SetChord(n, 0);
	s.MelodyOn(76, false);
	int j, p;
	double rmsChord, rmsMel;
	Measure(s, 9600, j, p, rmsChord, false);
	Measure(s, 9600, j, p, rmsMel, true);
	CHECK(rmsChord > 300.0);
	CHECK(rmsMel > 300.0);
}
