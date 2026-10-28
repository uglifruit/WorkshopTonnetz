// controls.cpp — paging, debounce, pickup and the Down tap.

#include "controls.h"

namespace tonnetz {

// Defaults for pages that have not been visited yet (Q12).
static const int32_t kDefaults[3][3] = {
	{ 1200, 1700, 2048 },   // PERFORM: replaced by the live knobs at boot
	{ 2094, 2250, 2600 },   // TIMING: 100 BPM, density 0.55, phrase 8
	{ 2048, 2048, 2048 },   // ADVANCED: CV pull 0.54, eighth-note rhythm, inertia 0.5
};

static inline int32_t Abs(int32_t x) { return x < 0 ? -x : x; }

void Controls::Init()
{
	for (int p = 0; p < 3; p++)
		for (int k = 0; k < 3; k++)
		{
			stored_[p][k] = kDefaults[p][k];
			live_[p][k] = (p == kPerform);
			arrivalSign_[p][k] = 0;
		}
	page_ = kPerform;
	lastSw_ = kMiddle;
	switchSettle_ = 0;
	pendingPage_ = -1;
	pageSettle_ = 0;
	resetRequest_ = false;
	phraseIndex_ = 2;
}

void __not_in_flash_func(Controls::Arrive)(Page p, const int32_t knob[3])
{
	page_ = p;
	for (int k = 0; k < 3; k++)
	{
		const int32_t d = knob[k] - stored_[p][k];
		live_[p][k] = Abs(d) < kPickupBand;
		arrivalSign_[p][k] = d > 0 ? 1 : -1;
	}
}

void __not_in_flash_func(Controls::Tick)(const int32_t knob[3], int sw)
{
	for (int k = 0; k < 3; k++) lastKnob_[k] = knob[k];

	// --- switch, debounced ---
	if (switchSettle_ > 0) switchSettle_--;
	if (sw != lastSw_ && switchSettle_ == 0)
	{
		const int prev = lastSw_;
		lastSw_ = sw;
		switchSettle_ = kSwitchSettleTicks;
		if (sw == kDown)
		{
			pendingPage_ = -1;          // a flick from Up that paused at Middle must not land
			downTicks_ = 0;
			downMoved_ = false;
			for (int k = 0; k < 3; k++) downArrival_[k] = knob[k];
			Arrive(kAdvanced, knob);
		}
		else if (prev == kDown)
		{
			// Released: a tap if it was short and nothing was turned.
			if (!downMoved_ && downTicks_ < kTapMaxTicks) resetRequest_ = true;
			Arrive(sw == kUp ? kTiming : kPerform, knob);
		}
		else
		{
			// Middle <-> Up, deferred: passing through Middle on the way between
			// Up and Down is not a visit.
			pendingPage_ = (sw == kUp) ? kTiming : kPerform;
			pageSettle_ = kPageSettleTicks;
		}
	}
	if (pendingPage_ >= 0 && pageSettle_ > 0 && --pageSettle_ == 0)
	{
		if (pendingPage_ != page_) Arrive(static_cast<Page>(pendingPage_), knob);
		pendingPage_ = -1;
	}

	if (lastSw_ == kDown)
	{
		if (downTicks_ < 0x7FFFFFFF) downTicks_++;
		for (int k = 0; k < 3; k++)
			if (Abs(knob[k] - downArrival_[k]) > kPickupBand) downMoved_ = true;
	}

	// --- pickup ---
	for (int k = 0; k < 3; k++)
	{
		const int32_t d = knob[k] - stored_[page_][k];
		if (!live_[page_][k])
		{
			const int8_t sign = d > 0 ? 1 : -1;
			if (Abs(d) < kPickupBand || sign != arrivalSign_[page_][k]) live_[page_][k] = true;
		}
		if (live_[page_][k]) stored_[page_][k] = knob[k];
	}

	// --- phrase length: four zones with hysteresis ---
	const int32_t v = stored_[kTiming][2];
	const int32_t lo = phraseIndex_ * 1024 - 60;
	const int32_t hi = (phraseIndex_ + 1) * 1024 + 60;
	if (v < lo || v > hi)
	{
		int idx = v / 1024;
		phraseIndex_ = idx > 3 ? 3 : idx;
	}
}

} // namespace tonnetz
