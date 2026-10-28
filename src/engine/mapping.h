// mapping.h — knob and CV values to musical parameters.
//
// Every curve is here, in one file, so the control path can be tested end to
// end: tests/test_controls.cpp checks each knob reaches its whole range. (A
// sibling card shipped a glide knob that only reached 1/15th of its stated
// range because nothing tested the mapping itself.)
//
// Knob values are Q12, 0..4095.

#pragma once
#include <stdint.h>
#include <math.h>

namespace tonnetz {

constexpr float kBpmMin = 40.0f;
constexpr float kBpmMax = 240.0f;
constexpr float kGlideMaxSeconds = 4.0f;
constexpr int   kGlideDeadzone   = 40;      // bottom 1% of X = instant
constexpr float kStrictAbove     = 0.92f;   // top 8% of the CV-attraction knob = strict mode
constexpr int   kPhraseLengths[4] = { 2, 4, 8, 16 };

static inline float Unit(int32_t q12)
{
	if (q12 < 0) q12 = 0;
	if (q12 > 4095) q12 = 4095;
	return static_cast<float>(q12) * (1.0f / 4095.0f);
}

/// Main (timing): exponential, so every part of the travel changes tempo by
/// the same RATIO. Noon is 98 BPM. (Evaluated at boot into an integer table
/// for the ISR — see CardCore.)
static inline float BpmFrom(int32_t knobQ12)
{
	return kBpmMin * powf(kBpmMax / kBpmMin, Unit(knobQ12));
}

/// Main (performance) plus CV In 2: +-5 V sweeps the whole range.
static inline float WanderFrom(int32_t knobQ12, int32_t cv2Counts, int32_t cv2FullScale)
{
	const float w = Unit(knobQ12) + static_cast<float>(cv2Counts) / static_cast<float>(cv2FullScale);
	return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
}

/// X (performance): cubic, 0..4 s, so the short end — where most of the
/// difference between "change" and "portamento" lives — gets most of the
/// travel. 25% -> 0.06 s, 50% -> 0.5 s, 75% -> 1.7 s, 100% -> 4 s.
static inline float GlideSecondsFrom(int32_t knobQ12)
{
	if (knobQ12 < kGlideDeadzone) return 0.0f;
	const float u = Unit(knobQ12);
	return kGlideMaxSeconds * u * u * u;
}

static inline float CvAttractionFrom(int32_t knobQ12)
{
	const float u = Unit(knobQ12) / kStrictAbove;
	return u > 1.0f ? 1.0f : u;
}

static inline bool CvStrictFrom(int32_t knobQ12) { return Unit(knobQ12) >= kStrictAbove; }

static inline int PhraseLengthFrom(int32_t index)
{
	if (index < 0) index = 0;
	if (index > 3) index = 3;
	return kPhraseLengths[index];
}

static inline int PitchClassFromSemitone(int32_t semi) { return static_cast<int>(((semi % 12) + 12) % 12); }

} // namespace tonnetz
