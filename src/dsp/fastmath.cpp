// fastmath.cpp — the generated tables. Built once at boot in double precision
// (slow on the RP2040, but only once, before audio starts).

#include "fastmath.h"
#include <math.h>

namespace tonnetz {

int16_t  __not_in_flash("tonnetz_tables") gSinTable[258];
uint32_t __not_in_flash("tonnetz_tables") gNoteInc[129];

void DspInit()
{
	const double halfPi = 1.5707963267948966;
	for (int i = 0; i <= 256; i++)
		gSinTable[i] = static_cast<int16_t>(lround(32767.0 * sin(halfPi * static_cast<double>(i) / 256.0)));
	gSinTable[257] = gSinTable[256];   // padding, see fastmath.h

	// inc = f * 2^32 / 48000, f = 440 * 2^((n - 69) / 12)
	for (int n = 0; n <= 128; n++)
	{
		const double f = 440.0 * pow(2.0, (static_cast<double>(n) - 69.0) / 12.0);
		gNoteInc[n] = static_cast<uint32_t>(llround(f * 4294967296.0 / 48000.0));
	}
}

} // namespace tonnetz
