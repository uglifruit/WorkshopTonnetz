// tonal.cpp — local scale inference (Window B) and tonal centre (Window C).

#include "tonal.h"
#include <math.h>

namespace tonnetz {

// Interval sets, as masks relative to the root.
static constexpr uint16_t M(int a, int b, int c, int d, int e, int f, int g)
{
	return static_cast<uint16_t>((1u << a) | (1u << b) | (1u << c) | (1u << d) | (1u << e) | (1u << f) | (1u << g));
}

static const uint16_t kScaleIntervals[kNumScaleTypes] = {
	M(0, 2, 4, 5, 7, 9, 11),   // Ionian
	M(0, 2, 3, 5, 7, 8, 10),   // Aeolian
	M(0, 2, 3, 5, 7, 9, 10),   // Dorian
	M(0, 2, 4, 5, 7, 9, 10),   // Mixolydian
	M(0, 2, 3, 5, 7, 8, 11),   // Harmonic minor
	M(0, 2, 4, 5, 7, 8, 11),   // Harmonic major
	M(0, 2, 3, 5, 7, 9, 11),   // Melodic minor (ascending)
};

// Small preferences between collections that explain the evidence equally:
// the common ones first, harmonic minor ahead of melodic minor (Am -> E is
// "A harmonic minor", though A melodic minor also contains every note).
static const float kScalePrior[kNumScaleTypes] = {
	0.020f, 0.020f, 0.000f, 0.000f, 0.015f, 0.000f, 0.005f,
};

const char *ScaleTypeName(int type)
{
	static const char *const kNames[kNumScaleTypes] = {
		"ionian", "aeolian", "dorian", "mixolydian", "harm-minor", "harm-major", "mel-minor",
	};
	return (type >= 0 && type < kNumScaleTypes) ? kNames[type] : "?";
}

uint16_t ScaleMask(int type, int root)
{
	const uint32_t m = kScaleIntervals[type];
	root = ((root % 12) + 12) % 12;
	return static_cast<uint16_t>(((m << root) | (m >> (12 - root))) & 0xFFFu);
}

float KeyFunction(Chord c, int key)
{
	const int tonic = key >> 1;
	const bool minorKey = key & 1;
	const int r = ((c.root - tonic) % 12 + 12) % 12;
	const bool m = c.IsMinor();
	if (!minorKey)
	{
		if (!m)
		{
			switch (r)
			{
			case 0:  return 1.00f;  // I
			case 5:  return 0.80f;  // IV
			case 7:  return 0.85f;  // V
			case 8:  return 0.30f;  // bVI   (borrowed)
			case 10: return 0.30f;  // bVII  (borrowed)
			case 3:  return 0.20f;  // bIII  (borrowed)
			case 2:  return 0.25f;  // II    (V of V)
			case 4:  return 0.20f;  // III   (V of vi)
			case 9:  return 0.15f;  // VI    (V of ii)
			case 1:  return 0.10f;  // bII
			default: return 0.0f;
			}
		}
		switch (r)
		{
		case 9:  return 0.60f;  // vi
		case 2:  return 0.60f;  // ii
		case 4:  return 0.40f;  // iii
		case 5:  return 0.35f;  // iv    (borrowed)
		case 0:  return 0.25f;  // i     (parallel minor)
		default: return 0.0f;
		}
	}
	if (m)
	{
		switch (r)
		{
		case 0:  return 1.00f;  // i
		case 5:  return 0.80f;  // iv
		case 7:  return 0.50f;  // v
		case 2:  return 0.15f;  // ii   (melodic)
		default: return 0.0f;
		}
	}
	switch (r)
	{
	case 7:  return 0.85f;  // V
	case 8:  return 0.60f;  // VI
	case 3:  return 0.55f;  // III
	case 10: return 0.50f;  // VII
	case 5:  return 0.30f;  // IV   (dorian)
	case 0:  return 0.30f;  // I    (picardy)
	case 1:  return 0.25f;  // bII  (Neapolitan)
	default: return 0.0f;
	}
}

void TonalEstimator::Reset(Chord home)
{
	nRecent_ = 0;
	hasPlanned_ = false;
	for (int k = 0; k < kNumChords; k++) keyEvidence_[k] = 0.0f;
	// Seed Window C with home, so a fresh start leans towards it but a couple
	// of contrary chords can still move it.
	keyEvidence_[home.Index()] = 1.0f;
	evidenceWeight_ = 1.0f;
	tonic_ = home.Index();
	UpdateKeys();
	tonic_ = home.Index();
	scaleType_ = home.IsMinor() ? kAeolian : kIonian;
	scaleRoot_ = home.root;
	scaleFit_ = 1.0f;
}

void TonalEstimator::OnChordArrived(Chord c, float metricWeight)
{
	const bool hasPrev = nRecent_ > 0;
	const Chord prev = hasPrev ? recent_[0] : c;

	// Window B: push to the front.
	for (int i = kMaxB - 1; i > 0; i--) recent_[i] = recent_[i - 1];
	recent_[0] = c;
	if (nRecent_ < kMaxB) nRecent_++;
	hasPlanned_ = false;

	// Window C: decay, then add this chord's evidence for every key.
	const float decay = Lerp(cfg_->decayC, inertia_);
	evidenceWeight_ = evidenceWeight_ * decay + metricWeight;
	for (int k = 0; k < kNumChords; k++)
	{
		float e = KeyFunction(c, k) * metricWeight;
		const Chord tonicTriad = Chord::FromIndex(k);
		if (hasPrev && c == tonicTriad)
		{
			const int prevRel = ((prev.root - tonicTriad.root) % 12 + 12) % 12;
			if (prevRel == 7 && !prev.IsMinor()) e += cfg_->cadenceBonus;     // V -> I / V -> i
			else if (prevRel == 7)               e += cfg_->fifthBonus;       // v -> i
			else if (prevRel == 5)               e += cfg_->plagalBonus;      // IV/iv -> I
		}
		keyEvidence_[k] = keyEvidence_[k] * decay + e;
	}
	UpdateKeys();
	UpdateScale();
}

void TonalEstimator::SetPlanned(Chord next)
{
	planned_ = next;
	hasPlanned_ = true;
	UpdateScale();
}

void TonalEstimator::UpdateKeys()
{
	// Softmax over evidence normalised by how much evidence has accumulated,
	// so the sharpness does not depend on the decay setting. Subtracting the
	// max keeps exp() in range.
	const float norm = evidenceWeight_ > 1e-6f ? 1.0f / evidenceWeight_ : 1.0f;
	float maxE = -1e30f;
	for (int k = 0; k < kNumChords; k++)
		if (keyEvidence_[k] > maxE) maxE = keyEvidence_[k];
	float sum = 0.0f;
	for (int k = 0; k < kNumChords; k++)
	{
		keyProb_[k] = expf(cfg_->beta * (keyEvidence_[k] - maxE) * norm);
		sum += keyProb_[k];
	}
	int best = 0;
	for (int k = 0; k < kNumChords; k++)
	{
		keyProb_[k] /= sum;
		if (keyProb_[k] > keyProb_[best]) best = k;
	}
	// Inertia: the reported tonic only moves when the challenger clearly wins.
	const float margin = 1.0f + Lerp(cfg_->tonicMargin, inertia_);
	if (best != tonic_ && keyProb_[best] > keyProb_[tonic_] * margin) tonic_ = best;
}

void TonalEstimator::Evidence(float ev[12]) const
{
	for (int pc = 0; pc < 12; pc++) ev[pc] = 0.0f;
	float w = 1.0f;
	const int n = nRecent_ < cfg_->windowB ? nRecent_ : cfg_->windowB;
	for (int i = 0; i < n; i++)
	{
		int t[3];
		recent_[i].Tones(t);
		for (int j = 0; j < 3; j++) ev[t[j]] += w;
		w *= cfg_->decayB;
	}
	if (hasPlanned_)
	{
		int t[3];
		planned_.Tones(t);
		for (int j = 0; j < 3; j++) ev[t[j]] += cfg_->plannedWeight;
	}
}

float TonalEstimator::Fit(const float ev[12], uint16_t mask) const
{
	float in = 0.0f, out = 0.0f;
	for (int pc = 0; pc < 12; pc++)
	{
		if ((mask >> pc) & 1u) in += ev[pc];
		else out += ev[pc];
	}
	const float total = in + out;
	if (total <= 0.0f) return 0.0f;
	return (in - cfg_->outsidePenalty * out) / total;
}

float TonalEstimator::ScaleFit(int type, int root) const
{
	float ev[12];
	Evidence(ev);
	return Fit(ev, ScaleMask(type, root));
}

void TonalEstimator::UpdateScale()
{
	if (nRecent_ == 0) return;
	const int tonicPc = tonic_ >> 1;
	const int chordRoot = recent_[0].root;
	float ev[12];
	Evidence(ev);   // once, not once per candidate scale

	auto score = [&](int type, int root) {
		float s = Fit(ev, ScaleMask(type, root)) + kScalePrior[type];
		// Among collections with the same notes, prefer the mode built on the
		// tonal centre, then the one built on the current chord root.
		if (root == tonicPc) s += 0.02f;
		if (root == chordRoot) s += 0.01f;
		return s;
	};

	int bestT = scaleType_, bestR = scaleRoot_;
	float best = -1e30f;
	for (int t = 0; t < kNumScaleTypes; t++)
		for (int r = 0; r < 12; r++)
		{
			const float s = score(t, r);
			if (s > best) { best = s; bestT = t; bestR = r; }
		}
	const float current = score(scaleType_, scaleRoot_);
	const float margin = Lerp(cfg_->scaleMargin, inertia_);
	if ((bestT != scaleType_ || bestR != scaleRoot_) && best > current + margin)
	{
		scaleType_ = bestT;
		scaleRoot_ = bestR;
	}
	scaleFit_ = Fit(ev, ScaleMask(scaleType_, scaleRoot_));
}

} // namespace tonnetz
