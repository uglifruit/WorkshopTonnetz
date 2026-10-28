// composer.cpp — core 1: resets, parameters, and planning ahead.

#include "composer.h"
#include "dsp/dsp_config.h"
#include "engine/mapping.h"

namespace tonnetz {

void Composer::Init(Link *link, const EngineConfig *cfg, uint32_t seed)
{
	link_ = link;
	epoch_ = link->resetEpoch;
	haveRaw_ = false;
	conductor_.Init(cfg, seed);
	conductor_.Reset(epoch_ ? link->restartTick : 0);
}

PerformParams Composer::Map(const int32_t *raw)
{
	PerformParams p;
	p.wander       = WanderFrom(raw[kParamWander], raw[kParamCv2], kCv2FullScale);
	p.glideSeconds = GlideSecondsFrom(raw[kParamGlide]);
	p.anticipation = Unit(raw[kParamAntic]);
	p.density      = Unit(raw[kParamDensity]);
	p.phraseLength = PhraseLengthFrom(raw[kParamPhraseIndex]);
	p.cvAttraction = CvAttractionFrom(raw[kParamCvAttract]);
	p.cvStrict     = CvStrictFrom(raw[kParamCvAttract]);
	p.rhythm       = Unit(raw[kParamRhythm]);
	p.inertia      = Unit(raw[kParamInertia]);
	p.cvActive     = raw[kParamCvActive] != 0;
	p.cvPc         = PitchClassFromSemitone(raw[kParamCvSemitone]);
	return p;
}

void Composer::Step()
{
	const uint32_t ep = link_->resetEpoch;
	if (ep != epoch_)
	{
		tz_dmb();
		epoch_ = ep;
		conductor_.Reset(link_->restartTick);
	}

	int32_t raw[kNumParams];
	if (link_->ReadParams(raw))
	{
		bool changed = !haveRaw_;
		for (int i = 0; i < kNumParams && !changed; i++) changed = raw[i] != raw_[i];
		if (changed)
		{
			for (int i = 0; i < kNumParams; i++) raw_[i] = raw[i];
			haveRaw_ = true;
			conductor_.SetParams(Map(raw));
		}
	}

	const uint32_t horizon = link_->transportRunning
		? link_->firedTick + static_cast<uint32_t>(kPlanLookaheadTicks)
		: static_cast<uint32_t>(kPlanLookaheadTicks);
	while (!link_->Full() && static_cast<int32_t>(conductor_.NextTick() - horizon) <= 0)
	{
		const uint32_t t0 = nowUs ? nowUs() : 0;
		TickPlan p = conductor_.PlanTick();
		if (nowUs)
		{
			const uint32_t dt = nowUs() - t0;
			if (dt > link_->planWorstUs) link_->planWorstUs = dt;
		}
		p.epoch = epoch_;
		if (onPlan) onPlan(p, conductor_, onPlanCtx);
		link_->Push(p);
	}
	if (!link_->composerReady)
	{
		tz_dmb();
		link_->composerReady = 1;
	}
}

} // namespace tonnetz
