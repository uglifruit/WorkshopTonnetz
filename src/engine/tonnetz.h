// tonnetz.h — the 24 major/minor triads and the neo-Riemannian moves between them.
//
// A Chord is (root pitch class, quality). Pitch classes are integers mod 12,
// C = 0. Chords are indexed 0..23 as root*2 + quality.
//
// TRANSFORMATION ORDER, stated once so nothing depends on notation: a compound
// move's name lists its operations in the order they are APPLIED, left to
// right. "PL" means apply P, then apply L to the result:
//
//     PL(C) = L(P(C)) = L(Cm) = Ab
//
// (Some texts write this as LP, function-composition style. This code never
// does.)

#pragma once
#include <stdint.h>

namespace tonnetz {

enum Quality : uint8_t { kMajor = 0, kMinor = 1 };

struct Chord
{
	uint8_t root    = 0;       // pitch class 0..11
	uint8_t quality = kMajor;

	Chord() = default;
	Chord(int r, int q) : root(static_cast<uint8_t>(((r % 12) + 12) % 12)), quality(static_cast<uint8_t>(q)) {}

	static Chord FromIndex(int i) { return Chord(i >> 1, i & 1); }
	int  Index()   const { return root * 2 + quality; }
	bool IsMinor() const { return quality == kMinor; }
	int  Third()   const { return (root + (quality == kMajor ? 4 : 3)) % 12; }
	int  Fifth()   const { return (root + 7) % 12; }

	/// The three pitch classes, root first.
	void Tones(int out[3]) const { out[0] = root; out[1] = Third(); out[2] = Fifth(); }

	/// 12-bit pitch-class set, bit pc set for each chord tone.
	uint16_t Mask() const
	{
		return static_cast<uint16_t>((1u << root) | (1u << Third()) | (1u << Fifth()));
	}
	bool Contains(int pc) const { return (Mask() >> (((pc % 12) + 12) % 12)) & 1u; }

	bool operator==(const Chord &o) const { return root == o.root && quality == o.quality; }
	bool operator!=(const Chord &o) const { return !(*this == o); }
};

constexpr int kNumChords = 24;

// ---------------------------------------------------------------------------
// Primary transformations. Each is an involution: X(X(c)) == c.
//
//   P (parallel)        same root, quality flipped.        C  <-> Cm
//   L (leading-tone)    major r -> minor r+4               C  <-> Em
//                       minor r -> major r+8   (the root moves down a semitone
//                                               in the major->minor direction)
//   R (relative)        major r -> minor r+9               C  <-> Am
//                       minor r -> major r+3   (the fifth moves up a tone)
// ---------------------------------------------------------------------------

enum Op : uint8_t { kP = 0, kL = 1, kR = 2 };

Chord ApplyOp(Chord c, Op op);

constexpr int kMaxMoveLen = 3;

struct Move
{
	const char *name;          // ops in application order, e.g. "RLP"
	uint8_t     len;
	Op          ops[kMaxMoveLen];
	uint8_t     novelty;       // 0 = direct or diatonic-fifth, 1 = two/three-step colour, 2 = remote
};

/// The move vocabulary the harmony engine draws candidates from. Index 0..2 are
/// P, L, R; the rest are compounds. See tonnetz.cpp for the full table and what
/// each does to C major.
extern const Move kMoves[];
extern const int  kNumMoves;
constexpr int     kMaxMoves = 16;

Chord ApplyMove(Chord c, const Move &m);
/// Index into kMoves of the single op, so history can compare single moves.
int MoveIndexOfOp(Op op);

/// Must be called once before Distance(). Builds the all-pairs distance table.
void TonnetzInit();

/// Fewest P/L/R operations between two chords (0..~6).
int Distance(Chord a, Chord b);

int CommonTones(Chord a, Chord b);

/// "C", "F#m", ... Writes at most 4 chars plus terminator.
const char *ChordName(Chord c, char *buf);

} // namespace tonnetz
