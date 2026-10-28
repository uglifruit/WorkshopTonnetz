// cvin.h — CV In 1 as a quantised pitch, CV In 2 as a smoothed offset.
//
// CV In 1 is read as 1 V/oct relative to 0 V = C. The nearest semitone is
// taken with hysteresis (the reading must pass the half-way point by a
// quarter semitone) and must then hold for a few ms before it counts, so an
// ADC wobble at a boundary cannot flip the harmony's target note. The octave
// is kept (the semitone can be negative) but the harmony only uses the pitch
// class.

#pragma once
#include <stdint.h>
#include "dsp_config.h"
#include "platform.h"

namespace tonnetz {

class CvPitch
{
public:
	/// Control rate. Returns the settled semitone (0 V = 0).
	int32_t Process(int32_t counts)
	{
		const int32_t v = ((counts - kCvOffsetCounts) * kCvScaleQ12) >> 12;
		const int32_t q8 = v * 9;                   // semitones, Q8 (256/9 counts per semitone)
		const int32_t dist = q8 - candidate_ * 256;
		if (dist > 128 + kCvHysteresisQ8 || dist < -(128 + kCvHysteresisQ8))
		{
			candidate_ = (q8 + 128) >> 8;           // round to nearest
			settle_ = kCvSettleTicks;
		}
		if (settle_ > 0 && --settle_ == 0) semitone_ = candidate_;
		return semitone_;
	}
	int32_t Semitone() const { return semitone_; }
	void Reset(int32_t s) { semitone_ = candidate_ = s; settle_ = 0; }

private:
	int32_t candidate_ = 0;
	int32_t semitone_ = 0;
	int     settle_ = 0;
};

/// A slow one-pole on CV In 2 (ComputerCard already filters at ~240 Hz).
class CvSmooth
{
public:
	int32_t Process(int32_t counts)
	{
		acc_ += ((counts << 8) - acc_) >> 5;
		return acc_ >> 8;
	}
private:
	int32_t acc_ = 0;
};

} // namespace tonnetz
