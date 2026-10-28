// synth.h — four oscillators: three chord voices (Audio Out 1) and a melody (Out 2).
//
// CHORD GLIDE is a time-based LINEAR ramp in semitone (log-frequency) space:
// on a new chord each voice's step is (target - CURRENT pitch) / blocks, so it
// arrives exactly when the glide time says, whatever it was doing before. A
// chord that arrives mid-glide starts its ramp from where the voice actually
// IS, never from the old target, so long glides across several chord changes
// stay continuous — the texture morphs rather than jumping. Oscillator phases
// are never reset.
//
// The ramp advances once per control block (16 samples); the per-sample loop
// only accumulates phase.

#pragma once
#include <stdint.h>
#include "dsp_config.h"
#include "fastmath.h"

namespace tonnetz {

struct GlideVoice
{
	// The ramp runs in Q24 so its integer-division residue is negligible: in
	// Q16 a 2 s glide could land with a 9-cent jump on its last block. 96 << 24
	// still fits an int32.
	int32_t  pitch = 60 << 16;    // Q16 semitones, where the voice IS
	int32_t  pitchQ24 = 60 << 24;
	int32_t  targetQ24 = 60 << 24;
	int32_t  step = 0;            // Q24 per control block
	int32_t  blocksLeft = 0;
	uint32_t phase = 0;
	uint32_t inc = 0;

	void Retarget(int32_t newTarget, int32_t blocks);
	void Control();
};

class Synth
{
public:
	void Init();

	/// New chord: three MIDI notes (voice 0 lowest) reached over glideSamples.
	void SetChord(const uint8_t notes[3], uint32_t glideSamples);
	void MelodyOn(uint8_t note, bool legato);
	void MelodyOff();

	/// Once per control block: advance ramps, refresh increments.
	void Control();

	/// Once per sample. Q15 >> 4 into the DAC's 12-bit range.
	int16_t RenderChord();
	int16_t RenderMelody();

	int32_t ChordPitch(int v) const { return chord_[v].pitch; }
	int32_t MelodyPitch() const { return mel_.pitch; }
	int32_t MelodyEnvelope() const { return env_; }   // Q30
	bool    MelodyGate() const { return gate_; }

private:
	enum EnvStage : uint8_t { kIdle, kAttack, kDecay, kRelease };

	GlideVoice chord_[3];
	GlideVoice mel_;
	int32_t env_ = 0;
	EnvStage stage_ = kIdle;
	bool gate_ = false;
};

} // namespace tonnetz
