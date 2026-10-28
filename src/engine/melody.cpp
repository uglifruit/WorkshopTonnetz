// melody.cpp — pitch-class weighting, note choice and rhythm.

#include "melody.h"
#include <math.h>

namespace tonnetz {

static inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static inline float Smoothstep(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }
static inline int   Absi(int x) { return x < 0 ? -x : x; }

void MelodyEngine::Init(const MelodyConfig *cfg, Rng *rng)
{
	cfg_ = cfg;
	rng_ = rng;
	for (int n = 0; n < 128; n++)
	{
		const float z = (static_cast<float>(n) - cfg_->registerCentre) / cfg_->registerWidth;
		registerWeight_[n] = 0.15f + 0.85f * expf(-0.5f * z * z);
	}
	Reset();
}

void MelodyEngine::Reset()
{
	sounding_ = false;
	note_ = 0;
	ticksLeft_ = 0;
	lastNote_ = -1;
	lastInterval_ = 0;
	lastWasNonChord_ = false;
	suspension_ = false;
	for (int &r : recent_) r = -1;
}

int MelodyEngine::GridTicks(float rhythm)
{
	if (rhythm < 0.33f) return 4;
	if (rhythm < 0.75f) return 2;
	return 1;
}

float MelodyEngine::Metric(int t)
{
	if (t % 16 == 0) return 1.00f;   // chord downbeat
	if (t % 4 == 0)  return 0.80f;   // beat
	if (t % 2 == 0)  return 0.55f;   // eighth offbeat
	return 0.35f;                    // sixteenth
}

float MelodyEngine::NextWeight(float phase, float anticipation) const
{
	return cfg_->initialNextWeight
		+ Clamp01(anticipation) * Smoothstep(phase) * (cfg_->maximumNextWeight - cfg_->initialNextWeight);
}

void MelodyEngine::PitchClassWeights(const MelodyContext &ctx, const MelodyParams &p, bool strong, float w[12]) const
{
	const float t = Smoothstep(ctx.phase);
	const float wNext = NextWeight(ctx.phase, p.anticipation);
	const float edge = (ctx.nextStartsPhrase && ctx.phase > 0.5f) ? cfg_->phraseEndBoost : 1.0f;

	for (int pc = 0; pc < 12; pc++) w[pc] = 0.0f;

	// Inferred local scale (Window B). When no seven-note collection fits,
	// do not pretend: every pitch class gets a token weight and the line
	// relies on chord tones and chromatic approaches below.
	if (!ctx.chromatic)
	{
		for (int pc = 0; pc < 12; pc++)
			if ((ctx.scaleMask >> pc) & 1u) w[pc] += cfg_->scaleWeight * ctx.scaleConfidence;
	}
	else
	{
		for (int pc = 0; pc < 12; pc++) w[pc] += 0.03f;
	}

	// The current chord eases a little as the next one takes over; curFloor
	// below guarantees it never fades too far.
	int ct[3];
	ctx.cur.Tones(ct);
	const float curW = cfg_->curWeight * (1.0f - cfg_->curEase * Clamp01(p.anticipation) * t);
	for (int i = 0; i < 3; i++) w[ct[i]] += curW;

	if (ctx.hasPrev)
	{
		int pt[3];
		ctx.prev.Tones(pt);
		// Tones shared with the previous chord are the connective tissue.
		for (int i = 0; i < 3; i++) w[pt[i]] += cfg_->prevWeight * (ctx.cur.Contains(pt[i]) ? 1.0f : 0.5f);
	}

	int nt[3];
	ctx.next.Tones(nt);
	for (int i = 0; i < 3; i++)
	{
		if (ctx.cur.Contains(nt[i])) w[nt[i]] += wNext;   // common tone: safe now AND later
		else w[nt[i]] += wNext * (strong ? cfg_->strongAnticipation : 1.0f) * edge;
	}

	// Approach notes: a semitone (strongly) or tone (weakly) either side of a
	// next-chord tone that MOVES — a tone the next chord shares with this one
	// needs no approach. In C -> Em -> F this lights up E (into F) and B (into
	// C), the two classic leading tones.
	const float a = cfg_->approachWeight * Clamp01(p.anticipation) * t * (strong ? 0.5f : 1.0f) * edge;
	if (a > 0.0f)
	{
		static const int   kOff[4] = { -1, 1, -2, 2 };
		static const float kW[4]   = { 1.0f, 1.0f, 0.25f, 0.25f };
		for (int i = 0; i < 3; i++)
		{
			if (ctx.cur.Contains(nt[i])) continue;
			for (int k = 0; k < 4; k++)
			{
				const int pc = ((nt[i] + kOff[k]) % 12 + 12) % 12;
				if (ctx.next.Contains(pc)) continue;
				const bool fits = ctx.cur.Contains(pc) || ((ctx.scaleMask >> pc) & 1u);
				w[pc] += a * kW[k] * (fits ? 1.0f : cfg_->chromaticApproach);
			}
		}
	}

	// Strong ticks favour stable notes.
	if (strong)
		for (int pc = 0; pc < 12; pc++)
			if (!ctx.cur.Contains(pc)) w[pc] *= cfg_->strongNonChord;

	// The current chord keeps a significant presence however strong the pull
	// of the next one.
	float curSum = 0.0f, total = 0.0f;
	for (int pc = 0; pc < 12; pc++)
	{
		total += w[pc];
		if (ctx.cur.Contains(pc)) curSum += w[pc];
	}
	const float other = total - curSum;
	if (curSum > 0.0f && curSum < cfg_->curFloor * total)
	{
		const float want = cfg_->curFloor * other / (1.0f - cfg_->curFloor);
		const float k = want / curSum;
		for (int pc = 0; pc < 12; pc++)
			if (ctx.cur.Contains(pc)) w[pc] *= k;
		total = want + other;
	}
	for (int pc = 0; pc < 12; pc++) w[pc] /= total;
}

static float IntervalWeight(int d)
{
	switch (d)
	{
	case 0:  return 0.30f;
	case 1:
	case 2:  return 1.00f;
	case 3:
	case 4:  return 0.50f;
	case 5:  return 0.18f;
	case 6:  return 0.05f;
	case 7:  return 0.12f;
	case 8:
	case 9:  return 0.03f;
	case 10:
	case 11: return 0.01f;
	case 12: return 0.03f;
	default: return 0.0f;
	}
}

int MelodyEngine::ChooseNote(const MelodyContext &ctx, const MelodyParams &p, bool strong)
{
	float pcw[12];
	PitchClassWeights(ctx, p, strong, pcw);

	constexpr int kMaxSpan = 49;   // four octaves
	float w[kMaxSpan];
	const int lo = cfg_->low;
	const int n = (cfg_->high - lo + 1) < kMaxSpan ? (cfg_->high - lo + 1) : kMaxSpan;
	for (int i = 0; i < n; i++)
	{
		const int note = lo + i;
		float x = pcw[note % 12];
		if (lastNote_ >= 0)
		{
			const int d = Absi(note - lastNote_);
			x *= IntervalWeight(d);
			// After a leap, prefer a step back the other way.
			if (Absi(lastInterval_) > 4 && d >= 1 && d <= 2 && ((note - lastNote_) > 0) != (lastInterval_ > 0))
				x *= 1.8f;
			// A step tends to continue as a run in the same direction.
			if (Absi(lastInterval_) >= 1 && Absi(lastInterval_) <= 2 && d >= 1 && d <= 2
				&& ((note - lastNote_) > 0) == (lastInterval_ > 0))
				x *= 1.3f;
			// A non-chord tone resolves by step onto the chord.
			if (lastWasNonChord_ && !ctx.cur.Contains(lastNote_ % 12) && ctx.cur.Contains(note % 12) && d >= 1 && d <= 2)
				x *= 2.5f;
		}
		x *= registerWeight_[note & 127];
		for (int r : recent_)
			if (r >= 0 && r % 12 == note % 12) x *= 0.75f;
		w[i] = x;
	}
	return lo + rng_->Pick(w, n);
}

int MelodyEngine::ChooseDuration(const MelodyParams &p, int grid)
{
	static const int kUnits[6] = { 1, 2, 3, 4, 6, 8 };
	static const float kLog2[6] = { 0.0f, 1.0f, 1.585f, 2.0f, 2.585f, 3.0f };
	const float target = 1.6f - 1.6f * Clamp01(p.density);   // log2 of grid units
	float w[6];
	for (int i = 0; i < 6; i++)
	{
		const float d = kLog2[i] - target;
		w[i] = expf(-1.5f * (d < 0.0f ? -d : d));
	}
	int ticks = kUnits[rng_->Pick(w, 6)] * grid;
	return ticks > kTicksPerChord ? kTicksPerChord : ticks;
}

MelodyEvent MelodyEngine::Tick(const MelodyContext &ctx, const MelodyParams &p)
{
	MelodyEvent ev;
	const int grid = GridTicks(p.rhythm);
	const bool onGrid = (ctx.tickInChord % grid) == 0;
	const bool strong = (ctx.tickInChord % 8) == 0;

	if (sounding_)
	{
		ticksLeft_--;
		if (ctx.chordBoundary)
		{
			const int pc = note_ % 12;
			bool canSuspend = false;
			for (int d = 1; d <= 2; d++)
				if (ctx.cur.Contains(pc - d + 12)) canSuspend = true;
			if (ctx.cur.Contains(pc))
			{
				lastWasNonChord_ = false;           // a common tone, held across the change
			}
			else if (canSuspend && !suspension_ && rng_->Chance(cfg_->suspensionProb))
			{
				suspension_ = true;                 // hold it against the new chord...
				ticksLeft_ = grid;
			}
			else
			{
				ticksLeft_ = 0;                     // otherwise it must move now
			}
		}
		if (ticksLeft_ > 0) return ev;

		if (suspension_)
		{
			// ...then resolve it down by step onto the chord, without re-attacking.
			suspension_ = false;
			for (int d = 1; d <= 2; d++)
			{
				const int target = note_ - d;
				if (target >= cfg_->low && ctx.cur.Contains(target % 12))
				{
					lastInterval_ = target - note_;
					note_ = lastNote_ = target;
					ticksLeft_ = ChooseDuration(p, grid);
					lastWasNonChord_ = false;
					recent_[2] = recent_[1]; recent_[1] = recent_[0]; recent_[0] = note_;
					ev.action = kMelLegato;
					ev.note = static_cast<uint8_t>(note_);
					return ev;
				}
			}
		}
	}

	if (!onGrid && !ctx.chordBoundary)
	{
		if (sounding_) { sounding_ = false; ev.action = kMelOff; }
		return ev;
	}

	float pOn = Clamp01(0.10f + p.density * 1.20f * (0.40f + 0.60f * Metric(ctx.tickInChord)));
	if (ctx.lastChordOfPhrase && ctx.phase > 0.75f) pOn *= cfg_->phraseEndRest;   // let the phrase breathe

	if (rng_->Chance(pOn))
	{
		const int note = ChooseNote(ctx, p, strong);
		const bool legato = sounding_ && note != note_ && Absi(note - note_) <= 2 && rng_->Chance(cfg_->legatoProb);
		lastInterval_ = lastNote_ >= 0 ? note - lastNote_ : 0;
		lastNote_ = note_ = note;
		sounding_ = true;
		ticksLeft_ = ChooseDuration(p, grid);
		lastWasNonChord_ = !ctx.cur.Contains(note % 12);
		recent_[2] = recent_[1]; recent_[1] = recent_[0]; recent_[0] = note;
		ev.action = legato ? kMelLegato : kMelOn;
		ev.note = static_cast<uint8_t>(note);
		return ev;
	}
	if (sounding_) { sounding_ = false; ev.action = kMelOff; }
	return ev;
}

} // namespace tonnetz
