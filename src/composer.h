// composer.h — core 1's loop body: keep the plan queue a couple of ticks ahead.
//
// Step() is called continuously on core 1 (and once per simulated sample on
// the host). It notices resets, picks up panel changes, and plans ticks until
// it is kPlanLookaheadTicks ahead of the ISR. Everything musical happens in
// the Conductor; this file only moves data across the Link.

#pragma once
#include <stdint.h>
#include "engine/conductor.h"
#include "link.h"

namespace tonnetz {

class Composer
{
public:
	void Init(Link *link, const EngineConfig *cfg, uint32_t seed);
	void Step();

	Conductor       &GetConductor()       { return conductor_; }
	const Conductor &GetConductor() const { return conductor_; }

	/// Maps the ISR's raw panel values to musical parameters.
	static PerformParams Map(const int32_t *raw);

	/// Host-only observation hook: called with every plan as it is queued.
	void (*onPlan)(const TickPlan &, const Conductor &, void *) = nullptr;
	void *onPlanCtx = nullptr;

	/// Microsecond clock (time_us_32 on the card). When set, the slowest
	/// PlanTick is published in Link::planWorstUs.
	uint32_t (*nowUs)() = nullptr;

private:
	Link *link_ = nullptr;
	Conductor conductor_;
	uint32_t epoch_ = 0;
	int32_t raw_[kNumParams];
	bool haveRaw_ = false;
};

} // namespace tonnetz
