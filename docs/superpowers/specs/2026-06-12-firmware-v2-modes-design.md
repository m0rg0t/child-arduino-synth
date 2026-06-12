# Child-buzzer firmware v2 — play modes

**Date:** 2026-06-12
**Status:** Approved design
**Builds on:** `docs/superpowers/specs/2026-06-03-child-buzzer-synth-design.md` (v1)

## Goal

Extend the 7-key mini-piano firmware with three new exclusive play modes (sound
effects, built-in songs, echo/parrot) plus a pentatonic scale toggle, all
switched by two-key hold combos in the spirit of the existing vibrato gesture.
No hardware changes: same 7 keys, octave knob, and KY-006 passive buzzer.

## Constraints (unchanged from v1)

- **Monophonic.** `tone()` owns the single hardware timer; one pitch at a time.
- **Integer-only math** in `loop()`; no floats.
- **Two-file split:** `config.h` holds all tunables/data, `child_buzzer.ino`
  holds all logic.
- ATmega328P: 32 KB flash, 2 KB SRAM. Song/FX data goes in `PROGMEM`.
- No automated tests; verification = `DEBUG` serial output + manual smoke test.

## Architecture

### The one-frequency contract

Every mode reduces to the contract the v1 piano already satisfies: *each tick,
produce one target frequency or −1 (silence).* All engines (piano,
FX sweep, song player, echo playback) feed the same final
`tone()`/`noTone()` write guarded by the `lastFreqWritten` dirty-flag.
Because the layering toggles (vibrato, pentatonic) act on that single
frequency path, they apply in every mode for free.

### Mode state machine

```c
enum Mode : uint8_t { MODE_PIANO, MODE_FX, MODE_SONG, MODE_ECHO };
```

- Default `MODE_PIANO` at power-up (no persistence across power cycles).
- Holding **keys 2+4** for `COMBO_HOLD_MS` cycles Piano → FX → Song → Echo →
  Piano. Entering mode *k* is announced with *k+1* short beeps (1 = Piano,
  4 = Echo). Beep announce may be briefly blocking, like v1 `chirp()`.
- Switching modes silences output and resets the engines (stops songs,
  clears the echo buffer, kills any running FX).

### Combo gestures (table-driven)

The v1 single-purpose `updateComboToggle` generalizes into a table of three
entries `{keyA, keyB, callback}`, each with its own edge-triggered hold state:

| Keys | Hold time | Action | Audio feedback |
|------|-----------|--------|----------------|
| 0+6 (ends) | `COMBO_HOLD_MS` | toggle vibrato (v1, unchanged) | rising 2-beep chirp |
| 1+5 | `COMBO_HOLD_MS` | toggle pentatonic | falling 2-beep chirp |
| 2+4 | `COMBO_HOLD_MS` | cycle mode | N-beep mode announce |

While any combo pair is physically held, those keys are suppressed from note
playback. The v1 `suppressEnds` bool becomes a 7-bit suppression mask passed
to `activeNote()`.

### Orthogonal toggles

- **Vibrato (0+6):** v1 behavior, applies to the final frequency in any mode.
- **Pentatonic (1+5):** selects which 7-entry note table `noteFrequency()`
  reads. Second table in `config.h`: C-major pentatonic spanning into the
  next octave — `{262, 294, 330, 392, 440, 523, 587}` (C D E G A C′ D′).
  Octave doubling via `<< band` is unchanged. Affects piano/echo key input;
  songs and FX use their own pitch data and ignore it.

## Modes

### MODE_PIANO

Exactly v1 behavior (last-pressed-wins, knob = octave band), plus the new
toggles/combos above.

### MODE_FX — sound effects

Each key triggers one effect. All effects run on **one generic non-blocking
sweep engine**; an effect is a parameter row (stored in `config.h`):

```c
struct FxDef { uint16_t startHz, endHz; int16_t stepHz; uint8_t stepMs; uint8_t behavior; };
// behavior: FX_ONCE (play sweep once, then silence), FX_LOOP (wrap to start),
//           FX_PINGPONG (reverse at the ends)
```

Provisional key map (tunable):

| Key | Effect | Shape |
|-----|--------|-------|
| 0 | siren | slow PINGPONG, ~600–1200 Hz |
| 1 | laser zap | fast ONCE, high→low |
| 2 | slide whistle | medium ONCE, low→high |
| 3 | UFO warble | fast PINGPONG, narrow span |
| 4 | alarm | PINGPONG with step = full span (two-tone) |
| 5 | bird chirp | fast ONCE, short rising, retriggers while held |
| 6 | robot babble | LOOP with coarse steps across a wide span |

Effect sounds while the key is held; `FX_ONCE` runs to completion then goes
silent until re-press. Releasing the key silences immediately.
Last-pressed-wins applies, same as notes. The knob is unused in FX mode.

### MODE_SONG — built-in melodies

One melody per key, stored in `PROGMEM` as `{degree, duration}` pairs:

- `degree` = scale-degree index 0–6 into `NOTE_HZ` plus a small octave
  offset (packed in one byte; a sentinel value encodes a rest).
- `duration` = multiples of a base tick so the **knob controls tempo**
  (octave is meaningless mid-song; tempo is fun). Mapping: knob low = slow,
  high = fast, over a tunable BPM range.

Song list (key 0 → 6):

1. Twinkle Twinkle Little Star
2. Mary Had a Little Lamb
3. Ode to Joy
4. London Bridge
5. Frère Jacques
6. Smoke on the Water (main riff)
7. We Will Rock You (chant hook; the stomp-stomp-clap rendered as low
   ~80–110 Hz thuds)

Note on Smoke on the Water: the riff's intervals (0, +3, +5, +6 semitones)
are not playable from G in C major, but transposed to start on **B** they
land entirely on white keys: B–D–E | B–D–F–E | B–D–E | D–B. The
scale-degree encoding therefore needs no accidentals.

Interaction: pressing a key starts that song from the top; pressing a
*different* key switches songs; pressing the *same* key stops playback.
Playback is a non-blocking ticker in `loop()`. A song plays to its end and
stops (no looping).

### MODE_ECHO — record & parrot

The kid plays normally (full piano behavior, knob = octave, toggles apply)
while a recorder logs events:

```c
struct EchoEvent { uint8_t noteAndBand; uint16_t durationMs; uint16_t gapMs; };
// noteAndBand packs note index (low 3 bits) + octave band (next 2 bits)
// ECHO_MAX_EVENTS = 32  →  ~160 B of SRAM
```

- After `ECHO_SILENCE_MS` (default 1500 ms, tunable) with no key held and a
  non-empty buffer, the toy replays the recorded phrase (same notes,
  durations, and gaps, at the octave band captured per event... see below),
  then clears the buffer and is ready to record again.
- Recorded pitch: store the *final computed frequency band* alongside the
  note, i.e. record `{note, band}` at press time so knob twiddling during
  playback doesn't shift the parrot.
- Durations and gaps are clamped to `uint16_t` max (~65 s — far beyond toy
  attention spans).
- Buffer full → recording stops silently; the replay still happens at the
  silence timeout.
- Key presses during playback abort the replay and start a fresh recording.

## config.h additions (tunables/data only)

- `NOTE_HZ_PENTA[7]` second note table
- combo pair definitions + (existing) `COMBO_HOLD_MS`
- `FX_DEFS[7]` parameter rows + behavior enum constants
- `SONGS[7]` `PROGMEM` note tables + per-song lengths; tempo knob BPM range
- `ECHO_MAX_EVENTS`, `ECHO_SILENCE_MS`
- mode-announce beep pitch/duration

## child_buzzer.ino additions (logic only)

- `Mode` enum + mode cycle handling
- table-driven combo detector (replaces `updateComboToggle`)
- suppression mask in `activeNote()`
- FX sweep engine (one state struct: current Hz, direction, next-step time)
- song player ticker (current song, position, next-event time, tempo from knob)
- echo recorder + playback ticker
- `loop()` dispatches by mode to compute the tick's target frequency, then the
  existing shared vibrato + dirty-flag `tone()` write

## Error handling / edge cases

- Mode switch mid-sound: silence output, reset all engine state.
- Combo keys are suppressed as notes while held (existing v1 pattern).
- Blocking announce beeps (≤ ~500 ms) are acceptable, consistent with v1
  `chirp()`; `lastFreqWritten` is reset to −1 afterward.
- Echo buffer overflow: drop further events, keep what fits.
- All frequency math stays in `int32_t` intermediate / `uint16_t` result,
  as in v1.

## Verification (manual; no test harness)

Extend the `DEBUG` serial line with `mode=`. Smoke-test checklist:

1. Power-up → piano mode; v1 behaviors all intact (scale, octave knob,
   vibrato combo, debounce).
2. Hold 1+5 → falling chirp; keys now pentatonic; toggle back.
3. Hold 2+4 → 2 beeps → FX mode; each key produces its distinct effect;
   release silences; ONCE effects don't loop.
4. Hold 2+4 → 3 beeps → song mode; each key starts its song; same key stops;
   different key switches; knob audibly changes tempo; Smoke on the Water
   riff recognizable.
5. Hold 2+4 → 4 beeps → echo mode; play a short phrase, wait ~1.5 s, hear it
   parroted back with timing; playing during replay interrupts it.
6. Hold 2+4 → 1 beep → back to piano.
7. Vibrato toggle works in every mode (wobbles songs/FX/echo playback).
8. Verify flash/SRAM usage in `arduino-cli compile` output stays comfortable
   (flash < 80%, SRAM < 50%).

## Out of scope

- Audio-jack line out (still deferred to v2 hardware, see v1 spec appendix).
- Mode persistence across power cycles (EEPROM) — not needed for a toy.
- Polyphony/arpeggio chords — cut during brainstorming.
- Games (Simon, follow-me teaching) — cut during brainstorming.
