# Child Buzzer Synth — Design Spec

**Date:** 2026-06-03
**Status:** Approved (design); implementation pending
**v1 scope:** mini-piano + octave knob + vibrato. The line/aux audio jack is
**deferred to a later version** — its design is preserved in the appendix.

## Overview

A simple, sturdy mini-piano toy for a child. Seven keyboard switches play the
notes of a one-octave C-major scale through a KY-006 passive buzzer. A rotary
knob shifts the whole keyboard up or down by octaves (deep ↔ squeaky). A
software "squeeze both ends" gesture toggles a vibrato (pitch-wobble) effect.

A passive line/aux audio jack is planned for a later version (see appendix); v1
ships with the on-board buzzer only.

## Goals

- Intuitive for a small child: press a key → hear a note; turn the knob → pitch
  changes; no menus or screens.
- Pleasant, musical output (diatonic scale, in tune).
- Buildable from exactly the parts on hand — no extra components required.
- Firmware that is easy to re-tune (notes, octaves, timings, vibrato) without
  editing logic.

## Hardware / Bill of Materials

Exactly the parts already specified — nothing extra for v1:

| Qty | Part | Notes |
|----:|------|-------|
| 1 | Arduino Nano | The brain |
| 7 | Keyboard switches (mechanical key switches) | One per piano key |
| 1 | Rotary angle sensor (analog potentiometer) | Octave knob — NOT an encoder |
| 1 | KY-006 passive piezo buzzer | Driven by `tone()` square wave |

The line/aux audio jack and its passives are listed in the appendix (deferred).

### Pin map

| Signal | Pin | Mode | Wiring |
|--------|-----|------|--------|
| Key 1 (C, lowest) | D2 | `INPUT_PULLUP` | switch other side → GND |
| Key 2 (D) | D3 | `INPUT_PULLUP` | switch → GND |
| Key 3 (E) | D4 | `INPUT_PULLUP` | switch → GND |
| Key 4 (F) | D5 | `INPUT_PULLUP` | switch → GND |
| Key 5 (G) | D6 | `INPUT_PULLUP` | switch → GND |
| Key 6 (A) | D7 | `INPUT_PULLUP` | switch → GND |
| Key 7 (B, highest) | D8 | `INPUT_PULLUP` | switch → GND |
| Buzzer | D9 | `OUTPUT` (`tone`) | KY-006 signal pin; GND to GND |
| Octave knob | A0 | analog in | pot wiper → A0; ends → 5V and GND |

`INPUT_PULLUP` means each switch only needs to connect its pin to GND when
pressed — no external resistors. A pressed key therefore reads `LOW`.

> **Audio jack:** deferred to a later version. The full circuit and parts list
> live in the appendix so the v1 build stays focused on the buzzer.

## Behavior

### 1. Mini-piano (free play)

- The 7 keys map to one octave of the **C-major scale**:
  `C D E F G A B` (do-re-mi-fa-sol-la-ti).
- **Sound while held:** pressing a key starts its note; releasing stops it.
- **Monophonic, last-pressed-wins:** only one note sounds at a time (single tone
  timer). The most recently pressed key that is still held is the one that
  sounds. When it is released, sound falls back to another currently-held key
  (most recent of those). When no key is held, the buzzer is silent (`noTone()`).
- Each key is **debounced** (~5 ms stable-state filter) to reject contact bounce.

### 2. Octave knob

- `analogRead(A0)` (0–1023) is mapped to **4 octave bands** (index 0–3):
  `band = (raw * 4) / 1024` → 0,1,2,3 across the knob's travel.
- Octave shift is applied by doubling frequency: `freq = noteHz[key] << band`.
  Doubling is exact and avoids floating-point math.
- Re-read every loop, so turning the knob **while holding a key** slides the
  pitch by whole octaves live.
- Base octave is C4: band 0 = C4 (262 Hz) … band 3 ≈ B7 (~3951 Hz) — squarely in
  the piezo's loudest range so even the lowest keys are clearly audible.

### 3. Vibrato toggle (software combo)

- **Gesture:** hold the **lowest + highest keys together** (Key 1 + Key 7) for
  **~600 ms** to toggle vibrato on/off.
- **Edge-triggered:** the toggle fires once when the combo is recognized, then
  will not fire again until both keys are released. This prevents rapid
  on/off flapping while the combo is held.
- During the combo, those two keys' **notes are suppressed** (no C/B note plays).
- On toggle, play a short **confirmation chirp** (e.g. two quick beeps) so the
  child gets feedback even though there is no screen.

### 4. Vibrato effect

- When **on**, the sounding note's pitch wobbles **±~4%** around the base note at
  **~6 Hz**.
- Implemented by re-calling `tone(BUZZER, modulatedFreq)` on a fixed update tick
  (e.g. every ~10 ms). `tone()` keeps the buzzer running on its timer; we just
  nudge the frequency on schedule.
- The wobble shape comes from a small **lookup table** (or triangle wave) to
  avoid slow floating-point `sin()` in the loop.
- When **off**, the note plays at a steady frequency.

## Firmware architecture

Approach A — single sketch + a hardware/config header:

- **`child_buzzer.ino`** — all logic:
  - `setup()` — configure pin modes.
  - `loop()` — orchestrate: read octave band, scan keys, detect combo, pick the
    active note, apply vibrato, drive `tone()` / `noTone()`.
  - `scanKeys()` — per-key debounce; maintain pressed state + press order.
  - `activeNoteIndex()` — apply last-pressed-wins to choose the sounding key
    (or none).
  - `readOctaveBand()` — knob → octave band 0–3.
  - `updateVibratoCombo()` — detect the both-ends hold, edge-triggered toggle,
    confirmation chirp.
  - `applyVibrato(baseFreq)` — return modulated frequency from the LUT when
    vibrato is on, else `baseFreq`.
- **`config.h`** — everything tweakable without touching logic:
  - Pin numbers (keys, buzzer, knob).
  - `noteHz[7]` — C-major base frequencies (octave 0 / C4).
  - `OCTAVE_BANDS` (default 4).
  - `VIBRATO_DEPTH_PCT` (~4), `VIBRATO_RATE_HZ` (~6), vibrato update interval.
  - `DEBOUNCE_MS` (~5), `COMBO_HOLD_MS` (~600).
  - `DEBUG` flag.
- **`WIRING.md`** — pin-by-pin connection table incl. the jack RC network.
- **`README.md`** — what it is + how to flash (Arduino IDE: select the Nano
  board + port, then upload).
- Existing **`stl/`** models and **`screenshoots/`** render are left untouched.

## Verification

Arduino firmware has no host test harness here; verification is debug output +
a manual smoke test.

- **`DEBUG` flag** prints to Serial: detected active key, octave band, computed
  frequency, and vibrato on/off state — so behavior is observable, not guessed.
- **Manual smoke-test checklist:**
  1. Each of the 7 keys sounds its own ascending note; releasing silences it.
  2. Pressing a new key while holding another switches to the new note;
     releasing it falls back to the still-held key.
  3. Turning the knob across its range shifts all keys through 4 octave bands.
  4. Holding both end keys ~600 ms plays the confirmation chirp and toggles
     vibrato; a single toggle per gesture (no flapping).
  5. With vibrato on, a held note audibly wobbles; with it off, the note is
     steady.

## Out of scope (possible future work)

- **Line/aux audio jack** — designed and parts-listed below; deferred to v2.
- Polyphony (would need more than the single tone timer).
- Selectable scales / instruments via the knob.
- Hardware vibrato switch or auto-mute switched jack.
- On-board amplifier for direct, loud headphone drive.

## Open tunables (decide during build by ear)

- Exact vibrato depth/rate (start ±4% / 6 Hz).
- Octave band base/range (start C4 → B7, 4 bands).
- Confirmation chirp pattern.

## Appendix — Line/aux audio jack (deferred to v2)

Passive add-on that hangs in parallel off the D9 tone signal — no firmware
change. Lets the toy feed powered speakers / an amp / aux-in while the on-board
buzzer keeps playing.

```
D9 ──[ ~1 kΩ ]──┬──[ 10 µF + ]──┬── Jack TIP ─┐
                │   (+ to D9)    │             ├─ tied together (mono → both ears)
              [10 kΩ]            └── Jack RING ┘
                │
               GND ───────────────────────────── Jack SLEEVE
```

- Coupling cap blocks the square wave's DC offset (no DC through the speaker).
- Series + shunt resistors attenuate the 5 V swing toward a safe line level and
  gently low-pass the square wave. Raise the series resistor to lower the level.
- Tip + ring tied so the mono tone reaches both channels.

### Parts to buy (jack add-on)

| # | Part | Spec to search for | Qty | Notes |
|---|------|--------------------|----:|-------|
| 1 | 3.5 mm stereo panel-mount jack (TRS, female) | "3.5mm stereo panel mount jack threaded nut" / PJ-392 | 1 | Threaded barrel + nut to screw into the enclosure hole. Confirm thread matches the hole (commonly 6 mm). |
| 2 | Coupling capacitor 10 µF | "10µF 25V electrolytic" (+ toward D9) **or** 1–10 µF film (non-polarized) | 1 | Rating ≥16 V. |
| 3 | Series resistor 1 kΩ | "1kΩ 1/4W 5%" | 1 | Current limit / attenuation. |
| 4 | Shunt resistor 10 kΩ | "10kΩ 1/4W 5%" | 1 | Sets DC level / divider to ground. |
| 5 | Hookup wire + heat-shrink (optional) | "22 AWG hookup wire", "2mm heat shrink" | — | Tidy solder joints to the jack lugs. |

Open tunable: jack attenuation — start 1 kΩ series / 10 kΩ shunt / 10 µF cap,
adjust series R by ear.
