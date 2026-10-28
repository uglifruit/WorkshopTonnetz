// test_controls.cpp — knob curves end to end, pickup, the Down tap, CV quantising.

#include "test.h"
#include "composer.h"
#include "dsp/cardcore.h"
#include "dsp/controls.h"
#include "dsp/cvin.h"
#include "dsp/fastmath.h"
#include "engine/mapping.h"

using namespace tonnetz;

TEST(controls_glide_curve_reaches_range)
{
	CHECK(GlideSecondsFrom(0) == 0.0f);
	CHECK(GlideSecondsFrom(kGlideDeadzone - 1) == 0.0f);
	CHECK_NEAR(GlideSecondsFrom(4095), 4.0, 1e-4);
	float prev = 0.0f;
	for (int k = kGlideDeadzone; k <= 4095; k += 16)
	{
		CHECK(GlideSecondsFrom(k) >= prev);
		prev = GlideSecondsFrom(k);
	}
	NOTE("glide at 25/50/75%%: %.2f / %.2f / %.2f s", GlideSecondsFrom(1024), GlideSecondsFrom(2048), GlideSecondsFrom(3072));
	// The short end gets the travel: half the knob is under 0.5 s.
	CHECK(GlideSecondsFrom(2048) <= 0.51f);
}

TEST(controls_bpm_curve_and_isr_table)
{
	DspInit();
	Link link;
	CardCore core;
	core.Init(&link);
	CHECK_NEAR(BpmFrom(0), 40.0, 0.01);
	CHECK_NEAR(BpmFrom(4095), 240.0, 0.01);
	double worst = 0.0;
	uint32_t prev = 0;
	for (int k = 0; k <= 4095; k++)
	{
		const uint32_t q4 = core.BpmQ4(k);
		CHECK(q4 >= prev);
		prev = q4;
		const double err = std::fabs(q4 / 16.0 - BpmFrom(k)) / BpmFrom(k);
		if (err > worst) worst = err;
	}
	NOTE("ISR BPM table vs curve: worst %.3f%%; knob noon = %.1f BPM", worst * 100, core.BpmQ4(2048) / 16.0);
	CHECK(worst < 0.005);
	CHECK_NEAR(core.BpmQ4(0) / 16.0, 40.0, 0.1);
	CHECK_NEAR(core.BpmQ4(4095) / 16.0, 240.0, 0.5);
	// The default timing page sits at ~100 BPM.
	CHECK_NEAR(core.BpmQ4(2094) / 16.0, 100.0, 1.0);
}

TEST(controls_cv_attraction_and_strict)
{
	CHECK(CvAttractionFrom(0) == 0.0f);
	CHECK(!CvStrictFrom(3700));
	CHECK(CvStrictFrom(3800));
	CHECK_NEAR(CvAttractionFrom(3780), 1.0, 0.01);
	CHECK(PhraseLengthFrom(0) == 2 && PhraseLengthFrom(3) == 16);
}

/// Feed the controls a held position for n control ticks.
static void Hold(Controls &c, int32_t m, int32_t x, int32_t y, int sw, int ticks)
{
	const int32_t k[3] = { m, x, y };
	for (int i = 0; i < ticks; i++) c.Tick(k, sw);
}

TEST(controls_phrase_length_all_reachable_with_hysteresis)
{
	Controls c;
	c.Init();
	Hold(c, 0, 0, 0, Controls::kUp, 100);       // to TIMING; Y waits at its stored 2600
	for (int y = 0; y <= 4095; y += 8) Hold(c, 0, 0, y, Controls::kUp, 1);   // sweep through it to catch
	CHECK(!c.Waiting(2));
	int seenUp[4] = {}, seenDown[4] = {};
	for (int y = 0; y <= 4095; y += 8) { Hold(c, 0, 0, y, Controls::kUp, 2); seenUp[c.PhraseIndex()]++; }
	for (int y = 4095; y >= 0; y -= 8) { Hold(c, 0, 0, y, Controls::kUp, 2); seenDown[c.PhraseIndex()]++; }
	for (int i = 0; i < 4; i++) CHECK(seenUp[i] > 0 && seenDown[i] > 0);
	// Jitter across a boundary does not flicker.
	Hold(c, 0, 0, 2048 + 100, Controls::kUp, 10);
	const int idx = c.PhraseIndex();
	int flips = 0;
	for (int i = 0; i < 200; i++)
	{
		Hold(c, 0, 0, 2048 + ((i & 1) ? 30 : -30), Controls::kUp, 1);
		flips += c.PhraseIndex() != idx;
	}
	CHECK(flips == 0);
}

TEST(controls_pickup_protects_other_pages)
{
	Controls c;
	c.Init();
	Hold(c, 3000, 3000, 3000, Controls::kMiddle, 100);   // PERFORM live from boot
	CHECK(c.Value(Controls::kPerform, 0) == 3000);
	// Flip to TIMING: the knobs are far from its stored values, so they wait.
	Hold(c, 3000, 3000, 3000, Controls::kUp, 200);
	CHECK(c.CurrentPage() == Controls::kTiming);
	CHECK(c.Waiting(0) && c.Waiting(1) && c.Waiting(2));
	const int32_t bpmBefore = c.Value(Controls::kTiming, 0);
	Hold(c, 2900, 3000, 3000, Controls::kUp, 10);          // a nudge does nothing yet
	CHECK(c.Value(Controls::kTiming, 0) == bpmBefore);
	// Sweep Main down through the stored value: it catches and follows.
	for (int m = 2900; m >= 1000; m -= 20) Hold(c, m, 3000, 3000, Controls::kUp, 1);
	CHECK(!c.Waiting(0));
	CHECK(c.Value(Controls::kTiming, 0) == 1000);
	// PERFORM was untouched while we were away, and waits again on return.
	CHECK(c.Value(Controls::kPerform, 0) == 3000);
	Hold(c, 1000, 3000, 3000, Controls::kMiddle, 200);
	CHECK(c.CurrentPage() == Controls::kPerform);
	CHECK(c.Waiting(0));
	CHECK(!c.Waiting(1));   // X never moved, so it is already at its value
	CHECK(c.Value(Controls::kPerform, 0) == 3000);
}

TEST(controls_down_tap_resets_down_hold_edits)
{
	Controls c;
	c.Init();
	Hold(c, 2000, 2000, 2000, Controls::kMiddle, 100);
	// Tap: 150 ms, nothing moved.
	Hold(c, 2000, 2000, 2000, Controls::kDown, 450);
	CHECK(c.CurrentPage() == Controls::kAdvanced);
	Hold(c, 2000, 2000, 2000, Controls::kMiddle, 100);
	CHECK(c.TakeReset());
	CHECK(!c.TakeReset());   // once
	CHECK(c.CurrentPage() == Controls::kPerform);
	// Held with a knob turned: an edit, not a reset.
	Hold(c, 2000, 2000, 2000, Controls::kDown, 100);
	for (int x = 2000; x <= 3000; x += 10) Hold(c, 2000, x, 2000, Controls::kDown, 1);
	Hold(c, 2000, 3000, 2000, Controls::kMiddle, 100);
	CHECK(!c.TakeReset());
	// Held long and still: also not a tap.
	Hold(c, 2000, 3000, 2000, Controls::kDown, 3000);
	Hold(c, 2000, 3000, 2000, Controls::kMiddle, 100);
	CHECK(!c.TakeReset());
}

TEST(controls_cv_pitch_quantiser)
{
	// 1 V = 341.33 counts = 12 semitones; one semitone = 28.44 counts.
	auto settle = [](CvPitch &q, int32_t counts) { for (int i = 0; i < 100; i++) q.Process(counts); return q.Semitone(); };
	CvPitch q;
	CHECK(settle(q, 0) == 0);
	CHECK(settle(q, 341) == 12);       // 1 V -> C one octave up
	CHECK(settle(q, 114) == 4);        // E
	CHECK(settle(q, -341) == -12);
	CHECK(PitchClassFromSemitone(-12) == 0);
	CHECK(PitchClassFromSemitone(-1) == 11);
	for (int s = -24; s <= 36; s++)
	{
		const int32_t counts = static_cast<int32_t>(std::lround(s * 341.333 / 12.0));
		CHECK(settle(q, counts) == s);
	}
	// ADC jitter (+-6 counts, +-0.2 semitone) sitting right on a semitone
	// boundary does not flip it: the hysteresis band is +-0.75 semitone.
	settle(q, 100);                    // 3.52 semitones -> 4
	const int32_t held = q.Semitone();
	int flips = 0;
	for (int i = 0; i < 1000; i++) { q.Process(100 + ((i * 7) % 13) - 6); flips += q.Semitone() != held; }
	CHECK(flips == 0);
	// A deliberate move of a whole semitone does register.
	CHECK(settle(q, 100 - 28) == held - 1);
}

TEST(controls_composer_mapping_end_to_end)
{
	int32_t raw[kNumParams] = {};
	raw[kParamWander] = 0;
	raw[kParamGlide] = 4095;
	raw[kParamAntic] = 4095;
	raw[kParamDensity] = 0;
	raw[kParamPhraseIndex] = 3;
	raw[kParamCvAttract] = 4095;
	raw[kParamCvSemitone] = -8;
	raw[kParamCvActive] = 1;
	raw[kParamCv2] = 1707;            // +5 V on CV In 2
	const PerformParams p = Composer::Map(raw);
	CHECK_NEAR(p.wander, 1.0, 1e-3);  // CV 2 alone sweeps the whole wander range
	CHECK_NEAR(p.glideSeconds, 4.0, 1e-3);
	CHECK_NEAR(p.anticipation, 1.0, 1e-3);
	CHECK(p.phraseLength == 16);
	CHECK(p.cvStrict && p.cvActive);
	CHECK(p.cvPc == 4);               // -8 semitones = E
	raw[kParamCv2] = -1707;
	raw[kParamWander] = 4095;
	CHECK_NEAR(Composer::Map(raw).wander, 0.0, 1e-3);
}
