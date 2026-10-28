# TONNETZ

A generative harmonic synthesiser for the [Music Thing Modular Workshop System Computer](https://github.com/TomWhitwell/Workshop_Computer).

TONNETZ walks the neo-Riemannian Tonnetz, the network of the 24 major and minor triads linked by the P, L and R transformations. It plays the result as a slowly morphing three-voice chord on **Audio Out 1**. On **Audio Out 2** it plays a melody that knows where the harmony has been, where it is, and where it is about to go.

Progressions are not preset. Each next chord is chosen by scoring every reachable chord against several musical objectives:
- smooth voice leading;
- the inferred key;
- a phrase shape that departs and returns;
- avoiding trivial back-and-forth;
- an optional target note from CV.

It then samples from those scores. The next chord is planned the moment the current one starts, so the melody can lean towards it: notes that prepare the coming harmony become more likely as the chord change approaches.

It plays with nothing patched. Patch a clock to sync it, a reset to bring it home, and a keyboard or sequencer CV to steer the harmony towards notes you choose.

**Status:** v0.1, in development. It builds clean, and the music engine and synth pass 65 host tests (`tests/`). Reference renders come from `tools/render`. **It has not yet been flashed and played on hardware**; see [docs/BRINGUP.md](docs/BRINGUP.md) for the first-session checklist.

## Jacks

| Jack | Function |
|---|---|
| **Audio Out 1** | The chord: three voices, summed |
| **Audio Out 2** | The melody |
| **Pulse In 1** | Clock, one pulse per quarter note. Overrides the internal tempo while patched |
| **Pulse In 2** | Reset: back to the home chord, start a new phrase |
| **CV In 1** | 1 V/oct harmonic target (0 V = C). Pulls the *next* chord towards containing that note |
| **CV In 2** | Wander offset, added to the Main knob (±5 V covers the whole range) |
| **CV Out 1** | Current chord root, 1 V/oct (C3 = root C) |
| **CV Out 2** | Current melody note, 1 V/oct |
| **Pulse Out 1** | Trigger on every chord change |
| **Pulse Out 2** | Trigger on every melody note |
| Audio In 1/2 | Unused (reserved) |

The two audio outputs are independent mono signals, not a stereo pair. Running them through separate effects works well, or you can mix them yourself.

## Controls

The Z switch selects one of three pages. **Middle** and **Up** stay put. **Down** springs back, so its page is only active while you hold it.

| Page | Main | X | Y |
|---|---|---|---|
| **Middle: PERFORM** | Wander / gravity | Chord glide time | Melodic anticipation |
| **Up: TIMING** | Internal BPM (40–240) | Melody density | Phrase length: 2 / 4 / 8 / 16 chords |
| **Down (hold): ADVANCED** | CV In 1 pull (top 8% = *strict*) | Melody rhythm: quarters / eighths / sixteenths | Tonal inertia |

- **Wander.** Fully anticlockwise, the card stays close to its home key: diatonic chords, simple moves, and phrases that resolve. Turning clockwise raises the randomness. It allows compound and remote transformations (slides, hexatonic poles), weakens the pull of home, and lets phrases end somewhere new. Above about 60%, the card may adopt a new home key.
- **Glide.** Fully anticlockwise, chord changes are instant. The curve is cubic, so the short end, where "change" turns into "portamento", gets most of the travel: noon is 0.5 s and fully clockwise is 4 s. Long glides overlap chord changes. Voices then move through non-triadic intermediate harmony, and the chord becomes a slowly morphing drone.
- **Anticipation.** How strongly the planned next chord colours the melody as the change approaches. At zero the melody follows only the current harmony.
- **CV pull.** How strongly CV In 1's note attracts the next chord. Below 92% it is a pull; above, it is *strict*: the next chord must contain the note if any reachable chord does.
- **Inertia.** How reluctant the inferred tonal centre and scale are to change.
- **Pickup.** When you change page, each knob ignores its position until it reaches the value that page last had, so a page change never makes anything jump. While a knob is waiting, its LED pulses: LED 0 for Main, 1 for X, 2 for Y.
- **Down tap = reset.** Press and release Down within 0.4 s without turning a knob. This does the same as a trigger at Pulse In 2.

Defaults before a page is first visited:
- TIMING: 100 BPM, density 0.55, 8-chord phrases;
- ADVANCED: CV pull about 0.5, eighth-note rhythm, medium inertia.

## LEDs

```
 0 clock / beat          1 chord change
 2 melody envelope       3 tension (distance from home)
 4 next chord committed  5 page: off PERFORM, on TIMING, blinking ADVANCED
```

LED 0 glows dimly between beats while an external clock is locked. LED 4 is dim while the next chord is still open to change, and lights fully once the chord is committed.

## Clocking

With Pulse In 1 unpatched, the internal clock runs at the TIMING page's BPM. When a cable is patched, each rising edge is a quarter-note beat. The 16ths between edges follow the measured pulse period, and the chord changes land on the pulses.

Jack detection tells the card whether a cable is patched, so a *stopped* clock and an *unplugged* one are handled differently:
- **Unplugged:** the card returns to the internal tempo immediately.
- **Patched but stopped** (no pulse for 2 periods or 2 s): the music holds by default. The chord sustains, the melody releases, and it resumes on the next pulse. A compile-time option (`kClockFallback` in `src/dsp/dsp_config.h`) can freewheel at the last tempo or switch to the internal tempo instead.

Changing clock source never double-advances. A pulse arriving just after an internal beat is taken to *be* that beat.

## Reset

A trigger at Pulse In 2 or a Down tap does the following:
- returns to the home key (C major);
- clears the phrase position and the move history;
- re-seeds the tonal centre;
- plans a fresh next chord.

Oscillators are never reset, so the chord glides home rather than clicking. The restart lands on the beat that arrives with the reset, or on the next beat.

## Patch ideas

- **Standalone.** Audio Out 1 and Audio Out 2 into two mixer channels, with reverb on the chord. Leave Main near 9 o'clock and X around noon.
- **Morphing drone.** X fully clockwise, Main near noon, TIMING BPM low. The harmony becomes a continuously shifting cluster.
- **Steered.** A sequencer's pitch CV into CV In 1, with CV pull up. The card harmonises *towards* your notes rather than transposing to them. Hold Down and push Main past 92% to make it strict.
- **Accompanist.** Clock and reset from your sequencer. CV Out 1 drives a bass voice, and Pulse Out 1 triggers its envelope.

## Building

Uses the Raspberry Pi Pico SDK 2.2.0 toolchain installed by the Pico VS Code extension (`%USERPROFILE%\.pico-sdk`).

```powershell
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$env:PATH = "$env:USERPROFILE\.pico-sdk\cmake\v3.31.5\bin;$env:USERPROFILE\.pico-sdk\ninja\v1.12.1;$env:USERPROFILE\.pico-sdk\toolchain\14_2_Rel1\bin;$env:PATH"
cmake -S . -B build -G Ninja
cmake --build build
```

Each build copies `build/tonnetz.uf2` to `FLASHME/tonnetz.uf2`. Hold the card's boot button, plug it in over USB, and drag that file onto the drive that appears. A bring-up variant that replaces the CV outputs with CPU-load meters builds with `-DTONNETZ_LOAD_METER=ON` (into `build-meter/`, producing `FLASHME/tonnetz-loadmeter.uf2`); see [docs/BRINGUP.md](docs/BRINGUP.md).

The build uses 2.3% of flash and 20.9% of RAM, with all code copied to RAM at boot.

## Testing and listening on a computer

The music engine and the audio-ISR code are plain C++ with no hardware dependencies, so they build and run on a desktop. The host compiler is zig's bundled clang, installed once with `python -m pip install --user ziglang`.

```powershell
powershell -File tests/run_tests.ps1           # 65 tests; add a word to filter, e.g. "tonal"
powershell -File tools/render.ps1              # reference WAVs into tools/out/, with statistics
powershell -File tools/render.ps1 mine.wav 120 0.7 0.9 0.6   # one render: secs wander glide antic
```

The renders run the card's real ISR and composer code sample by sample. They are what the DAC would receive: left = Audio Out 1, right = Audio Out 2.

## How it works

[docs/HARMONY.md](docs/HARMONY.md) explains:
- the Tonnetz and its transformations;
- every score term and weight;
- the three harmonic memories (local melodic context, local scale, long-term tonal centre);
- lookahead and commitment;
- the melody's anticipation;
- the voice leading and glide.

All musical tunables are in one file, [src/engine/config.h](src/engine/config.h).

## Deferred from this version

- **Pattern capture and mutation** (brief §15, optional): recording a pleasing transformation sequence and replaying it with probabilistic changes.
- **Richer timbres.** Both voices use a fixed sine/triangle blend, with no waveform control, filter or detune yet. The ISR uses about a tenth of its budget (an estimate, to be measured), so there is room.
- **Depth-3 lookahead.** The search is depth 2 (every candidate, plus a soft value of where it can go next). A third level was left out to keep core 1's plan time well inside its deadline at 240 BPM.
- **Beats per chord on the panel.** It is a compile-time constant (`kBeatsPerChord = 4`).
- **Holding a chord for more than one chord-length.** Every chord change moves; "stay" is not a candidate.
- **CV In 1 octave.** Only the pitch class is used. Calibration of CV In 1 is two constants in `dsp_config.h`, with no on-card routine.
- **External clock resolution.** Fixed at one pulse per quarter note.
- **Melody dynamics.** Every note has the same peak level.
- **Audio In 1/2** are unused.
- **Random seed.** Fixed seeds for repeatable performances are compile-time only (`kDeterministicSeed`).

## License

CC BY 4.0. See [LICENSE](LICENSE).
