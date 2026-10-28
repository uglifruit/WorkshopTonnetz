# TONNETZ — first hardware session

Nothing below has been checked on a real card yet. Work through it in order. Each step says what you should hear or see, and what it means if you don't.

## 0. Flash

1. Build (see README) and drag `FLASHME/tonnetz.uf2` onto the card.
2. Power-cycle **cold** (off, then on), not just a reset. The crystal-delay setting exists because cards that work from a warm reset can fail a cold start.

## 1. Sound with nothing patched

Patch Audio Out 1 and Audio Out 2 to a mixer. Leave Main, X and Y at noon, and the switch in the middle.

| Check | Expect | If not |
|---|---|---|
| Audio Out 1 | A three-note chord, changing every 4 beats (~2.4 s at 100 BPM) | Silence: core 1 never queued plans (see step 6) |
| Audio Out 2 | A melody in the octave above middle C, with rests | |
| LED 0 | Flashes on each beat | |
| LED 1 | Flashes and fades at each chord change | |
| LED 2 | Follows the melody's notes | |
| LED 4 | Dim for most of each chord, then bright from ~70% to the change | |
| Clicks | None at chord changes or note starts | Note the setting that clicks |
| Levels | Both outputs comfortable (modelled peak ~1840/2047 on Out 1, ~1640 on Out 2) | |

## 2. The performance page (middle)

| Control | Try | Expect |
|---|---|---|
| Main (wander) | Fully CCW for a minute | Plainly tonal: C, Am, F, G, Em, occasional Cm/Fm colour, phrases returning to C |
| | Fully CW for a minute | Chromatic, remote chords (C♯m, A♭m …). Still voice-led, not random jumps. The home key may move |
| X (glide) | Fully CCW | Instant chord changes |
| | Noon | ~0.5 s portamento |
| | Fully CW | 4 s glides that overlap the changes: a morphing drone |
| Y (anticipation) | CCW vs CW | At CW, the melody starts using the *next* chord's notes in the last beat or so before each change |

## 3. Pages and pickup

1. Flick the switch **Up**. LED 5 lights, and LEDs 0–2 pulse: those knobs are waiting to be picked up.
2. Turn Main through its old position. Its LED stops pulsing and it now sets BPM (40–240). X sets melody density. Y sets phrase length.
3. Flick back to **Middle**. Nothing should jump. The knobs you moved pulse until they catch up again.
4. **Hold Down.** LED 5 blinks. Main sets CV pull, X the melody rhythm grid, Y the tonal inertia. Release it.
5. **Tap Down** quickly without touching a knob: a reset. The next beat starts a new phrase on C major.

## 4. Clock and reset

| Patch | Expect |
|---|---|
| A clock (quarter notes) into Pulse In 1 | LED 0 glows dimly between beats and flashes on each pulse. Chords change on the pulses, every 4 |
| Speed the clock up and down | Follows within a beat. No double chord changes |
| Stop the clock, leaving the cable in | Within ~2 s the melody releases and the chord holds. Restart: it picks up on the first pulse |
| Pull the cable | Returns to the internal BPM straight away |
| Trigger into Pulse In 2 | Back to C major on that beat (or the next), with a new phrase. The chord glides home with no click |

## 5. CV

| Patch | Expect |
|---|---|
| A keyboard or sequencer into CV In 1, CV pull at noon (hold Down, Main) | Upcoming chords tend to *contain* the held note. It is harmonised, not transposed |
| CV pull fully CW (strict) | Almost every chord contains the note |
| Change the note late in a chord (LED 4 bright) | The next chord does not change. The one after follows the new note |
| LFO into CV In 2 | Wander rises and falls with it |
| CV Out 1 into a tuned oscillator | Plays the chord roots, in tune (C3 for C) |
| CV Out 2 into a tuned oscillator | Doubles the melody, in tune |

**CV In 1 calibration.** The inputs are not factory-calibrated. If a keyboard's notes land a semitone off, or the error grows up the keyboard, trim `kCvOffsetCounts` (counts read at 0 V) and `kCvScaleQ12` (4096 = nominal) in [src/dsp/dsp_config.h](../src/dsp/dsp_config.h). Only the pitch class matters, so an octave error is harmless.

## 6. CPU headroom (load-meter build)

Build with `cmake -S . -B build-meter -G Ninja -DTONNETZ_LOAD_METER=ON` and `cmake --build build-meter`, then flash `FLASHME/tonnetz-loadmeter.uf2`. The CV outputs become meters; measure them with a multimeter or scope.

| Output | Reads | Healthy | Estimate |
|---|---|---|---|
| CV Out 1 | Peak audio-ISR time per sample, 0–6 V = 0–100% of the 20.8 µs budget | below ~3 V (50%) | ~10% |
| CV Out 2 | Core 1's slowest tick plan since boot, 0–6 V = 0–125 ms (the deadline at 240 BPM) | below ~1.5 V (~30 ms) | ≲10 ms |

Run it at Main fully CW, BPM at maximum, with a CV into CV In 1 changing every beat: that is the worst case for core 1.

**If CV Out 2 is high:** lower `beamWidth` in [src/engine/config.h](../src/engine/config.h) (6 → 3 roughly halves the plan time), or raise `kPlanLookaheadTicks` (this makes CV and knob response correspondingly later). Late plans are not catastrophic: the card drops the late note attacks and keeps the harmony, which is what `tests/test_card.cpp: card_survives_a_slow_composer` checks. You would hear it as a sparser melody at very high tempo.

**Flash the normal UF2 again afterwards.**

## 7. Report back

For anything that sounds wrong, describe the *sound* ("a click when…", "the melody stops after…") and the knob positions. The sibling cards' notes show that the description of the sound is usually what finds the bug.
