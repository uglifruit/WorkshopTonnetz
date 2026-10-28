// sim.h — the whole card on the host: CardCore (the ISR) + Composer (core 1).
//
// Step() runs one sample: the ISR's Process(), then a composer Step(), which
// is the worst case for the composer's latency (on the card core 1 loops far
// more often than once per sample). Used by the tests and tools/render.

#pragma once
#include "composer.h"
#include "dsp/cardcore.h"
#include "dsp/fastmath.h"

namespace tonnetz {

struct Sim
{
	EngineConfig cfg;
	Link link;
	CardCore core;
	Composer composer;
	HwIn in{};
	HwOut out{};
	uint64_t samples = 0;

	explicit Sim(uint32_t seed = 1)
	{
		DspInit();
		core.Init(&link);
		composer.Init(&link, &cfg, seed);
		in.knob[0] = 1200;   // wander ~0.3
		in.knob[1] = 1700;   // glide ~0.3 s
		in.knob[2] = 2048;   // anticipation 0.5
		in.sw = 1;           // Middle
	}

	void Step()
	{
		core.Process(in, out);
		composer.Step();
		in.pulse1Rise = false;
		in.pulse2Rise = false;
		samples++;
	}

	void Run(uint64_t n) { for (uint64_t i = 0; i < n; i++) Step(); }
	void RunSeconds(double s) { Run(static_cast<uint64_t>(s * 48000.0)); }
};

} // namespace tonnetz
