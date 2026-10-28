// cardcore.cpp — the audio ISR's whole job.

#include "cardcore.h"
#include "engine/mapping.h"

namespace tonnetz {

static constexpr int32_t kBeatFlashTicks  = 240;   // 80 ms at the control rate
static constexpr int32_t kChordFlashTicks = 600;   // 200 ms
static constexpr uint32_t kResetCoincide  = 480;   // a reset within 10 ms of a tick belongs to it
static constexpr uint32_t kLatePlan       = 1440;  // a plan more than 30 ms after its tick is late

void CardCore::Init(Link *link)
{
	link_ = link;
	// The exponential BPM curve, evaluated once (float, at boot) into a table
	// the ISR interpolates in integers.
	for (int i = 0; i <= 32; i++)
		bpmLut_[i] = static_cast<uint32_t>(BpmFrom(i * 128) * 16.0f + 0.5f);
	synth_.Init();
	controls_.Init();
	lastBpmKnob_ = controls_.Value(Controls::kTiming, 0);
	clock_.Init(ClockEngine::PeriodQ8FromBpmQ4(BpmQ4(lastBpmKnob_)));
	started_ = false;
	dueTick_ = 0;
	epoch_ = 0;
}

uint32_t __not_in_flash_func(CardCore::BpmQ4)(int32_t k) const
{
	if (k < 0) k = 0;
	if (k > 4095) k = 4095;
	const int i = k >> 7;
	const uint32_t f = static_cast<uint32_t>(k & 127);
	return bpmLut_[i] + (((bpmLut_[i + 1] - bpmLut_[i]) * f) >> 7);
}

void __not_in_flash_func(CardCore::Reset)()
{
	// Restart on a beat so chord changes stay on beats: the tick that just
	// fired if the reset arrived with it (a sequencer's reset+clock), else the
	// next beat.
	uint32_t r = 0;
	if (started_)
	{
		if (clock_.SamplesSinceTick() < kResetCoincide && (dueTick_ & 3u) == 0) r = dueTick_;
		else r = (dueTick_ + 4u) & ~3u;
	}
	epoch_++;
	link_->restartTick = r;
	tz_dmb();
	link_->resetEpoch = epoch_;
	synth_.MelodyOff();
}

void __not_in_flash_func(CardCore::ApplyPlan)(const TickPlan &p, bool late)
{
	if (p.flags & kPlanChordChange)
	{
		synth_.SetChord(p.chord, p.glideSamples);
		trig1_ = kTriggerSamples;
		chordFlash_ = kChordFlashTicks;
		cvNote1_ = p.chordRoot;
	}
	committed_ = (p.flags & kPlanCommitted) != 0;
	tension_ = p.tension;

	// A late plan (the composer fell behind, or an external clock jumped
	// ahead) keeps its harmony but not its note attacks — a note that should
	// have started a while ago is better dropped than played off the beat.
	// Dropped becomes a REST: the composer thinks the previous note ended
	// here, so letting it ring on would hang it until the next event.
	switch (p.melAction)
	{
	case kMelOn:
		if (late) { synth_.MelodyOff(); break; }
		synth_.MelodyOn(p.melNote, false);
		trig2_ = kTriggerSamples;
		cvNote2_ = p.melNote;
		break;
	case kMelLegato:
		if (late) { synth_.MelodyOff(); break; }
		synth_.MelodyOn(p.melNote, true);
		cvNote2_ = p.melNote;
		break;
	case kMelOff:
		synth_.MelodyOff();
		break;
	default:
		break;
	}
}

void __not_in_flash_func(CardCore::ApplyPlans)()
{
	if (!started_) return;
	for (int guard = 0; guard < static_cast<int>(Link::kRingSize); guard++)
	{
		const TickPlan *p = link_->Peek();
		if (!p) break;
		if (p->epoch != epoch_) { link_->Pop(); continue; }       // from before a reset
		if (static_cast<int32_t>(p->tick - dueTick_) > 0) break;  // not yet
		// Late = for a tick already passed, or arriving well after its own
		// tick fired (core 1 fell behind).
		const bool late = p->tick != dueTick_ || clock_.SamplesSinceTick() > kLatePlan;
		if (late) plansLate_++;
		else plansOnTime_++;
		ApplyPlan(*p, late);
		link_->Pop();
	}
}

void __not_in_flash_func(CardCore::ControlTick)(const HwIn &in, HwOut &out)
{
	controls_.Tick(in.knob, in.sw);
	if (controls_.TakeReset()) Reset();

	const int32_t bk = controls_.Value(Controls::kTiming, 0);
	if (bk != lastBpmKnob_)
	{
		lastBpmKnob_ = bk;
		clock_.SetInternalPeriod(ClockEngine::PeriodQ8FromBpmQ4(BpmQ4(bk)));
	}

	const int32_t semi = cvPitch_.Process(in.cv1);
	const int32_t cv2 = cv2_.Process(in.cv2);

	int32_t v[kNumParams];
	v[kParamWander]      = controls_.Value(Controls::kPerform, 0);
	v[kParamGlide]       = controls_.Value(Controls::kPerform, 1);
	v[kParamAntic]       = controls_.Value(Controls::kPerform, 2);
	v[kParamDensity]     = controls_.Value(Controls::kTiming, 1);
	v[kParamPhraseIndex] = controls_.PhraseIndex();
	v[kParamCvAttract]   = controls_.Value(Controls::kAdvanced, 0);
	v[kParamRhythm]      = controls_.Value(Controls::kAdvanced, 1);
	v[kParamInertia]     = controls_.Value(Controls::kAdvanced, 2);
	v[kParamCvSemitone]  = semi;
	v[kParamCvActive]    = in.cv1Patched ? 1 : 0;
	v[kParamCv2]         = cv2 >> 2 << 2;   // drop the last bits of ADC noise
	link_->WriteParams(v);

	// --- LEDs:  0 1 / 2 3 / 4 5 ---
	if (beatFlash_ > 0) beatFlash_--;
	if (chordFlash_ > 0) chordFlash_--;
	const bool ext = clock_.GetState() == ClockEngine::State::External;
	out.led[0] = beatFlash_ > 0 ? 4095 : (ext ? 500 : 0);                 // clock
	out.led[1] = static_cast<uint16_t>(chordFlash_ * 4095 / kChordFlashTicks); // chord change
	out.led[2] = static_cast<uint16_t>(synth_.MelodyEnvelope() >> 18);    // melody
	out.led[3] = static_cast<uint16_t>(tension_ * 16);                     // departure from home
	out.led[4] = committed_ ? 4095 : 400;                                  // next chord committed
	switch (controls_.CurrentPage())
	{
	case Controls::kPerform:  out.led[5] = 0; break;
	case Controls::kTiming:   out.led[5] = 4095; break;
	case Controls::kAdvanced: out.led[5] = ((ctl_ >> 13) & 1u) ? 4095 : 0; break;   // ~6 Hz blink
	}
	// Pickup: a knob still waiting to catch its value pulses its LED.
	const uint32_t ph = (ctl_ >> 4) & 2047u;   // ~1.5 Hz triangle
	const uint16_t pulse = static_cast<uint16_t>(ph < 1024 ? ph * 4 : (2047 - ph) * 4);
	for (int k = 0; k < 3; k++)
		if (controls_.Waiting(k)) out.led[k] = pulse;

	out.cvNote1 = cvNote1_;
	out.cvNote2 = cvNote2_;
	out.controlTick = true;
}

void __not_in_flash_func(CardCore::Process)(const HwIn &in, HwOut &out)
{
	// Seed material for core 1: knob and CV LSB noise over the first ~85 ms.
	if (sample_ < 4096)
	{
		entropy_ = (entropy_ * 1664525u + 1013904223u)
			^ static_cast<uint32_t>((in.knob[0] & 15) | ((in.knob[1] & 15) << 4) | ((in.knob[2] & 15) << 8)
				| ((in.cv1 & 15) << 12) | ((in.cv2 & 15) << 16));
		if (sample_ == 4095)
		{
			link_->entropy = entropy_;
			tz_dmb();
			link_->entropyReady = 1;
		}
	}
	sample_++;

	// --- transport ---
	if (!started_)
	{
		if (link_->composerReady)
		{
			started_ = true;
			dueTick_ = 0;
			clock_.Init(ClockEngine::PeriodQ8FromBpmQ4(BpmQ4(lastBpmKnob_)));
			link_->firedTick = 0;
			tz_dmb();
			link_->transportRunning = 1;
			beatFlash_ = kBeatFlashTicks;
		}
	}
	else if (clock_.Process(in.pulse1Rise, in.pulse1Patched))
	{
		uint32_t next = dueTick_ + 1;
		if (clock_.Beat()) next = (next + 3u) & ~3u;   // beats on multiples of four
		dueTick_ = next;
		link_->firedTick = dueTick_;
		if (clock_.Beat()) beatFlash_ = kBeatFlashTicks;
	}

	if (in.pulse2Rise) Reset();
	ApplyPlans();

	// A patched clock that has stopped (Hold policy): the chord sustains, the
	// melody lets go rather than hanging on one note indefinitely.
	if (started_ && !clock_.Running())
	{
		if (!held_) { synth_.MelodyOff(); held_ = true; }
	}
	else held_ = false;

	// --- control rate, staggered so no sample pays for both jobs ---
	const uint32_t phase = ctl_ & (kControlBlock - 1);
	out.controlTick = false;
	if (phase == 0) ControlTick(in, out);
	else if (phase == kControlBlock / 2) synth_.Control();
	ctl_++;

	out.audio1 = synth_.RenderChord();
	out.audio2 = synth_.RenderMelody();
	out.pulse1 = trig1_ > 0;
	out.pulse2 = trig2_ > 0;
	if (trig1_ > 0) trig1_--;
	if (trig2_ > 0) trig2_--;
}

} // namespace tonnetz
