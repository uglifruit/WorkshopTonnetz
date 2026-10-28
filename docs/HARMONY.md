# How TONNETZ thinks

This document explains the music engine: what it represents, how it chooses, and what each weight pulls against. Every number quoted is a default from [src/engine/config.h](../src/engine/config.h), where it can be changed without touching anything else. A pair like **1.2 → 0.6** means the value slides from the first to the second as **wander** (Main, plus CV In 2) goes from 0 to 1.

## 1. Two cores, one plan

```
 core 0 — audio ISR, 48 kHz, fixed-point           core 1 — composer, free-running, float
 ┌──────────────────────────────────────┐          ┌──────────────────────────────────┐
 │ clock (internal / Pulse In 1)        │ firedTick│ Conductor.PlanTick()             │
 │   -> tick every 16th note            │─────────▶│   HarmonyEngine  (chords)        │
 │ apply the TickPlan for that tick     │  params  │   TonalEstimator (windows B, C)  │
 │ panel, pickup, CV quantiser          │─────────▶│   MelodyEngine   (window A)      │
 │ 3 chord voices + melody voice        │◀─────────│ stays 2 ticks ahead              │
 └──────────────────────────────────────┘ TickPlans└──────────────────────────────────┘
```

The ISR does no musical thinking. It counts ticks and applies a pre-computed **TickPlan** the moment its tick fires. A plan holds a chord change (three voice targets plus the glide time), a melody action (on, legato, off) and LED state. Core 1 computes plans two ticks ahead, so timing is sample-accurate while all the scoring happens elsewhere.

Musical position is counted in ticks. The chord phase is `tickInChord / 16` (4 beats of 4 sixteenths). It does not depend on tempo or clock source, so a given seed produces the same music on the host simulator and on the card.

If core 1 ever falls behind, a plan that arrives late (for a tick already passed, or more than 30 ms after its tick) keeps its harmony but drops its note attack, which becomes a rest. Nothing hangs, and the chord still moves.

## 2. The Tonnetz

A chord is `(root pitch class, major|minor)`, with C = 0. That gives 24 chords, indexed `root*2 + quality`.

| | major root r → | minor root r → | C major → |
|---|---|---|---|
| **P** parallel | minor r | major r | C minor |
| **L** leading-tone exchange | minor r+4 | major r+8 | E minor |
| **R** relative | minor r+9 | major r+3 | A minor |

Each is an involution (`X(X(c)) = c`), and each keeps two common tones. P and L move the third tone by a semitone, R by a whole tone. All of this is checked for every chord in `tests/test_tonnetz.cpp`.

**Transformation order is left to right.** `PL` means *apply P, then L*: PL(C) = L(Cm) = A♭. No function-composition notation is used anywhere.

The move vocabulary (`kMoves` in [tonnetz.cpp](../src/engine/tonnetz.cpp)) is the three primaries plus these compounds:

| move | C → | novelty | character |
|---|---|---|---|
| LR, RL | G, F | 0 | diatonic fifths |
| RLR | Dm | 1 | diatonic ii |
| PL, LP | A♭, E | 1 | major-third relations (hexatonic) |
| PR, RP | E♭, A | 1 | minor-third relations (octatonic) |
| RLP ("N") | Fm | 1 | borrowed minor iv |
| LPR ("S", slide) | C♯m | 2 | remote: shares only the third |
| LPL ("H", hexatonic pole) | A♭m | 2 | maximally remote within the hexatonic cycle |

Rank-1 moves only become available above wander 0.20, and rank 2 above 0.55. When two routes reach the same chord, the shortest is kept as that candidate's label, so the move history records the most direct relationship. Graph distance (`Distance()`) is an all-pairs search over single P/L/R steps; the Tonnetz diameter is 5.

## 3. Choosing the next chord

Every reachable chord is a **candidate**: 5 at the conservative end (P, L, R and the two fifths), up to 13 above wander 0.55. Each gets a score, a sum of terms in log-probability-like units, where positive means more likely:

| term | what it measures | weight |
|---|---|---|
| **voice leading** | total semitone movement to the best voicing, plus voicing penalties (§7) | −cost × 0.25 × **1.2 → 0.6** |
| **gravity** | how the chord functions (I = 1, V = .85, IV = .8, vi = .6, borrowed iv = .35 …) in the **inferred** key distribution *and* in the explicit **home** key | **3.0 → 0.45**; home's share **0.65 → 0.35** |
| **phrase** | distance from home vs the phrase contour (below); resolution bonus at the phrase's last chord; dominant bonus on the penultimate | **1.3 → 0.55** |
| **CV membership** | the chord *contains* CV In 1's pitch class | 3.0 × pull |
| **CV tonic** | the chord is *rooted on* it. This is a separate concept: attraction is not transposition | 1.0 × pull |
| **novelty** | the move's novelty rank | rank × **−0.9 → +0.35** |
| **repetition** | the chord appeared 2–16 chords ago (decaying 0.8 per chord) | −0.9 per (decayed) occurrence |
| **reversal** | going straight back to the previous chord: the trivial P-P / L-L / R-R oscillation | −2.2 (×0.2 in a "motif" phrase) |
| **cycle** | continuing this phrase's chosen alternation: R/L (diatonic corridor, fifths cycle), P/L (hexatonic), P/R (octatonic) | +0.8 |

**Gravity** uses the key *distribution*, not just the top key. When the tonal estimate is uncertain its pull is spread across the plausible keys, so ambiguity softens gravity rather than forcing a wrong key.

**Phrase contour.** The target distance from home rises from 0 at the phrase start to a peak at 60% of the phrase (±12%, redrawn each phrase), then falls back to 0 at the last chord. The peak distance is **1.6 → 4.0** Tonnetz steps (±0.6 per phrase). For 8-chord phrases at wander 0.3 that is roughly *near, near, departing, departing, distant, turning, returning, home*. At the last chord, landing exactly on home earns +2.5, and a strong tonic-function substitute earns a quarter of that. Both are scaled by (1 − 0.6 × wander), so at high wander resolution is optional.

**Per-phrase variety.** Each phrase draws a fresh peak position and distance, a corridor (free, R/L, P/L or P/R, with more exotic corridors at higher wander), and with 15% probability a "motif" flag that lets an A-B-A-B gesture through. No two phrases are shaped identically.

**Home migration.** At a phrase boundary, above wander 0.6 and with probability (wander − 0.6) × 1.2, home moves:
- to the inferred tonic, if that is confident (p ≥ 0.3) and different from home;
- otherwise to the chord the phrase came to rest on, if it is at least 2 steps away.

### Lookahead

For the 6 highest-scoring candidates, the engine looks one chord further. It scores every continuation and takes the temperature-weighted soft maximum `V = max + T·log(mean(exp((s − max)/T)))`, with CV counting half at this depth. This adds 0.5 × V to the candidate's value. Candidates outside the beam get the worst V seen, so an unexplored candidate is never flattered. A destination with only poor continuations, such as a dead end far from home late in a phrase, loses value before it is chosen.

### Selection

The engine samples rather than taking the best candidate:

```
p_i = exp((v_i − v_max) / T)  /  Σ_j exp((v_j − v_max) / T)
```

Subtracting `v_max` keeps every exponent ≤ 0, so it cannot overflow. The temperature **T = 0.35 → 1.6** is what makes the same rules conservative or adventurous.

## 4. Plan, commitment and CV

The next chord is planned the moment the current chord starts. Until **78%** of the chord (`commitPhase`), a change at CV In 1 can replan it:
- a new pitch class;
- the jack being patched or unpatched;
- strict mode toggling;
- a change of more than 0.1 in pull.

**Hysteresis:** on a replan, the existing plan survives if its new probability is at least half the best one's, so a wobbling CV does not keep moving the destination the melody is leaning towards. After the commit point the plan is frozen, and a CV change affects the chord after next. LED 4 lights at commitment.

In **real** time the commit point is two 16ths earlier (≈69% of the chord), because plans are made two ticks ahead.

The CV quantiser in the ISR adds two more layers. A reading must pass the half-way point between semitones by a quarter semitone, then hold for 8 ms. 1 V/oct is exact in integer arithmetic: 341.33 ADC counts per volt makes `semitone × 256 = counts × 9`.

**Strict** mode (top 8% of the pull knob) filters the candidates to chords containing the note. If no usual move reaches one, it widens to every move. If still none, it keeps the usual moves with a bonus for chords from which the note *is* reachable next time. In tests, strict mode puts the requested note in 100% of chords, and ordinary full pull in about 80%.

## 5. The three harmonic memories

### Window A: local melodic context (the melody's view)

Previous, current and *planned next* chord, weighted per pitch class. See §6.

### Window B: local scale

The last 5 chords (weights 1, 0.7, 0.49 …) plus the planned next chord (0.55) give a pitch-class evidence vector. It is scored against 7 collections × 12 roots:
- Ionian, Aeolian, Dorian, Mixolydian;
- harmonic minor, harmonic major;
- melodic minor (ascending: 1 2 ♭3 4 5 6 7).

```
fit = (evidence inside − 1.5 × evidence outside) / total evidence
```

Small priors break ties between collections that explain the notes equally. Harmonic minor sits ahead of melodic minor, so Am → E reads as *A harmonic minor*. Among modes with the same notes, the one rooted on the tonal centre wins. A new scale must beat the current one by **0.03 → 0.14** (by inertia). If the best fit is below 0.72, the estimator says **chromatic**: no seven-note collection explains the harmony. The melody then relies on chord tones and chromatic approach notes instead of forcing a misleading scale.

### Window C: tonal centre

This is a leaky accumulator over 24 tonic hypotheses. Each arriving chord adds its function weight in every key (the same table gravity uses), scaled by phrase position: 1.3 at a phrase start, 1.5 at a phrase end. Cadences add more: V→I +0.8, any falling fifth onto the tonic triad +0.4, IV/iv→I +0.3. Evidence decays by **0.80 → 0.95** per chord (by inertia), roughly a 5- to 20-chord memory. A softmax over the normalised evidence (β = 8) gives the key distribution. The *reported* tonic only changes when a challenger beats it by a factor of **1.25 → 1.8**.

The point of the separation:
- **C → Em** stays ambiguous: the top key probability is 0.34, and the C and G major collections tie.
- **C → Em → F** settles on the C major collection.
- **C → Fm → G → C** keeps C as tonic (the Fm reads as borrowed; the local scale briefly becomes C harmonic major).
- A tritone-remote F♯ in a C progression sets the chromatic flag but leaves the tonic at C.
- Six B♭ cadences do move it.

All of these are asserted in `tests/test_tonal.cpp`.

## 6. The melody

Twelve pitch-class weights are rebuilt for each note:

```
w[pc] =  scale     0.45 × confidence                         (Window B; token 0.03 each if chromatic)
       + current   1.0 × (1 − 0.3 × anticipation × t)        (eases a little late in the chord)
       + previous  0.35  (shared with current) / 0.175 (not)
       + next      nextWeight            if also a current tone (safe now and later)
                   nextWeight × 0.25     if future-only, on a strong tick
                   nextWeight            if future-only, on a weak tick
       + approach  0.7 × anticipation × t   a semitone (1.0) or tone (0.25) from a next-chord tone
                                            that MOVES; 0.3 if outside chord and scale
 then  × 0.5 for non-chord tones on strong ticks
 then  current-chord tones topped up to at least 40% of the total
```

Here `t = smoothstep(phase)`, and

```
nextWeight = 0.10 + anticipation × smoothstep(phase) × (1.2 − 0.10)
```

The brief suggested 0.9 as the maximum. Measured against the scale term, that barely moved the line, so the maximum is 1.2. Near a phrase boundary, the future terms are boosted ×1.3.

The three kinds of future note are treated differently:
- **Common tones** (the next chord shares them with the current one) are always safe.
- **True anticipations** (future-only notes) are steered onto weak ticks.
- **Approach notes** (leading into a tone that will change) resolve when the chord arrives: a non-chord tone is followed by a step onto the new chord with ×2.5 preference.

In C → Em → F, late in the Em, this makes E (a semitone under F) the strongest note and lets F and A appear on offbeats.

**Pitch.** The weights are spread over the register (MIDI 60–84, with a gentle pull to 72), then multiplied by an interval preference: steps 1.0, thirds 0.5, fourths 0.18, fifths 0.12, larger intervals rarer still, repeats 0.3. After a leap, a step back the other way gets ×1.8. A step tends to continue as a run (×1.3). Recently used pitch classes get ×0.75 each.

**Rhythm.** The melody grid is the ADVANCED X knob: quarters, eighths (default) or sixteenths. At each grid tick where no note is sounding, a note starts with probability `0.10 + density × 1.2 × (0.4 + 0.6 × metric)`, where metric is 1.0 for the chord downbeat, 0.8 for a beat, 0.55 for an eighth and 0.35 for a sixteenth. That probability is halved in the last quarter of a phrase, so phrases breathe. Durations are drawn around 2^(1.6 − 1.6 × density) grid units.

A note sounding when the chord changes:
- is **held** if it is a common tone;
- may become a **suspension** (35%) if it sits a step above a new chord tone, resolving down onto it one grid step later without re-attacking;
- otherwise moves.

At default settings the reference renders run 2–3.5 notes per chord. About 70% are chord tones and over 80% of strong-tick notes are, with about 40% stepwise motion and 12–15% leaps over a fourth.

## 7. Voicing and glide

**Voicing.** Each chord is voiced from the previous voicing, keeping each voice's identity (voice 0 lowest), by exhaustive search. Every assignment of the three pitch classes to the three voices, across every octave inside MIDI 48–76, is about 160 options. The cost is:

```
Σ |movement|  + 0.5 per semitone a voice leaps beyond 5
              + 6 per crossing, 8 per unison
              + 0.6 per semitone of span beyond 19
              + 0.1 × |mean pitch − 62|           (stops it drifting to an edge)
```

The search is integer arithmetic in 1/30 semitone, because it runs about 100 times per plan on a core with no FPU. C(G3 C4 E4) → Em keeps G and E and moves C down to B. Every single P/L/R moves exactly one voice by one or two semitones. In the reference renders the three voices move 1.7–2.5 semitones *in total* per chord change.

**Glide** is a **time-based linear ramp in semitone space**. On a new chord each voice steps `(target − where it IS now) / blocks` per 1/3 ms, and arrives exactly at the glide time. A chord that arrives mid-glide starts from the voice's actual pitch, never from the old target, so long glides across several changes stay continuous. Oscillator phases are never reset. The ramp runs in Q24 so its integer residue cannot cause a landing jump. Harmonic analysis always uses the *intended* chords, never the instantaneous gliding pitches.

## 8. Reference numbers

From `tools/render.ps1` (180 s each; seeds fixed):

| setting | distinct chords | diatonic to home | mean distance from home | A-B-A | tonic changes | voice movement / change |
|---|---|---|---|---|---|---|
| wander 0.05 | 13 | 81% | 1.25 | 10% | 0 | 1.75 |
| wander 0.40 | 18 | 53% | 1.75 | 1% | 17 | 2.23 |
| wander 0.95 | 24 | 34% | 2.45 | 0% | 16 (3 home migrations) | 2.45 |

The opening of the wander 0.05 render reads `C Cm C G Gm G Em C Am F C Em Am C F C Em G C Am F Dm Am C`, which is tonal with borrowed colour, and the phrases come home.

## 9. Experimenting

- **Navigation strategy:** `HarmonyEngine::Score()` holds every term in one function. Add a term there and a weight in `HarmonyConfig`.
- **Move vocabulary:** add a row to `kMoves`. Distances, strict-CV widening and the tests pick it up automatically.
- **Melodic behaviour:** `MelodyEngine::PitchClassWeights()` (what to play) and `MelodyEngine::Tick()` (when).
- **Check the result:** `tools/render.ps1` prints the statistics above for any change. `tests/run_tests.ps1` guards the musical invariants: P/L/R correctness, commitment, the tonal-inference cases, anticipation growth, CV attraction, and the no-click and level checks.
