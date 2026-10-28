// test_clock.cpp — internal tempo, external sync, loss and recovery.

#include "test.h"
#include "dsp/clock.h"
#include <vector>

using namespace tonnetz;

struct TickLog { std::vector<uint64_t> at; std::vector<bool> beat; };

/// Runs the clock; `edgeAt` lists samples with a rising edge on Pulse In 1.
static TickLog Drive(ClockEngine &c, uint64_t samples, const std::vector<uint64_t> &edgeAt, bool patched = true,
	uint64_t start = 0)
{
	TickLog log;
	size_t e = 0;
	for (uint64_t t = start; t < start + samples; t++)
	{
		bool edge = false;
		while (e < edgeAt.size() && edgeAt[e] < t) e++;
		if (e < edgeAt.size() && edgeAt[e] == t) edge = true;
		if (c.Process(edge, patched)) { log.at.push_back(t); log.beat.push_back(c.Beat()); }
	}
	return log;
}

TEST(clock_internal_tempo)
{
	ClockEngine c;
	c.Init(ClockEngine::PeriodQ8FromBpmQ4(120 * 16));   // 6000 samples per 16th
	TickLog log = Drive(c, 48000 * 4, {}, false);
	CHECK(log.at.size() == 32);
	for (size_t i = 1; i < log.at.size(); i++) CHECK(log.at[i] - log.at[i - 1] == 6000);
	// The transport fired tick 0 at start; every 4th tick after it is a beat.
	for (size_t i = 0; i < log.beat.size(); i++) CHECK(log.beat[i] == ((i + 1) % 4 == 0));
	// Fractional periods do not drift: 7 BPM-Q4 units off a round number.
	ClockEngine d;
	d.Init(ClockEngine::PeriodQ8FromBpmQ4(97 * 16 + 7));
	TickLog l2 = Drive(d, 48000 * 60, {}, false);
	const double expected = 60.0 * (97.0 + 7.0 / 16.0) * 4.0 / 60.0;
	CHECK_NEAR(static_cast<double>(l2.at.size()), expected, 1.0);
}

TEST(clock_external_sync_four_ticks_per_pulse)
{
	ClockEngine c;
	c.Init(ClockEngine::PeriodQ8FromBpmQ4(100 * 16));
	std::vector<uint64_t> edges;
	for (int i = 0; i < 20; i++) edges.push_back(1000 + static_cast<uint64_t>(i) * 24000);   // 120 BPM
	TickLog log = Drive(c, 1000 + 20 * 24000, edges);
	CHECK(c.GetState() == ClockEngine::State::External);
	// After the first few beats every edge is a beat tick and three 16ths follow.
	int beatsOnEdges = 0;
	for (size_t i = 0; i < log.at.size(); i++)
		if (log.beat[i])
			for (uint64_t e : edges) if (e == log.at[i]) beatsOnEdges++;
	NOTE("%zu ticks, %d beat ticks on pulse edges", log.at.size(), beatsOnEdges);
	CHECK(beatsOnEdges >= 19);
	// Steady state: 16ths evenly spaced inside a beat.
	for (size_t i = 40; i + 1 < log.at.size(); i++)
		CHECK(log.at[i + 1] - log.at[i] >= 5990 && log.at[i + 1] - log.at[i] <= 6010);
}

TEST(clock_no_double_tick_on_switch_to_external)
{
	// Internal at 120 BPM; the first external edge lands 100 samples after an
	// internal beat. It must be taken as that beat, not a second one.
	ClockEngine c;
	c.Init(ClockEngine::PeriodQ8FromBpmQ4(120 * 16));
	TickLog a = Drive(c, 24000 * 2, {}, true);
	uint64_t lastBeat = 0;
	for (size_t i = 0; i < a.at.size(); i++) if (a.beat[i]) lastBeat = a.at[i];
	const uint64_t edge = lastBeat + 100;
	TickLog b = Drive(c, 30000, { edge }, true, 24000 * 2);
	int ticksNearEdge = 0;
	for (uint64_t t : b.at) if (t >= lastBeat && t <= edge + 500) ticksNearEdge++;
	CHECK(ticksNearEdge == 0);
	CHECK(c.GetState() == ClockEngine::State::External);
}

TEST(clock_stop_and_restart)
{
	ClockEngine c;
	c.Init(ClockEngine::PeriodQ8FromBpmQ4(100 * 16));
	std::vector<uint64_t> edges;
	for (int i = 0; i < 8; i++) edges.push_back(static_cast<uint64_t>(i + 1) * 24000);
	TickLog a = Drive(c, 9 * 24000, edges);
	CHECK(c.GetState() == ClockEngine::State::External);
	// The sequencer stops. Within a few seconds the clock is Lost; with the
	// default Hold policy no ticks fire while it waits.
	TickLog b = Drive(c, 48000 * 5, {}, true, 9 * 24000);
	CHECK(c.GetState() == ClockEngine::State::Lost);
	if (kClockFallback == ClockFallback::Hold)
	{
		CHECK(!c.Running());
		int late = 0;
		for (uint64_t t : b.at) late += t > 9 * 24000 + 3 * 48000;
		CHECK(late == 0);
	}
	// It starts again: the first edge is a beat, and the old tempo is kept
	// until a new period is measured.
	const uint64_t restart = 9 * 24000 + 48000 * 5 + 100;
	TickLog d = Drive(c, 24000 * 3, { restart, restart + 24000, restart + 48000 }, true, restart);
	CHECK(!d.at.empty() && d.at[0] == restart && d.beat[0]);
	CHECK(c.GetState() == ClockEngine::State::External);
	CHECK(d.at.size() >= 9);
}

TEST(clock_unpatch_returns_to_internal)
{
	ClockEngine c;
	c.Init(ClockEngine::PeriodQ8FromBpmQ4(100 * 16));
	std::vector<uint64_t> edges;
	for (int i = 0; i < 4; i++) edges.push_back(static_cast<uint64_t>(i + 1) * 24000);
	Drive(c, 5 * 24000, edges);
	CHECK(c.GetState() == ClockEngine::State::External);
	TickLog b = Drive(c, 48000 * 2, {}, false, 5 * 24000);
	CHECK(c.GetState() == ClockEngine::State::Internal);
	CHECK(b.at.size() >= 12);   // 100 BPM: ~13 16ths in 2 s
}

TEST(clock_speedup_lands_beat_on_edge)
{
	// Pulses suddenly twice as fast: each edge is still a beat, even though
	// the 16ths scheduled from the old period have not all fired.
	ClockEngine c;
	c.Init(ClockEngine::PeriodQ8FromBpmQ4(100 * 16));
	std::vector<uint64_t> edges;
	for (int i = 0; i < 6; i++) edges.push_back(static_cast<uint64_t>(i + 1) * 24000);
	const uint64_t last = edges.back();
	for (int i = 1; i <= 6; i++) edges.push_back(last + static_cast<uint64_t>(i) * 12000);
	TickLog log = Drive(c, last + 7 * 12000, edges);
	for (int i = 1; i <= 6; i++)
	{
		bool found = false;
		for (size_t k = 0; k < log.at.size(); k++)
			if (log.at[k] == last + static_cast<uint64_t>(i) * 12000 && log.beat[k]) found = true;
		CHECK(found);
	}
}
