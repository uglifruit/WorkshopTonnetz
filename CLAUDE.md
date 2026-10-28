# TONNETZ — working notes for Claude Code

A program card for the **Music Thing Modular Workshop System Computer** (RP2040), built on the header-only **ComputerCard** library (0.3.0, copied from `../Workshop2D2`). It is a sibling of `../WorkshopShepard`, `../WorkshopOcarina`, `../Workshop2D2`, `../WorkshopTract8` and `../WorkshopBio`, and reuses their hard-won rules (below).

TONNETZ is a generative harmonic synthesiser: neo-Riemannian Tonnetz navigation, scored and sampled, with three harmonic memories, a planned and committed next chord, a voice-led gliding chord on Audio Out 1, and an anticipating melody on Audio Out 2. [docs/HARMONY.md](docs/HARMONY.md) is the musical specification; read it before changing the engine.

## Status: v0.1.0, NOT YET ON HARDWARE

- Builds clean: 2.3% flash, 20.9% RAM (`copy_to_ram`).
- 65 host tests pass. The reference renders in `tools/out/` (gitignored) are the only listening done so far.
- [docs/BRINGUP.md](docs/BRINGUP.md) is the first-session checklist.
- `UF2/` is empty until a release. `panels/` is empty.
- Repository: https://github.com/uglifruit/WorkshopTonnetz (public, `main`).

**Do not "fix" anything on the strength of a host measurement alone** once hardware results exist. The sibling cards record many cases where a number improved and the sound got worse.

## Architecture: who runs what

```
src/main.cpp          wiring only: ComputerCard <-> HwIn/HwOut, core 1 launch, 192 MHz
src/dsp/  (core 0, the 48 kHz ISR, fixed-point only)
  cardcore.*          transport, plan application, panel, LEDs, outputs  <- the ISR's whole job
  clock.*             internal tempo / Pulse In 1 sync / lost-clock state machine
  controls.*          pages, debounce, pickup, Down tap
  cvin.h              1 V/oct quantiser with hysteresis, CV 2 smoothing
  synth.*             3 chord voices + melody, linear semitone glide, envelope
  fastmath.*          sine (258 entries), note increment table, built at boot
src/composer.*        (core 1) resets, params, plans kPlanLookaheadTicks ahead
src/engine/ (core 1, float allowed, no hardware headers)
  conductor.*         THE entry point: PlanTick() -> TickPlan
  harmony.*           candidates, scoring, lookahead, softmax, plan/commit, phrase, migration
  tonal.*             Window B (scale) and Window C (tonal centre)
  melody.*            Window A, anticipation, rhythm
  voicing.*           integer exhaustive voice leading
  tonnetz.*           P/L/R, the move table, distance
  mapping.h           EVERY knob/CV -> parameter curve
  config.h            EVERY musical tunable
src/link.h            the only shared memory between the cores
```

**Core 1 plans, the ISR applies.** Do not move musical decisions into the ISR, and do not let core 1 touch ComputerCard (its members are not volatile). Every cross-core publish is "write data, `tz_dmb()`, bump index/sequence".

**Tests and the render tool drive the real code.** `tests/sim.h` runs `CardCore::Process` + `Composer::Step` per sample with simulated jacks. If you add a feature, test it from the jacks (`test_card.cpp`) as well as in isolation. Shepard's lesson: "a test that begins downstream of the bug cannot see it."

## Rules carried over from the sibling cards: do not undo

1. **Fixed-point only in the ISR.** `-Wdouble-promotion -Wfloat-conversion` are on. Core 1 uses `float` on purpose; a `double` there is still a bug.
2. **Audit the ISR call graph after any DSP change:**
   ```
   arm-none-eabi-objdump -d build/tonnetz.elf > build/tonnetz.dis
   python -I tools/isr_calls.py build/tonnetz.dis --list
   ```
   It must report no unexpected flash functions and no float/double/64-bit helpers. It follows long-branch veneers. The first version did not, and missed `ClockEngine::Init` being in flash.
3. **`__not_in_flash_func` on every ISR function.** `copy_to_ram` makes this belt-and-braces, but it keeps the guarantee if that is ever taken off. Integer divides go through the RAM divider (`PICO_DIVIDER_IN_RAM=1`).
4. **Tables are plain globals in `.cpp`** (`gSinTable`, `gNoteInc`), built in `DspInit()` at boot.
5. **The sine table has 258 entries**: the last is padding for `fast_sin`'s exact-boundary read (Ocarina).
6. **`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`** in CMake *and* as a `#define` (in `platform.h`) for the registry validator.
7. **No hardware access in constructors**: setup is in `main()`.
8. **`CVOutMIDINote` only on a change** (Ocarina).
9. **Switch:** Down is momentary. Page changes between Middle and Up are deferred (`kPageSettleTicks`), and the switch is debounced (`kSwitchSettleTicks`).
10. **Pickup with LED feedback** (Shepard finding 10). PERFORM is live from boot.
11. **Down tap is decided on RELEASE.** No timed gesture is layered over a held position (Ocarina).
12. **FLASHME is refreshed by every build.** The load-meter variant writes `tonnetz-loadmeter.uf2`, never `tonnetz.uf2`.

## Findings during development (host, before hardware)

1. **Glide landing jump.** The Q16 linear ramp truncates `(target − pitch) / blocks`, so the last block jumped by up to `blocks` LSB, ~9 cents on a 2 s glide. The ramp now runs in Q24. `synth_retarget_mid_glide_is_continuous` measures the per-block step.
2. **Late plans hid inside their own tick.** "Late" first meant "for a tick already passed", so a plan arriving 400 ms after its tick fired counted as on time. Late is now also "more than 30 ms after its tick" (`kLatePlan`).
3. **A dropped attack must become a rest.** When a late note-on was simply skipped, the previous note rang on until the next event, up to 4 s. The composer believes the old note ended there, so the ISR now releases it. `card_survives_a_slow_composer` covers both findings.
4. **Core 1's budget.** The first engine ran float voicing inside the depth-2 lookahead, about 117 `VoiceLead` calls per plan, estimated at 100–200 ms on the M0+ against a 125 ms deadline at 240 BPM. Fixes: integer voicing, evidence computed once per scale update rather than per scale, a precomputed melody register curve, beam 8 → 6, and `copy_to_ram`. The estimate is now ≲10 ms. **This is unmeasured**: use the load-meter build (`TONNETZ_LOAD_METER`).
5. **Anticipation was diluted.** Approach weight on both neighbours of *every* next-chord tone, chromatic included, spread the future mass so thinly that it grew only 0.13 → 0.17 across a chord. Approach notes now go only to tones that move, with whole-tone and chromatic approaches weakened, and next weight max 1.2. The growth is now 0.15 → 0.26. The test asserts *growth*: F and A are diatonic to C, so their baseline mass is never near zero, and asserting that it was would have been a wrong test.
6. **Conservative was too chromatic and its tonic twitchy.** At wander 0.05, P moves (cheapest voice leading) won too often (C Cm C G Gm Eb …). The inferred tonic also changed 20 times in 76 chords. Gravity at the conservative end went 2.2 → 3.0, home's share became wander-dependent (0.65 → 0.35), and tonal decay and switching margin went up. Result: 81% diatonic, zero tonic changes at wander 0.05.
7. **Home migration never fired.** Phrase-end resolution kept pulling the inferred tonic back to home, so "inferred tonic ≠ home" was never true. Migration now falls back to the chord the phrase came to rest on.
8. **A render-tool bug that looked like a melody bug.** The melody ran at 0.7 notes per chord because the tool returned to PERFORM after 400 samples, but the page change settles at 576. Its sweep of perform-X therefore landed on *density*. Wiring and timing bugs look like musical bugs; check the control path first.

## Building and testing

The firmware environment is in the README. The host tests use zig's clang (`python -m pip install --user ziglang`). **MSVC is installed here but the Windows SDK is not**, so `cl` cannot link; that is why zig is used.

```
powershell -File tests/run_tests.ps1 [filter]
powershell -File tools/render.ps1 [out.wav secs wander glide antic density seed]
```

`tests/host_build.ps1` compiles `src/` with the firmware's float warnings, and `tests/` without them.

## Registry notes (for release)

`info.yaml` follows Workshop2D2's draft form. It has **not** been run through the registry validator, and Shepard's notes show the upstream schema differs from that form:
- `short-description` is required;
- `controls.knobs[].when` is an object;
- LED ids are strings.

Before submitting, run:

```
cd ../Workshop_Computer/tools/sitegen && npm install
npm run validate-info -- <path>/info.yaml
```
