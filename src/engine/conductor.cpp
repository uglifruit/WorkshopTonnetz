// conductor.cpp — tick-level glue between harmony, melody and the ISR.

#include "conductor.h"

namespace tonnetz {

static HarmonyParams ToHarmony(const PerformParams &p)
{
	HarmonyParams h;
	h.wander = p.wander;
	h.cvAttraction = p.cvAttraction;
	h.cvStrict = p.cvStrict;
	h.cvActive = p.cvActive;
	h.cvPc = p.cvPc;
	h.phraseLength = p.phraseLength;
	return h;
}

void Conductor::Init(const EngineConfig *cfg, uint32_t seed)
{
	cfg_ = cfg;
	rng_.Seed(seed);
	TonnetzInit();
	tonal_.Init(&cfg->tonal);
	tonal_.SetInertia(params_.inertia);
	melody_.Init(&cfg->melody, &rng_);
	harmony_.Init(cfg, &rng_, &tonal_);
	harmony_.SetParams(ToHarmony(params_));
	Reset(0);
}

void Conductor::SetParams(const PerformParams &p)
{
	params_ = p;
	tonal_.SetInertia(p.inertia);
	harmony_.SetParams(ToHarmony(p));
}

void Conductor::Reset(uint32_t atTick)
{
	harmony_.Reset();
	melody_.Reset();
	nextTick_ = atTick;
	chordStartTick_ = atTick;
	started_ = false;
}

float Conductor::Phase() const
{
	return static_cast<float>(nextTick_ - chordStartTick_) / static_cast<float>(kTicksPerChord);
}

static uint8_t Note(int n) { return static_cast<uint8_t>(n < 0 ? 0 : (n > 127 ? 127 : n)); }

TickPlan Conductor::PlanTick()
{
	TickPlan plan{};
	plan.tick = nextTick_;

	bool boundary = false;
	if (!started_)
	{
		started_ = true;
		boundary = true;
		chordStartTick_ = nextTick_;
	}
	else if (nextTick_ - chordStartTick_ >= static_cast<uint32_t>(kTicksPerChord))
	{
		harmony_.Advance();
		chordStartTick_ = nextTick_;
		boundary = true;
	}

	const int tickInChord = static_cast<int>(nextTick_ - chordStartTick_);
	const float phase = static_cast<float>(tickInChord) / static_cast<float>(kTicksPerChord);

	// Commitment: from here on the planned next chord is fixed, so the melody
	// can anticipate it without the destination moving under it.
	if (!harmony_.Committed() && phase >= cfg_->harmony.commitPhase) harmony_.Commit();

	if (boundary)
	{
		plan.flags |= kPlanChordChange;
		const Voicing &v = harmony_.CurrentVoicing();
		for (int i = 0; i < 3; i++) plan.chord[i] = Note(v.note[i]);
		float g = params_.glideSeconds * static_cast<float>(kSampleRate);
		plan.glideSamples = g < 0.0f ? 0u : static_cast<uint32_t>(g);
		if (harmony_.PhrasePos() == 0) plan.flags |= kPlanPhraseStart;
	}
	if (harmony_.Committed()) plan.flags |= kPlanCommitted;
	plan.chordRoot = Note(48 + harmony_.Current().root);
	plan.tension = static_cast<uint8_t>(harmony_.Tension() * 255.0f);

	MelodyContext mc;
	mc.prev = harmony_.Previous();
	mc.cur = harmony_.Current();
	mc.next = harmony_.Next();
	mc.hasPrev = harmony_.HasPrevious();
	mc.phase = phase;
	mc.tickInChord = tickInChord;
	mc.ticksPerChord = kTicksPerChord;
	mc.scaleMask = tonal_.ScaleMaskNow();
	mc.scaleConfidence = tonal_.ScaleConfidence();
	mc.chromatic = tonal_.Chromatic();
	mc.nextStartsPhrase = harmony_.NextStartsPhrase();
	mc.lastChordOfPhrase = harmony_.PhrasePos() == harmony_.PhraseLength() - 1;
	mc.chordBoundary = boundary;

	MelodyParams mp;
	mp.anticipation = params_.anticipation;
	mp.density = params_.density;
	mp.rhythm = params_.rhythm;

	const MelodyEvent ev = melody_.Tick(mc, mp);
	plan.melAction = ev.action;
	plan.melNote = ev.note;

	nextTick_++;
	return plan;
}

} // namespace tonnetz
