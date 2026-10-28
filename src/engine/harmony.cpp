// harmony.cpp — candidate scoring, lookahead and stochastic selection.

#include "harmony.h"
#include <math.h>

namespace tonnetz {

static inline float Absf(float x) { return x < 0.0f ? -x : x; }
static inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

void HarmonyEngine::Init(const EngineConfig *cfg, Rng *rng, TonalEstimator *tonal)
{
	cfg_ = cfg;
	rng_ = rng;
	tonal_ = tonal;
	haveVoicing_ = false;
	Reset();
}

void HarmonyEngine::DeriveWeights()
{
	const HarmonyConfig &h = cfg_->harmony;
	const float w = Clamp01(params_.wander);
	temperature_ = Lerp(h.temperature, w);
	wVL_         = Lerp(h.wVoiceLeading, w);
	wGrav_       = Lerp(h.wGravity, w);
	wPhrase_     = Lerp(h.wPhrase, w);
	wNovelty_    = Lerp(h.wNovelty, w);
	maxNovelty_  = w < h.novelty1Wander ? 0 : (w < h.novelty2Wander ? 1 : 2);
}

void HarmonyEngine::Reset()
{
	home_ = Chord(cfg_->harmony.homeRoot, cfg_->harmony.homeMinor ? kMinor : kMajor);
	const Voicing from = haveVoicing_ ? voicing_ : InitialVoicing(home_, cfg_->voicing);
	voicing_ = haveVoicing_ ? VoiceLead(from, home_, cfg_->voicing) : from;
	haveVoicing_ = true;

	nHist_ = 1;
	hist_[0] = home_;
	histMove_[0] = -1;
	phrasePos_ = 0;
	DeriveWeights();
	StartPhrase();

	tonal_->Reset(home_);
	tonal_->OnChordArrived(home_, 1.3f);
	committed_ = false;
	PlanNext(false);
}

void HarmonyEngine::ForceCurrent(Chord c)
{
	for (int i = kHistory - 1; i > 0; i--) { hist_[i] = hist_[i - 1]; histMove_[i] = histMove_[i - 1]; }
	hist_[0] = c;
	histMove_[0] = -1;
	if (nHist_ < kHistory) nHist_++;
	voicing_ = VoiceLead(voicing_, c, cfg_->voicing);
	committed_ = false;
	PlanNext(false);
}

void HarmonyEngine::SetParams(const HarmonyParams &p)
{
	const bool cvChanged = p.cvActive != params_.cvActive || p.cvStrict != params_.cvStrict
		|| (p.cvActive && p.cvPc != params_.cvPc)
		|| (p.cvActive && Absf(p.cvAttraction - params_.cvAttraction) > 0.1f);
	params_ = p;
	DeriveWeights();
	if (cvChanged && !committed_) PlanNext(true);
}

void HarmonyEngine::StartPhrase()
{
	const HarmonyConfig &h = cfg_->harmony;
	const float w = Clamp01(params_.wander);
	phraseLen_ = params_.phraseLength < 2 ? 2 : params_.phraseLength;

	peakDistance_ = Lerp(h.peakDistance, w) + (rng_->Uniform() * 2.0f - 1.0f) * h.peakJitter;
	if (peakDistance_ < 1.0f) peakDistance_ = 1.0f;
	peakPos_ = h.peakPos + (rng_->Uniform() * 2.0f - 1.0f) * h.peakPosJitter;

	float cw[4];
	for (int i = 0; i < 4; i++) cw[i] = h.corridorLo[i] + (h.corridorHi[i] - h.corridorLo[i]) * w;
	corridor_ = rng_->Pick(cw, 4);
	motif_ = rng_->Chance(h.motifProb);
}

float HarmonyEngine::PhraseTarget(int pos) const
{
	// Distance-from-home contour: 0 at the phrase start, rising to the peak at
	// peakPos, falling back to 0 (resolution) at the last chord.
	if (phraseLen_ < 2) return 0.0f;
	const float u = static_cast<float>(pos) / static_cast<float>(phraseLen_ - 1);
	const float halfPi = 1.5707963f;
	if (u <= peakPos_) return peakDistance_ * sinf(halfPi * u / peakPos_);
	return peakDistance_ * cosf(halfPi * (u - peakPos_) / (1.0f - peakPos_));
}

float HarmonyEngine::Tension() const
{
	return Clamp01(static_cast<float>(Distance(Current(), home_)) / 5.0f);
}

HarmonyEngine::Context HarmonyEngine::MakeContext() const
{
	Context ctx;
	for (int i = 0; i < nHist_; i++) { ctx.hist[i] = hist_[i]; ctx.histMove[i] = histMove_[i]; }
	ctx.nHist = nHist_;
	ctx.voicing = voicing_;
	ctx.pos = (phrasePos_ + 1) % phraseLen_;
	return ctx;
}

// ---------------------------------------------------------------------------
// Scoring. Every term is a contribution to a log-probability-like score; the
// softmax turns the sum into probabilities. Positive = more likely.
// ---------------------------------------------------------------------------
void HarmonyEngine::Score(const Context &ctx, Candidate &c, float cvScale) const
{
	const HarmonyConfig &h = cfg_->harmony;
	const float w = Clamp01(params_.wander);

	// 1. Voice-leading efficiency: total semitone movement plus voicing penalties.
	float vlCost = 0.0f;
	c.voicing = VoiceLead(ctx.voicing, c.chord, cfg_->voicing, &vlCost);
	c.vl = -wVL_ * vlCost * 0.25f;

	// 2. Tonal gravity: fit with the INFERRED key (as a distribution, so an
	//    uncertain estimate spreads its pull) and with the explicit HOME key.
	const float *kp = tonal_->KeyProbs();
	float inferred = 0.0f;
	for (int k = 0; k < kNumChords; k++)
		if (kp[k] > 0.01f) inferred += kp[k] * KeyFunction(c.chord, k);
	const float homeFn = KeyFunction(c.chord, home_.Index());
	const float share = Lerp(h.homeShare, w);
	c.gravity = wGrav_ * ((1.0f - share) * inferred + share * homeFn);

	// 5. Phrase structure: follow the departure/return contour, resolve at the end.
	const float target = PhraseTarget(ctx.pos);
	const float dist = static_cast<float>(Distance(c.chord, home_));
	float phrase = -Absf(dist - target) * 0.66f;
	const float resolveScale = 1.0f - 0.6f * w;   // resolution is optional at high wander
	if (ctx.pos == phraseLen_ - 1)
	{
		if (c.chord == home_) phrase += h.resolveBonus * resolveScale;
		else if (homeFn >= 0.6f) phrase += h.resolveBonus * 0.25f * resolveScale;
	}
	if (ctx.pos == phraseLen_ - 2 && !c.chord.IsMinor() && c.chord.root == (home_.root + 7) % 12)
		phrase += h.dominantBonus * resolveScale;
	c.phrase = wPhrase_ * phrase;

	// 7. External pitch: MEMBERSHIP (the chord contains the note) and TONIC
	//    attraction (the chord is built on it) are separate terms.
	c.cv = 0.0f;
	if (params_.cvActive)
	{
		const float a = params_.cvAttraction * cvScale;
		if (c.chord.Contains(params_.cvPc)) c.cv += a * h.wCvMembership;
		if (c.chord.root == params_.cvPc)   c.cv += a * h.wCvTonic;
	}

	// 8. Novelty: compound moves are discouraged when settled, mildly
	//    favoured when wandering.
	c.novelty = wNovelty_ * static_cast<float>(kMoves[c.move].novelty);

	// 6. Recent repetition (beyond the immediate reversal, handled below).
	float rep = 0.0f, age = 1.0f;
	for (int i = 2; i < ctx.nHist; i++)
	{
		if (ctx.hist[i] == c.chord) rep += age;
		age *= h.repetitionDecay;
	}
	c.repetition = -h.wRepetition * rep;

	// 4. Transformation history: P, L and R are involutions, so going straight
	//    back is the trivial two-chord oscillation. Penalised, not forbidden;
	//    in a "motif" phrase the back-and-forth is allowed as a gesture.
	c.reversal = 0.0f;
	if (ctx.nHist >= 2 && c.chord == ctx.hist[1])
		c.reversal = -h.wReversal * (motif_ ? h.motifReversalScale : 1.0f);

	// Cycles: continuing this phrase's chosen alternation (R/L diatonic
	// corridor and fifths cycle, P/L hexatonic, P/R octatonic).
	c.cycle = 0.0f;
	if (corridor_ != 0 && kMoves[c.move].len == 1)
	{
		static const Op kPairs[4][2] = { { kP, kP }, { kR, kL }, { kP, kL }, { kP, kR } };
		const Op op = kMoves[c.move].ops[0];
		const Op a = kPairs[corridor_][0], b = kPairs[corridor_][1];
		const int lastMove = ctx.histMove[0];
		if (op == a || op == b)
		{
			if (lastMove < 0) c.cycle = h.wCycle * 0.5f;
			else if (kMoves[lastMove].len == 1)
			{
				const Op last = kMoves[lastMove].ops[0];
				if ((last == a || last == b) && last != op) c.cycle = h.wCycle;
			}
		}
	}

	c.score = c.vl + c.gravity + c.phrase + c.cv + c.novelty + c.repetition + c.reversal + c.cycle;
}

int HarmonyEngine::Generate(const Context &ctx, float cvScale, Candidate *out) const
{
	const Chord cur = ctx.hist[0];
	int n = 0;
	auto addMoves = [&](int maxNov) {
		for (int m = 0; m < kNumMoves; m++)
		{
			if (kMoves[m].novelty > maxNov) continue;
			const Chord d = ApplyMove(cur, kMoves[m]);
			if (d == cur) continue;
			int existing = -1;
			for (int i = 0; i < n; i++)
				if (out[i].chord == d) { existing = i; break; }
			if (existing >= 0)
			{
				// Keep the shortest route as the label.
				if (kMoves[m].len < kMoves[out[existing].move].len) out[existing].move = m;
				continue;
			}
			out[n].chord = d;
			out[n].move = m;
			n++;
		}
	};
	addMoves(maxNovelty_);

	// Strict CV: only chords containing the requested pitch class. Widen to
	// every move if the usual vocabulary cannot reach one.
	const bool strict = params_.cvActive && params_.cvStrict && cvScale >= 1.0f;
	if (strict)
	{
		auto filter = [&]() {
			int k = 0;
			for (int i = 0; i < n; i++)
				if (out[i].chord.Contains(params_.cvPc)) out[k++] = out[i];
			return k;
		};
		Candidate base[kMaxMoves];
		const int nBase = n;
		for (int i = 0; i < n; i++) base[i] = out[i];
		int k = filter();
		if (k == 0)
		{
			n = 0;
			addMoves(2);
			k = filter();
		}
		if (k == 0)
		{
			// Not reachable in one move: keep the usual vocabulary, with a
			// bonus for chords from which it IS reachable next time.
			n = nBase;
			for (int i = 0; i < n; i++) out[i] = base[i];
			for (int i = 0; i < n; i++)
			{
				Score(ctx, out[i], cvScale);
				for (int m = 0; m < kNumMoves; m++)
					if (ApplyMove(out[i].chord, kMoves[m]).Contains(params_.cvPc))
					{
						out[i].score += cfg_->harmony.wCvMembership * params_.cvAttraction;
						break;
					}
			}
			return n;
		}
		n = k;
	}

	for (int i = 0; i < n; i++) Score(ctx, out[i], cvScale);
	return n;
}

float HarmonyEngine::SoftValue(const Context &ctx, const Candidate &c) const
{
	// One step further: a temperature-weighted soft maximum over where we could
	// go FROM c. A destination with only poor continuations loses value.
	Context next;
	next.hist[0] = c.chord;
	next.histMove[0] = c.move;
	const int keep = ctx.nHist < kHistory - 1 ? ctx.nHist : kHistory - 1;
	for (int i = 0; i < keep; i++) { next.hist[i + 1] = ctx.hist[i]; next.histMove[i + 1] = ctx.histMove[i]; }
	next.nHist = keep + 1;
	next.voicing = c.voicing;
	next.pos = (ctx.pos + 1) % phraseLen_;

	Candidate c2[kMaxMoves];
	const int n = Generate(next, 0.5f, c2);   // CV counts half for n+2
	if (n == 0) return -10.0f;
	float maxS = -1e30f;
	for (int i = 0; i < n; i++) if (c2[i].score > maxS) maxS = c2[i].score;
	float sum = 0.0f;
	for (int i = 0; i < n; i++) sum += expf((c2[i].score - maxS) / temperature_);
	return maxS + temperature_ * logf(sum / static_cast<float>(n));
}

void HarmonyEngine::PlanNext(bool keepIfClose)
{
	const HarmonyConfig &h = cfg_->harmony;
	const Context ctx = MakeContext();
	nCands_ = Generate(ctx, 1.0f, cands_);
	if (nCands_ == 0)
	{
		// Cannot happen with P/L/R available, but never leave next_ stale.
		next_.chord = ApplyOp(Current(), kR);
		next_.move = MoveIndexOfOp(kR);
		next_.voicing = VoiceLead(voicing_, next_.chord, cfg_->voicing);
		tonal_->SetPlanned(next_.chord);
		return;
	}

	// Depth-2 lookahead on the strongest immediate candidates. The rest get
	// the worst future value seen, so the beam never flatters an unexplored
	// candidate.
	int order[kMaxMoves];
	for (int i = 0; i < nCands_; i++) order[i] = i;
	for (int i = 1; i < nCands_; i++)
		for (int j = i; j > 0 && cands_[order[j]].score > cands_[order[j - 1]].score; j--)
		{
			const int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
		}
	const int beam = h.beamWidth < nCands_ ? h.beamWidth : nCands_;
	float worstFuture = 1e30f;
	float future[kMaxMoves];
	for (int i = 0; i < beam; i++)
	{
		future[order[i]] = SoftValue(ctx, cands_[order[i]]);
		if (future[order[i]] < worstFuture) worstFuture = future[order[i]];
	}
	for (int i = beam; i < nCands_; i++) future[order[i]] = worstFuture;

	// Softmax with temperature, max subtracted for numerical stability:
	//   p_i = exp((v_i - v_max) / T) / sum_j exp((v_j - v_max) / T)
	float maxV = -1e30f;
	for (int i = 0; i < nCands_; i++)
	{
		cands_[i].value = cands_[i].score + h.futureDiscount * future[i];
		if (cands_[i].value > maxV) maxV = cands_[i].value;
	}
	float sum = 0.0f;
	float probs[kMaxMoves];
	for (int i = 0; i < nCands_; i++)
	{
		probs[i] = expf((cands_[i].value - maxV) / temperature_);
		sum += probs[i];
	}
	float bestP = 0.0f;
	int keepIdx = -1;
	for (int i = 0; i < nCands_; i++)
	{
		probs[i] /= sum;
		cands_[i].prob = probs[i];
		if (probs[i] > bestP) bestP = probs[i];
		if (keepIfClose && cands_[i].chord == next_.chord) keepIdx = i;
	}

	// Replan hysteresis: a destination that is still a strong option is kept,
	// so a wobbling CV does not keep flipping the plan the melody leans on.
	if (keepIdx >= 0 && probs[keepIdx] >= h.replanKeepRatio * bestP)
	{
		next_ = cands_[keepIdx];
	}
	else
	{
		next_ = cands_[rng_->Pick(probs, nCands_)];
	}
	tonal_->SetPlanned(next_.chord);
}

void HarmonyEngine::Advance()
{
	const HarmonyConfig &h = cfg_->harmony;
	for (int i = kHistory - 1; i > 0; i--) { hist_[i] = hist_[i - 1]; histMove_[i] = histMove_[i - 1]; }
	hist_[0] = next_.chord;
	histMove_[0] = next_.move;
	if (nHist_ < kHistory) nHist_++;
	voicing_ = next_.voicing;

	phrasePos_++;
	const bool newPhrase = phrasePos_ >= phraseLen_;
	if (newPhrase) phrasePos_ = 0;
	const float metric = phrasePos_ == 0 ? 1.3f : (phrasePos_ == phraseLen_ - 1 ? 1.5f : 1.0f);
	tonal_->OnChordArrived(hist_[0], metric);

	if (newPhrase)
	{
		// At low gravity the card may decide it now lives somewhere else: the
		// inferred tonic if it is confident and not home, otherwise the chord
		// the phrase actually came to rest on, if that is well away from home.
		const float w = Clamp01(params_.wander);
		if (w > h.migrateWander && rng_->Chance((w - h.migrateWander) * h.migrateRate))
		{
			const Chord inferred = Chord::FromIndex(tonal_->Tonic());
			if (inferred != home_ && tonal_->TonicConfidence() >= h.migrateMinConf) home_ = inferred;
			else if (Distance(hist_[0], home_) >= 2) home_ = hist_[0];
		}
		StartPhrase();
	}
	committed_ = false;
	PlanNext(false);
}

} // namespace tonnetz
