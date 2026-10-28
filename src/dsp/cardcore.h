// cardcore.h — everything the audio ISR does, behind a hardware-free interface.
//
// main.cpp copies the ComputerCard inputs into an HwIn, calls Process() once
// per sample, and copies the HwOut back to the jacks and LEDs. Because nothing
// here touches the hardware, tests/ and tools/render drive exactly this code —
// the clock, the plan queue, the pickup, the synth — with simulated inputs.
//
// Per sample:  clock -> reset -> apply due plans -> (control block) -> render.
//
// The TRANSPORT counts ticks. A beat tick always lands on a multiple of four,
// so chord changes (every 16 ticks from the last reset) always fall on beats,
// even when an external clock jumps ahead.

#pragma once
#include <stdint.h>
#include "clock.h"
#include "controls.h"
#include "cvin.h"
#include "dsp_config.h"
#include "link.h"
#include "synth.h"

namespace tonnetz {

struct HwIn
{
	int32_t knob[3];         // 0..4095: Main, X, Y
	int     sw;              // 0 Down, 1 Middle, 2 Up
	int32_t cv1, cv2;        // -2048..2047
	bool    pulse1Rise, pulse2Rise;
	bool    pulse1Patched, cv1Patched;
};

struct HwOut
{
	int16_t  audio1, audio2;
	bool     pulse1, pulse2;
	bool     controlTick;    // the fields below were refreshed this sample
	uint16_t led[6];         // 0..4095
	uint8_t  cvNote1, cvNote2;
};

class CardCore
{
public:
	void Init(Link *link);
	void Process(const HwIn &in, HwOut &out);

	const Synth       &GetSynth()    const { return synth_; }
	const ClockEngine &GetClock()    const { return clock_; }
	const Controls    &GetControls() const { return controls_; }
	bool     Started() const { return started_; }
	uint32_t DueTick() const { return dueTick_; }
	uint32_t Epoch()   const { return epoch_; }
	uint32_t BpmQ4(int32_t knobQ12) const;
	/// Plans applied on their tick / after it (the composer fell behind or the
	/// clock jumped). Late should stay zero on a free-running clock.
	uint32_t PlansOnTime() const { return plansOnTime_; }
	uint32_t PlansLate()   const { return plansLate_; }

private:
	void ControlTick(const HwIn &in, HwOut &out);
	void ApplyPlans();
	void ApplyPlan(const TickPlan &p, bool late);
	void Reset();

	Link *link_ = nullptr;
	Synth synth_;
	ClockEngine clock_;
	Controls controls_;
	CvPitch cvPitch_;
	CvSmooth cv2_;

	uint32_t bpmLut_[33];     // BPM Q4 at knob = i * 128
	int32_t  lastBpmKnob_ = -1;

	uint32_t sample_ = 0;
	uint32_t ctl_ = 0;
	uint32_t entropy_ = 0x12345678u;

	bool     started_ = false;
	uint32_t dueTick_ = 0;
	uint32_t epoch_ = 0;
	bool     held_ = false;

	int32_t trig1_ = 0, trig2_ = 0;          // samples
	int32_t beatFlash_ = 0, chordFlash_ = 0; // control ticks
	uint8_t tension_ = 0;
	bool    committed_ = false;
	uint8_t cvNote1_ = 48, cvNote2_ = 72;
	uint32_t plansOnTime_ = 0, plansLate_ = 0;
};

} // namespace tonnetz
