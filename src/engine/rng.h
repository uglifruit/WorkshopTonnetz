// rng.h — the engine's one source of randomness.
//
// Every stochastic decision draws from a single Rng, so a fixed seed replays
// the card exactly (kDeterministicSeed, and always on the host).

#pragma once
#include <stdint.h>

namespace tonnetz {

class Rng
{
public:
	void Seed(uint32_t s) { s_ = s ? s : 0x9E3779B9u; }

	/// Marsaglia xorshift32.
	uint32_t Next()
	{
		s_ ^= s_ << 13;
		s_ ^= s_ >> 17;
		s_ ^= s_ << 5;
		return s_;
	}

	/// Uniform in [0, 1).
	float Uniform() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }

	bool Chance(float p) { return Uniform() < p; }

	/// Index drawn from non-negative weights. Returns n-1 if they sum to zero
	/// or rounding leaves the draw past the end.
	int Pick(const float *w, int n)
	{
		float total = 0.0f;
		for (int i = 0; i < n; i++) total += w[i];
		if (total <= 0.0f) return n - 1;
		float r = Uniform() * total;
		for (int i = 0; i < n; i++)
		{
			r -= w[i];
			if (r < 0.0f) return i;
		}
		return n - 1;
	}

private:
	uint32_t s_ = 0x9E3779B9u;
};

} // namespace tonnetz
