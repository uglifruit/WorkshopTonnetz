// synth.cpp — oscillators, glide and envelope. Runs in the audio ISR.

#include "synth.h"

namespace tonnetz {

void __not_in_flash_func(GlideVoice::Retarget)(int32_t newTarget, int32_t blocks)
{
	if (newTarget < (kPitchMin << 16)) newTarget = kPitchMin << 16;
	if (newTarget > (kPitchMax << 16)) newTarget = kPitchMax << 16;
	targetQ24 = newTarget << 8;
	if (blocks <= 0)
	{
		pitchQ24 = targetQ24;
		pitch = newTarget;
		blocksLeft = 0;
		step = 0;
		return;
	}
	// From the CURRENT pitch, not the old target. 32-bit divide: the RP2040's
	// hardware divider (PICO_DIVIDER_IN_RAM), once per chord change.
	step = (targetQ24 - pitchQ24) / blocks;
	blocksLeft = blocks;
}

void __not_in_flash_func(GlideVoice::Control)()
{
	if (blocksLeft > 0)
	{
		pitchQ24 += step;
		if (--blocksLeft == 0) pitchQ24 = targetQ24;   // residue < blocks / 2^24 semitone
		pitch = pitchQ24 >> 8;
	}
	inc = PitchToInc(pitch);
}

void Synth::Init()
{
	static const int kStart[3] = { 55, 60, 64 };
	for (int v = 0; v < 3; v++)
	{
		chord_[v].Retarget(kStart[v] << 16, 0);
		// Spread the starting phases so the three voices never begin in step.
		chord_[v].phase = static_cast<uint32_t>(v) * 0x55555555u;
		chord_[v].Control();
	}
	mel_.Retarget(72 << 16, 0);
	mel_.Control();
	env_ = 0;
	stage_ = kIdle;
	gate_ = false;
}

void __not_in_flash_func(Synth::SetChord)(const uint8_t notes[3], uint32_t glideSamples)
{
	const int32_t blocks = static_cast<int32_t>(glideSamples / kControlBlock);
	for (int v = 0; v < 3; v++) chord_[v].Retarget(static_cast<int32_t>(notes[v]) << 16, blocks);
}

void __not_in_flash_func(Synth::MelodyOn)(uint8_t note, bool legato)
{
	const int32_t p = static_cast<int32_t>(note) << 16;
	if (legato && stage_ != kIdle && gate_)
	{
		// Slur: glide to the new note, envelope untouched.
		mel_.Retarget(p, kLegatoGlideBlocks);
		return;
	}
	// A new attack starts from wherever the envelope is (never from zero
	// while sounding), so retriggering cannot click. If the voice is silent
	// the phase is parked at zero so the attack starts on a zero crossing.
	if (stage_ == kIdle) mel_.phase = 0;
	mel_.Retarget(p, 0);
	mel_.inc = PitchToInc(p);
	stage_ = kAttack;
	gate_ = true;
}

void __not_in_flash_func(Synth::MelodyOff)()
{
	gate_ = false;
	if (stage_ != kIdle) stage_ = kRelease;
}

void __not_in_flash_func(Synth::Control)()
{
	for (int v = 0; v < 3; v++) chord_[v].Control();
	mel_.Control();
}

int16_t __not_in_flash_func(Synth::RenderChord)()
{
	int32_t sum = 0;
	for (int v = 0; v < 3; v++)
	{
		chord_[v].phase += chord_[v].inc;
		sum += wave(chord_[v].phase, kChordTriMixQ15);
	}
	// |sum| <= 3 * 32767; * 9830 stays inside int32.
	return clamp12(((sum * kChordGainQ15) >> 15) >> 4);
}

int16_t __not_in_flash_func(Synth::RenderMelody)()
{
	switch (stage_)
	{
	case kIdle:
		return 0;
	case kAttack:
		env_ += kEnvOne / kAttackSamples;
		if (env_ >= kEnvOne) { env_ = kEnvOne; stage_ = kDecay; }
		break;
	case kDecay:
		env_ += (kSustainLevel - env_) >> kDecayShift;
		break;
	case kRelease:
		env_ -= env_ >> kReleaseShift;
		if (env_ < kEnvFloor) { env_ = 0; stage_ = kIdle; }
		break;
	}
	mel_.phase += mel_.inc;
	const int32_t w = wave(mel_.phase, kMelodyTriMixQ15);       // Q15
	const int32_t s = (w * (env_ >> 15)) >> 15;                // Q15 x Q15 -> Q15
	return clamp12(((s * kMelodyGainQ15) >> 15) >> 4);
}

} // namespace tonnetz
