// clock.cpp — internal/external tick generation. Runs in the audio ISR.

#include "clock.h"

namespace tonnetz {

void __not_in_flash_func(ClockEngine::Init)(uint32_t internalPeriodQ8)
{
	internalPeriodQ8_ = internalPeriodQ8;
	state_ = State::Internal;
	accQ8_ = 0;
	sinceTick_ = 0;
	sinceEdge_ = 0;
	nIntervals_ = 0;
	extPeriod_ = 0;
	subLeft_ = 0;
	tickInBeat_ = 0;
	beat_ = false;
	lastTickBeat_ = true;
}

bool __not_in_flash_func(ClockEngine::FireTick)(bool beat)
{
	beat_ = beat;
	lastTickBeat_ = beat;
	sinceTick_ = 0;
	tickInBeat_ = beat ? 0 : tickInBeat_ + 1;
	return true;
}

static inline uint32_t Median3(uint32_t a, uint32_t b, uint32_t c)
{
	if (a > b) { const uint32_t t = a; a = b; b = t; }
	if (b > c) { const uint32_t t = b; b = c; c = t; }
	if (a > b) { const uint32_t t = a; a = b; b = t; }
	return b;
}

bool __not_in_flash_func(ClockEngine::Process)(bool risingEdge, bool patched)
{
	beat_ = false;
	if (sinceTick_ < 0x7FFFFFFFu) sinceTick_++;
	if (sinceEdge_ < 0x7FFFFFFFu) sinceEdge_++;

	if (!patched && state_ != State::Internal)
	{
		// Unpatched: back to the internal tempo, continuing the 16th grid
		// from the last tick so nothing doubles.
		state_ = State::Internal;
		const uint32_t carried = sinceTick_ < (internalPeriodQ8_ >> 8) ? sinceTick_ : (internalPeriodQ8_ >> 8);
		accQ8_ = carried << 8;
		nIntervals_ = 0;
	}

	if (patched && risingEdge && sinceEdge_ >= kMinPulseInterval)
	{
		const uint32_t interval = sinceEdge_;
		sinceEdge_ = 0;
		// Only the gap between two edges of a RUNNING external clock is a
		// period; the first edge after Internal or Lost just starts timing.
		if (state_ == State::External && interval <= kMaxPulseInterval)
		{
			intervals_[nIntervals_ % 3] = interval;
			nIntervals_++;
			extPeriod_ = nIntervals_ >= 3 ? Median3(intervals_[0], intervals_[1], intervals_[2]) : interval;
		}
		else if (extPeriod_ == 0)
		{
			// First edge ever: assume the internal tempo until a second arrives.
			extPeriod_ = (internalPeriodQ8_ >> 8) * 4;
		}
		state_ = State::External;
		subPeriod_ = extPeriod_ / 4;
		subAcc_ = 0;
		subLeft_ = 3;

		// Guard: a beat tick that fired within the last eighth of a beat was
		// this beat (the internal clock or a freewheel got there first).
		const uint32_t guard = extPeriod_ / 8;
		if (lastTickBeat_ && sinceTick_ <= guard)
		{
			sinceTick_ = 0;
			return false;
		}
		return FireTick(true);
	}

	switch (state_)
	{
	case State::External:
		if (sinceEdge_ > (2 * extPeriod_ > kLostMinSamples ? 2 * extPeriod_ : kLostMinSamples))
		{
			state_ = State::Lost;
			nIntervals_ = 0;
			accQ8_ = 0;
			return false;
		}
		if (subLeft_ > 0 && ++subAcc_ >= subPeriod_)
		{
			subAcc_ = 0;
			subLeft_--;
			return FireTick(false);
		}
		return false;

	case State::Lost:
		if (kClockFallback == ClockFallback::Hold) return false;
		// fall through: run a counter at the chosen tempo
		{
			const uint32_t periodQ8 = (kClockFallback == ClockFallback::Freewheel && extPeriod_ > 0)
				? (extPeriod_ / 4) << 8 : internalPeriodQ8_;
			accQ8_ += 256;
			if (accQ8_ >= periodQ8)
			{
				accQ8_ -= periodQ8;
				return FireTick(tickInBeat_ >= 3);
			}
			return false;
		}

	case State::Internal:
	default:
		accQ8_ += 256;
		if (accQ8_ >= internalPeriodQ8_)
		{
			accQ8_ -= internalPeriodQ8_;
			return FireTick(tickInBeat_ >= 3);
		}
		return false;
	}
}

} // namespace tonnetz
