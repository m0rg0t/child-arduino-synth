# Child Buzzer Synth v1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the firmware for a 7-key mini-piano toy on an Arduino Nano: keys play a C-major scale through a KY-006 passive buzzer, a knob shifts the octave, and a "squeeze both ends" gesture toggles a vibrato wobble.

**Architecture:** A single Arduino sketch (`child_buzzer.ino`) with all hardware logic, plus a `config.h` holding every tunable (pins, note table, octave count, timings, vibrato, DEBUG). No external libraries — only the built-in `tone()`/`noTone()`, `digitalRead`, `analogRead`, `millis`. Monophonic output (one tone timer) with last-pressed-wins note selection. Verification is by clean compile + a manual hardware smoke test (no host test harness, per the approved spec).

**Tech Stack:** Arduino Nano (ATmega328P), Arduino C++, `tone()` square-wave output, internal pull-ups for switches, analog read for the potentiometer.

---

## File Structure

```
child-buzzer/                       (existing git repo)
├── firmware/
│   └── child_buzzer/               (Arduino sketch folder — name matches .ino)
│       ├── child_buzzer.ino        (all logic: setup, loop, keys, octave, vibrato)
│       └── config.h                (pins, note table, octaves, timings, vibrato, DEBUG)
├── WIRING.md                       (pin-by-pin connection guide)
├── README.md                       (what it is + how to flash + how to play)
├── docs/superpowers/...            (spec + this plan)
├── stl/                            (existing 3D models — untouched)
└── screenshoots/                   (existing render — untouched)
```

Responsibilities:
- **`config.h`** — the only file you edit to re-tune behavior. Pure constants.
- **`child_buzzer.ino`** — reads hardware, decides the active note, drives the buzzer.
- **`WIRING.md` / `README.md`** — build + usage docs.

## Compile verification (used by every code task)

Two options. Use whichever you have.

- **arduino-cli** (one-time setup `arduino-cli core install arduino:avr`):
  ```bash
  arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer
  ```
  Expected: ends with `Sketch uses NNNN bytes ... Global variables use ...` and exit code 0. For old-bootloader clones the upload step later uses `--fqbn arduino:avr:nano:cpu=atmega328old`; the compile FQBN above is fine either way.
- **Arduino IDE:** open `firmware/child_buzzer/child_buzzer.ino`, click **Verify (✓)**. Expected: "Done compiling." with no errors.

---

### Task 1: Sketch scaffold — `config.h` + minimal compiling sketch

**Files:**
- Create: `firmware/child_buzzer/config.h`
- Create: `firmware/child_buzzer/child_buzzer.ino`

- [ ] **Step 1: Create `config.h` with all tunables**

```cpp
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---- Pins ----
// 7 keyboard switches, lowest (C) -> highest (B). Each switch wired to GND,
// read with INPUT_PULLUP, so a pressed key reads LOW.
const uint8_t KEY_PINS[7] = { 2, 3, 4, 5, 6, 7, 8 };
const uint8_t BUZZER_PIN  = 9;     // KY-006 passive buzzer signal pin
const uint8_t KNOB_PIN    = A0;    // rotary angle sensor (potentiometer) wiper

// ---- Notes ----
// Base frequencies (Hz) for octave band 0 = C-major scale starting at C4.
// Index 0..6 = C D E F G A B. Higher octave bands multiply by 2 per band.
const uint16_t NOTE_HZ[7]  = { 262, 294, 330, 349, 392, 440, 494 };
const uint8_t  OCTAVE_BANDS = 4;   // knob selects band 0..3  (C4 .. ~B7)

// ---- Timing ----
const uint16_t DEBOUNCE_MS   = 5;    // per-key debounce window
const uint16_t COMBO_HOLD_MS = 600;  // hold both end keys this long to toggle vibrato

// ---- Vibrato ----
const uint16_t VIBRATO_DEPTH_PERMILLE = 40;  // +/-4.0% pitch wobble
const uint16_t VIBRATO_UPDATE_MS      = 10;  // how often to nudge the pitch
// 16 LUT steps * 10 ms = 160 ms per cycle ~= 6.25 Hz wobble.

// ---- Debug ----
#define DEBUG 0   // set to 1 to print state over Serial @115200

#endif // CONFIG_H
```

- [ ] **Step 2: Create minimal `child_buzzer.ino`**

```cpp
#include "config.h"

void setup() {
  for (uint8_t i = 0; i < 7; i++) {
    pinMode(KEY_PINS[i], INPUT_PULLUP);
  }
  pinMode(BUZZER_PIN, OUTPUT);
#if DEBUG
  Serial.begin(115200);
#endif
}

void loop() {
  // Behavior is added in later tasks.
}
```

- [ ] **Step 3: Verify it compiles**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer` (or IDE Verify)
Expected: compiles cleanly, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add firmware/child_buzzer/config.h firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): scaffold sketch with pin setup and tunable config.h"
```

---

### Task 2: Playable mono piano — keys, debounce, last-pressed-wins, octave knob

This is the mid-build hardware checkpoint: after this task you can flash it and play 7 notes across 4 octaves. No vibrato yet.

**Files:**
- Modify: `firmware/child_buzzer/child_buzzer.ino` (replace entire file)

- [ ] **Step 1: Replace `child_buzzer.ino` with the mono-piano version**

```cpp
#include "config.h"

// ---- Per-key debounce + press-order state ----
bool     keyHeld[7]      = { false, false, false, false, false, false, false };
bool     lastReading[7]  = { false, false, false, false, false, false, false };
uint32_t lastChangeMs[7] = { 0, 0, 0, 0, 0, 0, 0 };
uint32_t pressOrder[7]   = { 0, 0, 0, 0, 0, 0, 0 };  // 0 = not held; larger = newer
uint32_t pressCounter    = 0;

int      lastFreqWritten = -1;  // -1 means noTone() is currently active

void setup() {
  for (uint8_t i = 0; i < 7; i++) {
    pinMode(KEY_PINS[i], INPUT_PULLUP);
  }
  pinMode(BUZZER_PIN, OUTPUT);
#if DEBUG
  Serial.begin(115200);
#endif
}

// Debounce one key and update keyHeld[i] + pressOrder[i].
void debounceKey(uint8_t i, uint32_t now) {
  bool reading = (digitalRead(KEY_PINS[i]) == LOW);  // pressed shorts pin to GND
  if (reading != lastReading[i]) {
    lastReading[i] = reading;
    lastChangeMs[i] = now;
  }
  if (now - lastChangeMs[i] >= DEBOUNCE_MS && reading != keyHeld[i]) {
    keyHeld[i] = reading;
    pressOrder[i] = reading ? ++pressCounter : 0;  // newest press wins; 0 on release
  }
}

// Most-recently-pressed key still held (0..6), or -1 if none.
// When suppressEnds is true, keys 0 and 6 are ignored (used during the combo).
int activeNote(bool suppressEnds) {
  int best = -1;
  uint32_t bestOrder = 0;
  for (uint8_t i = 0; i < 7; i++) {
    if (!keyHeld[i]) continue;
    if (suppressEnds && (i == 0 || i == 6)) continue;
    if (pressOrder[i] > bestOrder) {
      bestOrder = pressOrder[i];
      best = i;
    }
  }
  return best;
}

// Frequency for a note index at an octave band (doubling per band; exact).
uint16_t noteFrequency(int noteIndex, uint8_t band) {
  return (uint16_t)(NOTE_HZ[noteIndex] << band);
}

// Knob (0..1023) -> octave band 0..OCTAVE_BANDS-1.
uint8_t readOctaveBand() {
  int raw = analogRead(KNOB_PIN);
  uint8_t band = (uint8_t)((uint32_t)raw * OCTAVE_BANDS / 1024);
  if (band >= OCTAVE_BANDS) band = OCTAVE_BANDS - 1;
  return band;
}

void loop() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < 7; i++) {
    debounceKey(i, now);
  }

  int note = activeNote(false);
  uint8_t band = readOctaveBand();

  int freq = (note < 0) ? -1 : (int)noteFrequency(note, band);

  if (freq != lastFreqWritten) {
    if (freq < 0) {
      noTone(BUZZER_PIN);
    } else {
      tone(BUZZER_PIN, (unsigned int)freq);
    }
    lastFreqWritten = freq;
  }
}
```

- [ ] **Step 2: Verify it compiles**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer` (or IDE Verify)
Expected: compiles cleanly, exit code 0.

- [ ] **Step 3: (Optional) Flash and spot-check on hardware**

Flash to the Nano. Expected: each key plays an ascending note while held; releasing silences it; pressing a second key takes over and releasing it falls back to the still-held key; turning the knob shifts all notes across octaves.

- [ ] **Step 4: Commit**

```bash
git add firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): playable mono piano — debounced keys, last-pressed-wins, octave knob"
```

---

### Task 3: Vibrato effect + "squeeze both ends" combo toggle + confirmation chirp

**Files:**
- Modify: `firmware/child_buzzer/child_buzzer.ino` (replace entire file)

- [ ] **Step 1: Replace `child_buzzer.ino` with the final version**

```cpp
#include "config.h"

// ---- Per-key debounce + press-order state ----
bool     keyHeld[7]      = { false, false, false, false, false, false, false };
bool     lastReading[7]  = { false, false, false, false, false, false, false };
uint32_t lastChangeMs[7] = { 0, 0, 0, 0, 0, 0, 0 };
uint32_t pressOrder[7]   = { 0, 0, 0, 0, 0, 0, 0 };  // 0 = not held; larger = newer
uint32_t pressCounter    = 0;

int      lastFreqWritten = -1;  // -1 means noTone() is currently active

// ---- Vibrato wobble shape: one sine cycle, 16 steps, scaled to +/-1000 ----
const uint8_t VIBRATO_STEPS = 16;
const int16_t VIBRATO_LUT[VIBRATO_STEPS] = {
     0,   383,   707,   924,  1000,   924,   707,   383,
     0,  -383,  -707,  -924, -1000,  -924,  -707,  -383
};

bool     vibratoOn      = false;
uint8_t  vibratoPhase   = 0;
uint32_t lastVibratoMs  = 0;

// ---- Combo (both end keys held) toggle state ----
bool     comboActive    = false;  // both ends currently held (candidate window)
bool     comboFired     = false;  // already toggled during this hold
uint32_t comboStartMs   = 0;

void setup() {
  for (uint8_t i = 0; i < 7; i++) {
    pinMode(KEY_PINS[i], INPUT_PULLUP);
  }
  pinMode(BUZZER_PIN, OUTPUT);
#if DEBUG
  Serial.begin(115200);
#endif
}

// Debounce one key and update keyHeld[i] + pressOrder[i].
void debounceKey(uint8_t i, uint32_t now) {
  bool reading = (digitalRead(KEY_PINS[i]) == LOW);  // pressed shorts pin to GND
  if (reading != lastReading[i]) {
    lastReading[i] = reading;
    lastChangeMs[i] = now;
  }
  if (now - lastChangeMs[i] >= DEBOUNCE_MS && reading != keyHeld[i]) {
    keyHeld[i] = reading;
    pressOrder[i] = reading ? ++pressCounter : 0;  // newest press wins; 0 on release
  }
}

// Most-recently-pressed key still held (0..6), or -1 if none.
// When suppressEnds is true, keys 0 and 6 are ignored (used during the combo).
int activeNote(bool suppressEnds) {
  int best = -1;
  uint32_t bestOrder = 0;
  for (uint8_t i = 0; i < 7; i++) {
    if (!keyHeld[i]) continue;
    if (suppressEnds && (i == 0 || i == 6)) continue;
    if (pressOrder[i] > bestOrder) {
      bestOrder = pressOrder[i];
      best = i;
    }
  }
  return best;
}

// Frequency for a note index at an octave band (doubling per band; exact).
uint16_t noteFrequency(int noteIndex, uint8_t band) {
  return (uint16_t)(NOTE_HZ[noteIndex] << band);
}

// Knob (0..1023) -> octave band 0..OCTAVE_BANDS-1.
uint8_t readOctaveBand() {
  int raw = analogRead(KNOB_PIN);
  uint8_t band = (uint8_t)((uint32_t)raw * OCTAVE_BANDS / 1024);
  if (band >= OCTAVE_BANDS) band = OCTAVE_BANDS - 1;
  return band;
}

// Apply the current vibrato phase to a base frequency.
// modulated = base * (1000 + depth_permille * lut/1000) / 1000
uint16_t applyVibrato(uint16_t baseFreq) {
  int16_t lut = VIBRATO_LUT[vibratoPhase];                 // -1000..1000
  int32_t factor = 1000 + (int32_t)VIBRATO_DEPTH_PERMILLE * lut / 1000;
  return (uint16_t)((int32_t)baseFreq * factor / 1000);
}

// Two quick beeps confirming a vibrato toggle. Briefly blocking (~220 ms) — fine
// for a toy; the combo is being held so no note should be sounding anyway.
void chirp() {
  tone(BUZZER_PIN, 1568); delay(70);   // ~G6
  noTone(BUZZER_PIN);     delay(40);
  tone(BUZZER_PIN, 2093); delay(70);   // ~C7
  noTone(BUZZER_PIN);     delay(40);
  lastFreqWritten = -1;                // force the next loop to re-assert the tone
}

// Detect the "both end keys held for COMBO_HOLD_MS" gesture and toggle vibrato
// exactly once per hold (edge-triggered).
void updateComboToggle(uint32_t now) {
  bool bothEnds = keyHeld[0] && keyHeld[6];
  if (bothEnds) {
    if (!comboActive) {
      comboActive = true;
      comboFired = false;
      comboStartMs = now;
    } else if (!comboFired && (now - comboStartMs >= COMBO_HOLD_MS)) {
      comboFired = true;
      vibratoOn = !vibratoOn;
      chirp();
    }
  } else {
    comboActive = false;
    comboFired = false;
  }
}

void loop() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < 7; i++) {
    debounceKey(i, now);
  }

  updateComboToggle(now);

  // Advance the vibrato wobble on its own tick.
  if (vibratoOn && (now - lastVibratoMs >= VIBRATO_UPDATE_MS)) {
    lastVibratoMs = now;
    vibratoPhase = (vibratoPhase + 1) % VIBRATO_STEPS;
  }

  bool suppressEnds = (keyHeld[0] && keyHeld[6]);
  int note = activeNote(suppressEnds);
  uint8_t band = readOctaveBand();

  int freq;
  if (note < 0) {
    freq = -1;
  } else {
    uint16_t base = noteFrequency(note, band);
    freq = vibratoOn ? applyVibrato(base) : base;
  }

  if (freq != lastFreqWritten) {
    if (freq < 0) {
      noTone(BUZZER_PIN);
    } else {
      tone(BUZZER_PIN, (unsigned int)freq);
    }
    lastFreqWritten = freq;
  }

#if DEBUG
  static uint32_t lastDbg = 0;
  if (now - lastDbg >= 200) {
    lastDbg = now;
    Serial.print(F("note=")); Serial.print(note);
    Serial.print(F(" band=")); Serial.print(band);
    Serial.print(F(" freq=")); Serial.print(freq);
    Serial.print(F(" vib=")); Serial.println(vibratoOn ? 1 : 0);
  }
#endif
}
```

- [ ] **Step 2: Verify it compiles**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer` (or IDE Verify)
Expected: compiles cleanly, exit code 0.

- [ ] **Step 3: Commit**

```bash
git add firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): add vibrato wobble + squeeze-both-ends toggle with chirp + DEBUG"
```

---

### Task 4: Wiring guide

**Files:**
- Create: `WIRING.md`

- [ ] **Step 1: Create `WIRING.md`**

```markdown
# Wiring — Child Buzzer Synth (v1)

Board: Arduino Nano. All switches use the internal pull-ups, so a pressed key
just connects its pin to GND (reads LOW). No external resistors needed.

## Keyboard switches (7)

| Key | Note | Nano pin | Switch wiring |
|-----|------|----------|---------------|
| 1 (lowest)  | C | D2 | one leg → D2, other leg → GND |
| 2 | D | D3 | one leg → D3, other leg → GND |
| 3 | E | D4 | one leg → D4, other leg → GND |
| 4 | F | D5 | one leg → D5, other leg → GND |
| 5 | G | D6 | one leg → D6, other leg → GND |
| 6 | A | D7 | one leg → D7, other leg → GND |
| 7 (highest) | B | D8 | one leg → D8, other leg → GND |

Tie every switch's "other leg" to a shared GND rail.

## KY-006 passive buzzer

| KY-006 pin | Connect to |
|------------|------------|
| S (signal) | D9 |
| − (GND)    | GND |
| middle (+) | leave unconnected (passive module; varies by board) |

## Rotary angle sensor (potentiometer) — octave knob

| Pot pin | Connect to |
|---------|------------|
| one outer leg   | 5V |
| other outer leg | GND |
| wiper (middle)  | A0 |

If the knob shifts pitch the "wrong" way, swap the two outer legs (5V ↔ GND).

## Power & ground

Power the Nano over USB (or regulated 5V). All grounds (switches, buzzer, pot)
share one common GND.

## How to play

- Press a key → its note sounds while held (one note at a time, newest wins).
- Turn the knob → shifts all keys across 4 octaves (deep ↔ squeaky).
- Hold the lowest + highest keys together (~0.6 s) → toggles vibrato; a two-beep
  chirp confirms.
```

- [ ] **Step 2: Commit**

```bash
git add WIRING.md
git commit -m "docs: add pin-by-pin wiring guide"
```

---

### Task 5: Project README

**Files:**
- Create: `README.md`

- [ ] **Step 1: Create `README.md`**

````markdown
# Child Buzzer Synth

A simple 7-key mini-piano toy for a child, built on an Arduino Nano with a
KY-006 passive buzzer. Seven keys play a C-major scale, a knob shifts the
octave, and a "squeeze both ends" gesture toggles a vibrato wobble.

Design spec: `docs/superpowers/specs/`. Wiring: `WIRING.md`. Enclosure models:
`stl/`.

## Firmware

- Sketch: `firmware/child_buzzer/child_buzzer.ino`
- Tunables (pins, note table, octaves, timings, vibrato, DEBUG):
  `firmware/child_buzzer/config.h`

### Flashing with the Arduino IDE

1. Open `firmware/child_buzzer/child_buzzer.ino`.
2. Tools → Board → **Arduino Nano**.
3. Tools → Processor → **ATmega328P** (or **"ATmega328P (Old Bootloader)"** for clones).
4. Tools → Port → your Nano's serial port.
5. Click **Upload (→)**.

### Flashing with arduino-cli

```bash
arduino-cli core install arduino:avr
arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer
arduino-cli upload  --fqbn arduino:avr:nano -p /dev/cu.usbserial-XXXX firmware/child_buzzer
# Old-bootloader clones: add :cpu=atmega328old to the FQBN.
```

## How to play

- **Press a key** → its note sounds while held (one note at a time; newest key wins).
- **Turn the knob** → moves all keys up/down by octaves (deep ↔ squeaky).
- **Hold lowest + highest keys together (~0.6 s)** → toggles vibrato on/off
  (two-beep chirp confirms).

## Roadmap

- v2: line/aux audio output jack (design + parts list in the spec appendix).
````

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add project README with flashing and play instructions"
```

---

### Task 6: Hardware smoke test

No automated tests (per the approved spec) — this is the acceptance check. Flash
`firmware/child_buzzer` to the Nano wired per `WIRING.md`, then verify each item.

- [ ] **Step 1: Flash the final firmware to the Nano**

```bash
arduino-cli upload --fqbn arduino:avr:nano -p /dev/cu.usbserial-XXXX firmware/child_buzzer
```
(or Upload from the Arduino IDE)

- [ ] **Step 2: Run the smoke-test checklist (by ear)**

  - [ ] Each of the 7 keys sounds its own ascending note; releasing silences it.
  - [ ] Pressing a new key while holding another switches to the new note; releasing it falls back to the still-held key.
  - [ ] Turning the knob across its range steps all keys through 4 octave bands (low → high).
  - [ ] Holding both end keys ~0.6 s plays the two-beep chirp and toggles vibrato; only one toggle per hold (no rapid flapping).
  - [ ] With vibrato on, a held note audibly wobbles; with it off, the note is steady.
  - [ ] (Optional) Set `DEBUG 1` in `config.h`, re-flash, open Serial Monitor @115200, and confirm `note/band/freq/vib` track what you do. Set back to `0` after.

- [ ] **Step 3: Record the result**

If anything fails, debug with `DEBUG 1` before changing logic. When all pass, the
v1 build is done.

---

## Self-Review

**Spec coverage:**
- Mini-piano, 7 keys = C-major, sound-while-held → Task 2 (`debounceKey`, `activeNote`, `loop`).
- Monophonic last-pressed-wins → Task 2 (`pressOrder` + `activeNote`).
- ~5 ms debounce → Task 2 (`debounceKey`, `DEBOUNCE_MS`).
- Octave knob → 4 bands, live shift, doubling frequency → Task 2 (`readOctaveBand`, `noteFrequency`).
- Vibrato toggle via both-ends ~600 ms, edge-triggered, ends suppressed, confirmation chirp → Task 3 (`updateComboToggle`, `chirp`, `activeNote(suppressEnds)`).
- Vibrato effect ±4% / ~6 Hz via LUT, no float in loop → Task 3 (`VIBRATO_LUT`, `applyVibrato`, phase tick).
- `config.h` holds all tunables; `DEBUG` Serial output → Task 1 (`config.h`) + Task 3 (DEBUG block).
- `WIRING.md` + `README.md`; `stl/` untouched → Tasks 4, 5 (no task touches `stl/`).
- Audio jack deferred to v2 → out of scope here (spec appendix); no task.
- Verification = compile + manual smoke test → compile step in every code task + Task 6.

**Placeholder scan:** No TBD/TODO/"handle edge cases"; every code step shows complete code; every command shows expected output.

**Type consistency:** `keyHeld`, `pressOrder`, `pressCounter`, `lastFreqWritten`, `vibratoOn`, `vibratoPhase`, `VIBRATO_LUT`, `VIBRATO_STEPS` declared once and used consistently. `activeNote(bool)` signature is identical in Task 2 and Task 3 (Task 2 calls it with `false`; Task 3 with `suppressEnds`). `noteFrequency(int, uint8_t)` and `readOctaveBand()` unchanged between tasks. `config.h` symbols (`KEY_PINS`, `BUZZER_PIN`, `KNOB_PIN`, `NOTE_HZ`, `OCTAVE_BANDS`, `DEBOUNCE_MS`, `COMBO_HOLD_MS`, `VIBRATO_DEPTH_PERMILLE`, `VIBRATO_UPDATE_MS`, `DEBUG`) all referenced with matching names/types.
