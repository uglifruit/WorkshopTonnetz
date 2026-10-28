// voicing.cpp — exhaustive minimum-movement voice leading.
//
// Integer arithmetic throughout: this runs ~160 times per candidate chord and
// ~100 candidates per plan (depth-2 lookahead), on a core with no FPU. Costs
// are counted in thirtieths of a semitone so every config weight, and the
// centre pull's divide-by-three, stay exact.

#include "voicing.h"

namespace tonnetz {

namespace {

struct IntWeights
{
	int crossing, leapThreshold, leapPenalty, maxSpan, spanPenalty, unison, centre3, centrePull;
};

inline int Round30(float x) { return static_cast<int>(x * 30.0f + (x >= 0.0f ? 0.5f : -0.5f)); }
inline int Absi(int x) { return x < 0 ? -x : x; }

IntWeights Weights(const VoicingConfig &cfg)
{
	IntWeights w;
	w.crossing      = Round30(cfg.crossingPenalty);
	w.leapThreshold = static_cast<int>(cfg.leapThreshold);
	w.leapPenalty   = Round30(cfg.leapPenalty);
	w.maxSpan       = static_cast<int>(cfg.maxSpan);
	w.spanPenalty   = Round30(cfg.spanPenalty);
	w.unison        = Round30(cfg.unisonPenalty);
	w.centre3       = static_cast<int>(cfg.centre * 3.0f + 0.5f);
	// |mean - centre| * pull * 30 == |sum - 3*centre| * pull * 10
	w.centrePull    = static_cast<int>(cfg.centrePull * 10.0f + 0.5f);
	return w;
}

/// Cost in 1/30 semitone. `bound` lets the search stop early once a candidate
/// is already worse than the best found.
inline int Cost30(const int prev[3], const int next[3], const IntWeights &w, int bound)
{
	int cost = 0;
	for (int v = 0; v < 3; v++)
	{
		const int move = Absi(next[v] - prev[v]);
		cost += move * 30;
		if (move > w.leapThreshold) cost += (move - w.leapThreshold) * w.leapPenalty;
	}
	if (cost >= bound) return cost;
	// Voice identity: 0 below 1 below 2. A crossing is allowed if nothing
	// else fits, but costs as much as a large leap.
	if (next[0] >= next[1]) cost += w.crossing;
	if (next[1] >= next[2]) cost += w.crossing;
	if (next[0] == next[1]) cost += w.unison;
	if (next[1] == next[2]) cost += w.unison;
	if (next[0] == next[2]) cost += w.unison;
	const int span = next[2] - next[0];
	if (span > w.maxSpan) cost += (span - w.maxSpan) * w.spanPenalty;
	// Without this, minimum-movement voicing random-walks to an edge of the
	// register and sits there.
	cost += Absi(next[0] + next[1] + next[2] - w.centre3) * w.centrePull;
	return cost;
}

} // namespace

float VoicingCost(const Voicing &prev, const Voicing &next, const VoicingConfig &cfg)
{
	return static_cast<float>(Cost30(prev.note, next.note, Weights(cfg), 0x7FFFFFFF)) / 30.0f;
}

Voicing InitialVoicing(Chord c, const VoicingConfig &cfg)
{
	// Root position stacked from the first root at or above (centre - 7).
	const int base = static_cast<int>(cfg.centre) - 7;
	int root = base + ((c.root - base) % 12 + 12) % 12;
	const int third = root + (c.IsMinor() ? 3 : 4);
	Voicing v{ { root, third, root + 7 } };
	return v;
}

Voicing VoiceLead(const Voicing &prev, Chord c, const VoicingConfig &cfg, float *cost)
{
	const IntWeights w = Weights(cfg);
	int tones[3];
	c.Tones(tones);
	static const int kPerms[6][3] = {
		{ 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 },
	};

	Voicing best = InitialVoicing(c, cfg);
	int bestCost = 0x7FFFFFFF;

	for (const auto &perm : kPerms)
	{
		// Octave options for each voice's pitch class inside the register.
		int opts[3][4];
		int nOpts[3];
		for (int v = 0; v < 3; v++)
		{
			const int pc = tones[perm[v]];
			nOpts[v] = 0;
			int n = cfg.low + ((pc - cfg.low) % 12 + 12) % 12;
			for (; n <= cfg.high && nOpts[v] < 4; n += 12) opts[v][nOpts[v]++] = n;
		}
		for (int a = 0; a < nOpts[0]; a++)
			for (int b = 0; b < nOpts[1]; b++)
				for (int d = 0; d < nOpts[2]; d++)
				{
					const int cand[3] = { opts[0][a], opts[1][b], opts[2][d] };
					const int cst = Cost30(prev.note, cand, w, bestCost);
					if (cst < bestCost)
					{
						bestCost = cst;
						best = Voicing{ { cand[0], cand[1], cand[2] } };
					}
				}
	}
	if (cost) *cost = static_cast<float>(bestCost) / 30.0f;
	return best;
}

} // namespace tonnetz
