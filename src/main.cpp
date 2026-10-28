// main.cpp — TONNETZ: a generative harmonic synthesiser for the Workshop Computer.
//
// This file is only wiring. The two halves of the card are:
//
//   core 0, audio ISR (48 kHz)   dsp/cardcore.*   clock, plans, panel, synth
//   core 1, free-running loop    composer.*       harmony, melody, planning
//
// They meet in link.h. Read docs/HARMONY.md for the music and CLAUDE.md for
// the rules this code keeps.
//
//   Audio Out 1   three-voice chord          Audio Out 2   melody
//   Pulse In 1    clock (quarter notes)      Pulse In 2    reset to home
//   CV In 1       1 V/oct harmonic target    CV In 2       wander offset
//   CV Out 1      chord root, 1 V/oct        CV Out 2      melody, 1 V/oct
//   Pulse Out 1   chord change               Pulse Out 2   melody note

#include "platform.h"
#include "ComputerCard.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include "composer.h"
#include "dsp/cardcore.h"
#include "dsp/fastmath.h"
#include "engine/config.h"

#if TONNETZ_LOAD_METER
#include "hardware/structs/systick.h"
#endif

using namespace tonnetz;

// BRING-UP LOAD METER (cmake -DTONNETZ_LOAD_METER=ON). Replaces the pitch CV
// outputs with:
//   CV Out 1  ISR load: peak ProcessSample time over ~0.1 s, 0..+6 V = 0..100%
//             of the 20.8 us sample period at 192 MHz (4000 cycles).
//   CV Out 2  core 1's slowest tick plan since boot, 0..+6 V = 0..125 ms, the
//             time core 1 has (2 ticks of lookahead) at 240 BPM.
// See docs/BRINGUP.md. Off in normal builds.
static constexpr uint32_t kCyclesPerSample = 192000000u / 48000u;

static Link gLink;
static CardCore gCore;
static Composer gComposer;
static EngineConfig gConfig;
static volatile uint32_t gCardId = 0;

class TonnetzCard : public ComputerCard
{
public:
	/// UniqueCardID() is protected; main() needs it for the seed.
	uint32_t CardIdBits() { const uint64_t id = UniqueCardID(); return static_cast<uint32_t>(id ^ (id >> 32)); }

protected:
	void __not_in_flash_func(ProcessSample)() override
	{
#if TONNETZ_LOAD_METER
		const uint32_t t0 = systick_hw->cvr;
#endif
		HwIn in;
		in.knob[0] = KnobVal(Main);
		in.knob[1] = KnobVal(X);
		in.knob[2] = KnobVal(Y);
		in.sw = static_cast<int>(SwitchVal());
		in.cv1 = CVIn1();
		in.cv2 = CVIn2();
		in.pulse1Rise = PulseIn1RisingEdge();
		in.pulse2Rise = PulseIn2RisingEdge();
		in.pulse1Patched = Connected(Pulse1);
		in.cv1Patched = Connected(CV1);

		HwOut out;
		gCore.Process(in, out);

		AudioOut1(out.audio1);
		AudioOut2(out.audio2);
		PulseOut1(out.pulse1);
		PulseOut2(out.pulse2);

		if (out.controlTick)
		{
			for (int i = 0; i < 6; i++) LedBrightness(i, out.led[i]);
#if TONNETZ_LOAD_METER
			if (++meterTicks_ >= 300)   // ~0.1 s
			{
				CVOut1(static_cast<int16_t>(peakCycles_ * 2047u / kCyclesPerSample));
				const uint32_t us = gLink.planWorstUs;
				CVOut2(static_cast<int16_t>((us > 125000u ? 125000u : us) * 2047u / 125000u));
				peakCycles_ = 0;
				meterTicks_ = 0;
			}
#else
			// Only on a change: cheap, and keeps the calibrated DAC maths off
			// most control ticks.
			if (out.cvNote1 != lastCv1_) { lastCv1_ = out.cvNote1; CVOut1MIDINote(out.cvNote1); }
			if (out.cvNote2 != lastCv2_) { lastCv2_ = out.cvNote2; CVOut2MIDINote(out.cvNote2); }
#endif
		}
#if TONNETZ_LOAD_METER
		const uint32_t used = (t0 - systick_hw->cvr) & 0x00FFFFFFu;
		if (used > peakCycles_) peakCycles_ = used;
#endif
	}

private:
	uint8_t lastCv1_ = 0xFF, lastCv2_ = 0xFF;
#if TONNETZ_LOAD_METER
	uint32_t peakCycles_ = 0;
	uint32_t meterTicks_ = 0;
#endif
};

static uint32_t NowUs() { return time_us_32(); }

// Global, not on main()'s stack.
static TonnetzCard card;

static void Core1Main()
{
	// Wait for the ISR's ~85 ms of ADC noise, then seed.
	while (!gLink.entropyReady) tight_loop_contents();
	tz_dmb();
	const uint32_t seed = kDeterministicSeed ? kDeterministicSeed
		: (gLink.entropy ^ gCardId ^ time_us_32());
	gComposer.Init(&gLink, &gConfig, seed);
	gComposer.nowUs = NowUs;
	for (;;) gComposer.Step();
}

int main()
{
	// 192 MHz at 1.15 V, as proven by the sibling cards.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	sleep_ms(2);
	set_sys_clock_khz(192000, true);

	DspInit();
	gCore.Init(&gLink);
#if TONNETZ_LOAD_METER
	systick_hw->rvr = 0x00FFFFFFu;
	systick_hw->cvr = 0;
	systick_hw->csr = 0x5;   // enable, processor clock, no interrupt
#endif
	gCardId = card.CardIdBits();

	card.EnableNormalisationProbe();   // jack detection: Pulse In 1 and CV In 1
	multicore_launch_core1(Core1Main);
	card.Run();
}
