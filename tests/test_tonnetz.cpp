// test_tonnetz.cpp — the 24 triads, P/L/R, compounds and distance.

#include "test.h"
#include "engine/tonnetz.h"

using namespace tonnetz;

TEST(tonnetz_all_24_triads_distinct)
{
	bool seen[kNumChords] = {};
	for (int i = 0; i < kNumChords; i++)
	{
		const Chord c = Chord::FromIndex(i);
		CHECK(c.Index() == i);
		CHECK(c.root < 12);
		seen[c.Index()] = true;
		// Every triad is three distinct pitch classes.
		int n = 0;
		for (int pc = 0; pc < 12; pc++) n += c.Contains(pc);
		CHECK(n == 3);
	}
	for (bool s : seen) CHECK(s);
}

TEST(tonnetz_known_examples)
{
	const Chord C(0, kMajor);
	CHECK(ApplyOp(C, kP) == Chord(0, kMinor));   // P(C) = Cm
	CHECK(ApplyOp(C, kL) == Chord(4, kMinor));   // L(C) = Em
	CHECK(ApplyOp(C, kR) == Chord(9, kMinor));   // R(C) = Am
	const Chord Am(9, kMinor);
	CHECK(ApplyOp(Am, kR) == C);
	CHECK(ApplyOp(Am, kL) == Chord(5, kMajor));  // L(Am) = F
	CHECK(ApplyOp(Am, kP) == Chord(9, kMajor));
}

TEST(tonnetz_involutions_and_shared_tones)
{
	for (int i = 0; i < kNumChords; i++)
	{
		const Chord c = Chord::FromIndex(i);
		for (int op = 0; op < 3; op++)
		{
			const Chord d = ApplyOp(c, static_cast<Op>(op));
			CHECK(d.quality != c.quality);                       // always flips mode
			CHECK(ApplyOp(d, static_cast<Op>(op)) == c);         // X(X(c)) == c
			CHECK(CommonTones(c, d) == 2);                       // parsimonious: two tones held
		}
		// The moving voice: P and L move one tone by a semitone, R by a tone.
		auto moved = [&](Op op) {
			const Chord d = ApplyOp(c, op);
			const uint16_t gone = static_cast<uint16_t>(c.Mask() & ~d.Mask());
			const uint16_t came = static_cast<uint16_t>(d.Mask() & ~c.Mask());
			int a = 0, b = 0;
			while (!((gone >> a) & 1u)) a++;
			while (!((came >> b) & 1u)) b++;
			int dist = ((b - a) % 12 + 12) % 12;
			return dist > 6 ? 12 - dist : dist;
		};
		CHECK(moved(kP) == 1);
		CHECK(moved(kL) == 1);
		CHECK(moved(kR) == 2);
	}
}

TEST(tonnetz_compounds_apply_left_to_right)
{
	const Chord C(0, kMajor);
	auto find = [](const char *name) -> const Move & {
		for (int m = 0; m < kNumMoves; m++)
		{
			const char *a = kMoves[m].name, *b = name;
			while (*a && *a == *b) { a++; b++; }
			if (!*a && !*b) return kMoves[m];
		}
		return kMoves[0];
	};
	CHECK(ApplyMove(C, find("PL")) == Chord(8, kMajor));   // C -> Cm -> Ab
	CHECK(ApplyMove(C, find("LP")) == Chord(4, kMajor));   // C -> Em -> E
	CHECK(ApplyMove(C, find("PR")) == Chord(3, kMajor));   // C -> Cm -> Eb
	CHECK(ApplyMove(C, find("RP")) == Chord(9, kMajor));   // C -> Am -> A
	CHECK(ApplyMove(C, find("LR")) == Chord(7, kMajor));   // C -> Em -> G
	CHECK(ApplyMove(C, find("RL")) == Chord(5, kMajor));   // C -> Am -> F
	CHECK(ApplyMove(C, find("RLR")) == Chord(2, kMinor));  // C -> Dm
	CHECK(ApplyMove(C, find("RLP")) == Chord(5, kMinor));  // N: C -> Fm
	CHECK(ApplyMove(C, find("LPR")) == Chord(1, kMinor));  // S: C -> C#m
	CHECK(ApplyMove(C, find("LPL")) == Chord(8, kMinor));  // H: C -> Abm

	// Every move from every chord lands on a valid triad, and the move's name
	// matches its op list.
	for (int m = 0; m < kNumMoves; m++)
	{
		int len = 0;
		while (kMoves[m].name[len]) len++;
		CHECK(len == kMoves[m].len);
		for (int i = 0; i < kNumChords; i++)
		{
			const Chord d = ApplyMove(Chord::FromIndex(i), kMoves[m]);
			CHECK(d.root < 12 && d.quality <= 1);
		}
	}
}

TEST(tonnetz_distance)
{
	TonnetzInit();
	const Chord C(0, kMajor);
	CHECK(Distance(C, C) == 0);
	CHECK(Distance(C, Chord(9, kMinor)) == 1);
	CHECK(Distance(C, Chord(7, kMajor)) == 2);
	int maxD = 0;
	for (int a = 0; a < kNumChords; a++)
		for (int b = 0; b < kNumChords; b++)
		{
			const int d = Distance(Chord::FromIndex(a), Chord::FromIndex(b));
			CHECK(d == Distance(Chord::FromIndex(b), Chord::FromIndex(a)));   // symmetric
			CHECK(d < 0xFF);                                                    // connected
			if (d > maxD) maxD = d;
		}
	NOTE("Tonnetz diameter (P/L/R steps): %d", maxD);
}
