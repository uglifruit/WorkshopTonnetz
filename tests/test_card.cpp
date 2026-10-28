// test_card.cpp — the whole card, ISR and composer together, from the jacks.

#include "test.h"
#include "sim.h"

using namespace tonnetz;

TEST(card_plays_unpatched_from_boot)
{
	Sim sim(5);
	double acc1 = 0, acc2 = 0;
	int peak1 = 0, peak2 = 0, chordTrigs = 0, noteTrigs = 0;
	bool lastP1 = false, lastP2 = false;
	const int n = 48000 * 20;
	for (int i = 0; i < n; i++)
	{
		sim.Step();
		acc1 += static_cast<double>(sim.out.audio1) * sim.out.audio1;
		acc2 += static_cast<double>(sim.out.audio2) * sim.out.audio2;
		peak1 = std::max(peak1, std::abs(static_cast<int>(sim.out.audio1)));
		peak2 = std::max(peak2, std::abs(static_cast<int>(sim.out.audio2)));
		chordTrigs += sim.out.pulse1 && !lastP1;
		noteTrigs += sim.out.pulse2 && !lastP2;
		lastP1 = sim.out.pulse1;
		lastP2 = sim.out.pulse2;
	}
	const double rms1 = std::sqrt(acc1 / n), rms2 = std::sqrt(acc2 / n);
	NOTE("20 s unpatched: out1 rms %.0f peak %d, out2 rms %.0f peak %d; %d chord / %d note triggers; plans %u on time, %u late",
		rms1, peak1, rms2, peak2, chordTrigs, noteTrigs, sim.core.PlansOnTime(), sim.core.PlansLate());
	CHECK(sim.core.Started());
	CHECK(rms1 > 300 && rms2 > 100);
	CHECK(peak1 < 2047 && peak2 < 2047);
	// 100 BPM, 4 beats per chord: 2.4 s per chord -> 8 or 9 changes in 20 s.
	CHECK(chordTrigs >= 8 && chordTrigs <= 10);
	CHECK(noteTrigs > 20);
	CHECK(sim.core.PlansLate() == 0);
}

TEST(card_reset_returns_home_on_a_beat)
{
	Sim sim(6);
	sim.in.knob[0] = 4095;    // wander far from home first
	sim.RunSeconds(30);
	sim.in.pulse2Rise = true;
	sim.Step();
	const uint32_t epoch = sim.core.Epoch();
	CHECK(epoch == 1);
	sim.RunSeconds(1.0);      // the next beat is at most 0.6 s away
	// The chord voices are heading for a C major triad.
	const Conductor &c = sim.composer.GetConductor();
	CHECK(c.Harmony().Current() == Chord(0, kMajor));
	CHECK(c.Harmony().PhrasePos() == 0);
	CHECK(sim.composer.GetConductor().NextTick() > sim.core.DueTick());
	// After the reset, chords still change on beats: the restart tick is a
	// multiple of four.
	CHECK((sim.link.restartTick & 3u) == 0);
}

TEST(card_down_tap_is_a_reset)
{
	Sim sim(7);
	sim.RunSeconds(5);
	sim.in.sw = 0;
	sim.RunSeconds(0.15);
	sim.in.sw = 1;
	sim.RunSeconds(0.1);
	CHECK(sim.core.Epoch() == 1);
}

TEST(card_external_clock_drives_chords)
{
	Sim sim(8);
	sim.in.pulse1Patched = true;
	// 150 BPM quarter-note pulses for 20 s.
	const int period = 19200;
	int chordChanges = 0, onEdge = 0;
	bool lastP1 = false;
	for (int i = 0; i < 48000 * 20; i++)
	{
		const bool edge = (i % period) == 100;
		sim.in.pulse1Rise = edge;
		sim.core.Process(sim.in, sim.out);
		sim.composer.Step();
		if (sim.out.pulse1 && !lastP1)
		{
			chordChanges++;
			// Applied within a few samples of the edge (the composer is
			// stepped after the ISR, so allow one sample).
			if ((i % period) >= 100 && (i % period) <= 102) onEdge++;
		}
		lastP1 = sim.out.pulse1;
	}
	NOTE("external 150 BPM: %d chord changes, %d on a pulse edge; state %d; late plans %u",
		chordChanges, onEdge, (int)sim.core.GetClock().GetState(), sim.core.PlansLate());
	CHECK(sim.core.GetClock().GetState() == ClockEngine::State::External);
	// 50 beats / 4 = 12 chords in 20 s.
	CHECK(chordChanges >= 11 && chordChanges <= 13);
	CHECK(onEdge >= chordChanges - 2);
}

TEST(card_cv_in_steers_the_harmony)
{
	Sim sim(9);
	sim.in.sw = 0;            // hold Down: advanced page
	sim.Run(500);
	for (int k = 2048; k <= 4095; k += 8) { sim.in.knob[0] = k; sim.Run(16); }   // CV pull to max (strict)
	sim.in.sw = 1;
	sim.Run(500);
	sim.in.knob[0] = 1200;    // perform Main back where it was, so pickup re-catches
	sim.in.cv1Patched = true;
	sim.in.cv1 = 171;         // +0.5 V = F# (semitone 6)
	sim.RunSeconds(5);
	int hits = 0, chords = 0;
	Chord last = sim.composer.GetConductor().Harmony().Current();
	for (int i = 0; i < 48000 * 60; i++)
	{
		sim.Step();
		const Chord c = sim.composer.GetConductor().Harmony().Current();
		if (c != last) { chords++; hits += c.Contains(6); last = c; }
	}
	NOTE("CV = F#, strict: %d of %d chords contain F#", hits, chords);
	CHECK(chords > 15);
	CHECK(hits * 100 / chords >= 80);
}

TEST(card_glide_knob_reaches_plans)
{
	Sim sim(10);
	struct Ctx { uint32_t maxGlide = 0, minGlide = 0xFFFFFFFF; } ctx;
	sim.composer.onPlan = [](const TickPlan &p, const Conductor &, void *v) {
		Ctx *c = static_cast<Ctx *>(v);
		if (p.flags & kPlanChordChange)
		{
			if (p.glideSamples > c->maxGlide) c->maxGlide = p.glideSamples;
			if (p.glideSamples < c->minGlide) c->minGlide = p.glideSamples;
		}
	};
	sim.composer.onPlanCtx = &ctx;
	sim.in.knob[1] = 4095;
	sim.RunSeconds(10);
	CHECK_NEAR(ctx.maxGlide / 48000.0, 4.0, 0.01);
	ctx.minGlide = 0xFFFFFFFF;
	sim.in.knob[1] = 0;
	sim.RunSeconds(10);
	CHECK(ctx.minGlide == 0);
}

TEST(card_survives_a_slow_composer)
{
	// Core 1 only gets to run every 20000 samples (0.42 s) — nearly three 16ths
	// at the default tempo, beyond the 2-tick lookahead. Plans arrive late; the
	// harmony must still move and the melody must never hang on one note.
	Sim sim(12);
	int chordChanges = 0, longestHeld = 0, held = 0;
	bool lastP1 = false, lastP2 = false;
	int32_t lastPitch = 0;
	for (int i = 0; i < 48000 * 30; i++)
	{
		sim.core.Process(sim.in, sim.out);
		if (i % 20000 == 0) sim.composer.Step();
		chordChanges += sim.out.pulse1 && !lastP1;
		lastP1 = sim.out.pulse1;
		// Time one note has been gated with no new note event.
		const int32_t pitch = sim.core.GetSynth().MelodyPitch();
		const bool event = (sim.out.pulse2 && !lastP2) || pitch != lastPitch;
		lastP2 = sim.out.pulse2;
		lastPitch = pitch;
		held = (sim.core.GetSynth().MelodyGate() && !event) ? held + 1 : 0;
		longestHeld = std::max(longestHeld, held);
	}
	NOTE("slow composer: %d chord changes, %u plans late, %u on time, longest single note %.2f s",
		chordChanges, sim.core.PlansLate(), sim.core.PlansOnTime(), longestHeld / 48000.0);
	CHECK(sim.core.PlansLate() > 0);           // the scenario really is late
	CHECK(chordChanges >= 10);                 // ~12 at 100 BPM in 30 s
	CHECK(longestHeld < 48000 * 4);            // no note hangs much past a chord
}

TEST(card_long_glides_stay_continuous_over_many_chords)
{
	Sim sim(11);
	sim.in.knob[1] = 4095;   // 4 s glides, chords every 2.4 s: always mid-glide
	sim.in.knob[0] = 3500;
	int32_t prev[3];
	for (int v = 0; v < 3; v++) prev[v] = sim.core.GetSynth().ChordPitch(v);
	int32_t maxStep = 0;
	for (int i = 0; i < 48000 * 60; i++)
	{
		sim.Step();
		for (int v = 0; v < 3; v++)
		{
			const int32_t p = sim.core.GetSynth().ChordPitch(v);
			maxStep = std::max(maxStep, std::abs(p - prev[v]));
			prev[v] = p;
		}
	}
	NOTE("60 s of 4 s glides: largest pitch step in one control block %.4f semitones", maxStep / 65536.0);
	CHECK(maxStep < 65536 / 100);
}
