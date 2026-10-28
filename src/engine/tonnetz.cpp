// tonnetz.cpp — P/L/R, the compound-move table, and graph distance.

#include "tonnetz.h"

namespace tonnetz {

Chord ApplyOp(Chord c, Op op)
{
	const int r = c.root;
	switch (op)
	{
	case kP: return Chord(r, c.IsMinor() ? kMajor : kMinor);
	case kL: return c.IsMinor() ? Chord(r + 8, kMajor) : Chord(r + 4, kMinor);
	case kR: return c.IsMinor() ? Chord(r + 3, kMajor) : Chord(r + 9, kMinor);
	}
	return c;
}

// What each move does to C major (and, mirrored, to a minor chord):
//
//   P    C -> Cm      L    C -> Em      R    C -> Am           novelty 0
//   LR   C -> G       RL   C -> F       (the diatonic fifths)  novelty 0
//   RLR  C -> Dm                        (diatonic ii)          novelty 1
//   PL   C -> Ab      LP   C -> E                              novelty 1
//   PR   C -> Eb      RP   C -> A                              novelty 1
//   RLP  C -> Fm      ("N", Nebenverwandt: borrowed minor iv)  novelty 1
//   LPR  C -> C#m     ("S", slide)                             novelty 2
//   LPL  C -> Abm     ("H", hexatonic pole)                    novelty 2
//
// Several compounds can reach the same chord as another route; the engine
// keeps the shortest route per destination as its label, so the Move a
// candidate carries is always the most direct way the Tonnetz connects them.
const Move kMoves[] = {
	{ "P",   1, { kP, kP, kP }, 0 },
	{ "L",   1, { kL, kP, kP }, 0 },
	{ "R",   1, { kR, kP, kP }, 0 },
	{ "LR",  2, { kL, kR, kP }, 0 },
	{ "RL",  2, { kR, kL, kP }, 0 },
	{ "RLR", 3, { kR, kL, kR }, 1 },
	{ "PL",  2, { kP, kL, kP }, 1 },
	{ "LP",  2, { kL, kP, kP }, 1 },
	{ "PR",  2, { kP, kR, kP }, 1 },
	{ "RP",  2, { kR, kP, kP }, 1 },
	{ "RLP", 3, { kR, kL, kP }, 1 },
	{ "LPR", 3, { kL, kP, kR }, 2 },
	{ "LPL", 3, { kL, kP, kL }, 2 },
};
const int kNumMoves = static_cast<int>(sizeof(kMoves) / sizeof(kMoves[0]));
static_assert(sizeof(kMoves) / sizeof(kMoves[0]) <= kMaxMoves, "raise kMaxMoves");

Chord ApplyMove(Chord c, const Move &m)
{
	for (int i = 0; i < m.len; i++) c = ApplyOp(c, m.ops[i]);
	return c;
}

int MoveIndexOfOp(Op op) { return static_cast<int>(op); }   // kMoves[0..2] are P, L, R

static uint8_t gDist[kNumChords][kNumChords];
static bool gDistReady = false;

void TonnetzInit()
{
	// Breadth-first search from every chord over single P/L/R edges.
	for (int s = 0; s < kNumChords; s++)
	{
		for (int i = 0; i < kNumChords; i++) gDist[s][i] = 0xFF;
		int queue[kNumChords];
		int qh = 0, qt = 0;
		gDist[s][s] = 0;
		queue[qt++] = s;
		while (qh < qt)
		{
			const int u = queue[qh++];
			for (int op = 0; op < 3; op++)
			{
				const int v = ApplyOp(Chord::FromIndex(u), static_cast<Op>(op)).Index();
				if (gDist[s][v] == 0xFF)
				{
					gDist[s][v] = static_cast<uint8_t>(gDist[s][u] + 1);
					queue[qt++] = v;
				}
			}
		}
	}
	gDistReady = true;
}

int Distance(Chord a, Chord b)
{
	if (!gDistReady) TonnetzInit();
	return gDist[a.Index()][b.Index()];
}

int CommonTones(Chord a, Chord b)
{
	uint16_t m = static_cast<uint16_t>(a.Mask() & b.Mask());
	int n = 0;
	while (m) { n += m & 1u; m >>= 1; }
	return n;
}

const char *ChordName(Chord c, char *buf)
{
	static const char *const kNames[12] = {
		"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
	};
	int i = 0;
	for (const char *p = kNames[c.root]; *p; p++) buf[i++] = *p;
	if (c.IsMinor()) buf[i++] = 'm';
	buf[i] = 0;
	return buf;
}

} // namespace tonnetz
