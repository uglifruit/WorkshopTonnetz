// render.cpp — play the whole card on the host: a WAV to listen to, plus statistics.
//
//   render <out.wav> [seconds] [wander] [glide] [anticipation] [density] [seed]
//
// Knob arguments are 0..1 (glide is the knob position, not seconds). The WAV
// is stereo: LEFT = Audio Out 1 (chord), RIGHT = Audio Out 2 (melody). It is
// exactly what the card's DAC would receive, scaled from 12 to 16 bits.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "sim.h"
#include "engine/mapping.h"

using namespace tonnetz;

struct Stats
{
	std::vector<Chord> chords;
	std::vector<int>   tonic;          // inferred tonic key at each chord
	std::vector<int>   scale;          // type*12+root at each chord
	std::vector<bool>  chromatic;
	std::vector<Chord> home;
	int voiceMove = 0;
	uint8_t lastVoicing[3] = { 0, 0, 0 };
	bool haveVoicing = false;
	int notes = 0, chordTones = 0, steps = 0, leaps = 0, repeats = 0, last = -1;
	int anticipations = 0, lateNotes = 0;
};

static void OnPlan(const TickPlan &p, const Conductor &c, void *v)
{
	Stats &s = *static_cast<Stats *>(v);
	const HarmonyEngine &h = c.Harmony();
	if (p.flags & kPlanChordChange)
	{
		s.chords.push_back(h.Current());
		s.tonic.push_back(c.Tonal().Tonic());
		s.scale.push_back(c.Tonal().ScaleType() * 12 + c.Tonal().ScaleRoot());
		s.chromatic.push_back(c.Tonal().Chromatic());
		s.home.push_back(h.Home());
		if (s.haveVoicing)
			for (int i = 0; i < 3; i++) s.voiceMove += std::abs(p.chord[i] - s.lastVoicing[i]);
		std::memcpy(s.lastVoicing, p.chord, 3);
		s.haveVoicing = true;
	}
	if (p.melAction == kMelOn || p.melAction == kMelLegato)
	{
		const int n = p.melNote;
		s.notes++;
		const bool ct = h.Current().Contains(n % 12);
		s.chordTones += ct;
		const uint32_t tic = p.tick % kTicksPerChord;
		if (tic >= kTicksPerChord * 3 / 4)
		{
			s.lateNotes++;
			s.anticipations += !ct && h.Next().Contains(n % 12);
		}
		if (s.last >= 0)
		{
			const int d = std::abs(n - s.last);
			s.steps += d >= 1 && d <= 2;
			s.leaps += d > 4;
			s.repeats += d == 0;
		}
		s.last = n;
	}
}

static void WriteWav(const char *path, const std::vector<int16_t> &lr)
{
	FILE *f = std::fopen(path, "wb");
	if (!f) { std::printf("cannot write %s\n", path); return; }
	const uint32_t dataBytes = static_cast<uint32_t>(lr.size() * 2);
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
	std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32(dataBytes);
	std::fwrite(lr.data(), 2, lr.size(), f);
	std::fclose(f);
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::printf("render <out.wav> [seconds] [wander] [glide] [anticipation] [density] [seed]\n");
		return 1;
	}
	const char *out = argv[1];
	const double seconds = argc > 2 ? std::atof(argv[2]) : 120.0;
	const double wander  = argc > 3 ? std::atof(argv[3]) : 0.3;
	const double glide   = argc > 4 ? std::atof(argv[4]) : 0.4;
	const double antic   = argc > 5 ? std::atof(argv[5]) : 0.5;
	const double density = argc > 6 ? std::atof(argv[6]) : -1.0;
	const uint32_t seed  = argc > 7 ? static_cast<uint32_t>(std::atoi(argv[7])) : 1;

	Sim sim(seed);
	Stats st;
	sim.composer.onPlan = OnPlan;
	sim.composer.onPlanCtx = &st;
	sim.in.knob[0] = static_cast<int32_t>(wander * 4095.0);
	sim.in.knob[1] = static_cast<int32_t>(glide * 4095.0);
	sim.in.knob[2] = static_cast<int32_t>(antic * 4095.0);

	if (density >= 0.0)
	{
		// Visit the TIMING page and sweep X through its stored value so pickup
		// catches, then park it at the requested density.
		sim.in.sw = 2;
		sim.Run(400);
		for (int k = 0; k <= 4095; k += 8) { sim.in.knob[1] = k; sim.Run(16); }
		sim.in.knob[1] = static_cast<int32_t>(density * 4095.0);
		sim.Run(400);
		sim.in.sw = 1;
		sim.Run(2000);   // past kPageSettle, or the sweep below would still be editing TIMING
		// Back on PERFORM, X must catch its old value again: sweep it.
		for (int k = 0; k <= 4095; k += 8) { sim.in.knob[1] = k; sim.Run(16); }
		sim.in.knob[1] = static_cast<int32_t>(glide * 4095.0);
		sim.Run(400);
	}

	const uint64_t n = static_cast<uint64_t>(seconds * 48000.0);
	std::vector<int16_t> lr;
	lr.reserve(n * 2);
	double acc1 = 0, acc2 = 0;
	int peak1 = 0, peak2 = 0;
	for (uint64_t i = 0; i < n; i++)
	{
		sim.Step();
		lr.push_back(static_cast<int16_t>(sim.out.audio1 * 16));
		lr.push_back(static_cast<int16_t>(sim.out.audio2 * 16));
		acc1 += static_cast<double>(sim.out.audio1) * sim.out.audio1;
		acc2 += static_cast<double>(sim.out.audio2) * sim.out.audio2;
		peak1 = std::max(peak1, std::abs(static_cast<int>(sim.out.audio1)));
		peak2 = std::max(peak2, std::abs(static_cast<int>(sim.out.audio2)));
	}
	WriteWav(out, lr);

	// --- report ---
	char nb[8];
	const int nc = static_cast<int>(st.chords.size());
	std::printf("%s  %.0f s  wander %.2f  glide %.2f s  antic %.2f  seed %u\n", out, seconds, wander,
		GlideSecondsFrom(static_cast<int32_t>(glide * 4095.0)), antic, seed);
	std::printf("  out1 rms %.0f peak %d | out2 rms %.0f peak %d | plans late %u\n",
		std::sqrt(acc1 / n), peak1, std::sqrt(acc2 / n), peak2, sim.core.PlansLate());

	std::printf("  first chords:");
	for (int i = 0; i < nc && i < 32; i++) std::printf(" %s", ChordName(st.chords[i], nb));
	std::printf("\n");

	int hist[kNumChords] = {}, aba = 0, distinct = 0, diatonic = 0, tonicChanges = 0, scaleChanges = 0, chrom = 0;
	double dist = 0.0;
	for (int i = 0; i < nc; i++)
	{
		hist[st.chords[i].Index()]++;
		dist += Distance(st.chords[i], st.home[i]);
		const uint16_t key = (ScaleMask(st.home[i].IsMinor() ? kAeolian : kIonian, st.home[i].root));
		diatonic += (st.chords[i].Mask() & ~key) == 0;
		if (i >= 2) aba += st.chords[i] == st.chords[i - 2];
		if (i >= 1) { tonicChanges += st.tonic[i] != st.tonic[i - 1]; scaleChanges += st.scale[i] != st.scale[i - 1]; }
		chrom += st.chromatic[i];
	}
	for (int c : hist) distinct += c > 0;
	std::printf("  %d chords, %d distinct; mean Tonnetz distance from home %.2f; %d%% diatonic to home; A-B-A %d%%\n",
		nc, distinct, nc ? dist / nc : 0.0, nc ? diatonic * 100 / nc : 0, nc > 2 ? aba * 100 / (nc - 2) : 0);
	std::printf("  mean voice movement per change %.2f semitones (3 voices)\n", nc > 1 ? static_cast<double>(st.voiceMove) / (nc - 1) : 0.0);
	std::printf("  tonic changes %d, scale changes %d, chromatic (no scale fits) %d%% of chords\n",
		tonicChanges, scaleChanges, nc ? chrom * 100 / nc : 0);
	int homeChanges = 0;
	for (int i = 1; i < nc; i++) homeChanges += st.home[i] != st.home[i - 1];
	std::printf("  home migrations %d (final home %s)\n", homeChanges, nc ? ChordName(st.home[nc - 1], nb) : "-");
	std::printf("  top chords:");
	for (int k = 0; k < 8; k++)
	{
		int best = 0;
		for (int i = 1; i < kNumChords; i++) if (hist[i] > hist[best]) best = i;
		if (hist[best] == 0) break;
		std::printf(" %s %d%%", ChordName(Chord::FromIndex(best), nb), hist[best] * 100 / nc);
		hist[best] = -1;
	}
	std::printf("\n");
	if (st.notes)
		std::printf("  melody %d notes (%.1f per chord): %d%% chord tones, %d%% steps, %d%% leaps>4, %d%% repeats; "
			"late-in-chord notes that anticipate the next chord %d%%\n",
			st.notes, static_cast<double>(st.notes) / (nc ? nc : 1), st.chordTones * 100 / st.notes,
			st.steps * 100 / st.notes, st.leaps * 100 / st.notes, st.repeats * 100 / st.notes,
			st.lateNotes ? st.anticipations * 100 / st.lateNotes : 0);
	return 0;
}
