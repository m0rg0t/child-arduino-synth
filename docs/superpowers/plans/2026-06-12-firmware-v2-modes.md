# Firmware v2 Play Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add FX, Song, and Echo modes plus a pentatonic toggle to the 7-key buzzer piano, switched by two-key hold combos.

**Architecture:** A `Mode` enum picks one of four exclusive engines (piano / FX sweep / song player / echo recorder); every engine produces one target frequency (or −1) per `loop()` tick, feeding the existing `lastFreqWritten` dirty-flag `tone()` write. Vibrato and pentatonic are orthogonal toggles applied on that shared path. The v1 single combo generalizes to a table of three `{keyA, keyB, callback}` gestures.

**Tech Stack:** Arduino (ATmega328P, `arduino:avr:nano`), `arduino-cli`. Two-file convention: `config.h` = tunables/data, `child_buzzer.ino` = logic. Song/pointer data in `PROGMEM`.

**Spec:** `docs/superpowers/specs/2026-06-12-firmware-v2-modes-design.md`

**Verification model:** This project has NO automated test harness (per CLAUDE.md). Each task's verify step is:
`arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer` → must end with `Sketch uses N bytes` and zero errors. Hardware smoke checks are listed per task and in the final checklist; run them when a board is attached, otherwise rely on the compile gate and code review.

---

### Task 1: Pentatonic table + table-driven combos + suppression mask

**Files:**
- Modify: `firmware/child_buzzer/config.h`
- Modify: `firmware/child_buzzer/child_buzzer.ino`

- [ ] **Step 1: Add pentatonic table and combo key pairs to config.h**

In `config.h`, after the `NOTE_HZ` / `OCTAVE_BANDS` block, add:

```c
// Alternate 7-key mapping: C-major pentatonic spilling into the next octave,
// so random mashing always sounds consonant. Index 0..6 = C D E G A C' D'.
const uint16_t NOTE_HZ_PENTA[7] = { 262, 294, 330, 392, 440, 523, 587 };
```

In the `// ---- Timing ----` section, after `COMBO_HOLD_MS`, add:

```c
// Two-key hold gestures (hold COMBO_HOLD_MS to fire once per hold).
// Symmetric pairs are the easiest two-finger spans for small hands.
const uint8_t COMBO_VIBRATO_A = 0, COMBO_VIBRATO_B = 6;  // ends: toggle vibrato
const uint8_t COMBO_PENTA_A   = 1, COMBO_PENTA_B   = 5;  // toggle pentatonic
```

- [ ] **Step 2: Replace the single-purpose combo with the table-driven detector in child_buzzer.ino**

Delete the v1 block (the three `comboActive` / `comboFired` / `comboStartMs` globals **and** the whole `updateComboToggle()` function). Add a `pentatonicOn` flag next to `vibratoOn`:

```c
bool     pentatonicOn   = false;
```

Replace the v1 `chirp()` function with a parameterized version plus the two toggle callbacks:

```c
// Two quick beeps as toggle feedback. Briefly blocking (~220 ms) — fine for a
// toy; a combo is being held so no note should be sounding anyway.
void chirp(uint16_t hz1, uint16_t hz2) {
  tone(BUZZER_PIN, hz1); delay(70);
  noTone(BUZZER_PIN);    delay(40);
  tone(BUZZER_PIN, hz2); delay(70);
  noTone(BUZZER_PIN);    delay(40);
  lastFreqWritten = -1;              // force the next loop to re-assert the tone
}

void toggleVibrato()    { vibratoOn    = !vibratoOn;    chirp(1568, 2093); }  // rising
void togglePentatonic() { pentatonicOn = !pentatonicOn; chirp(2093, 1568); }  // falling
```

Below the callbacks, add the combo table and detector (replaces `updateComboToggle`):

```c
// ---- Combo gestures (hold two keys COMBO_HOLD_MS to fire once per hold) ----
struct ComboState {
  uint8_t keyA, keyB;
  void (*onFire)();
  bool active;        // both keys currently held (candidate window)
  bool fired;         // already fired during this hold
  uint32_t startMs;
};

const uint8_t COMBO_COUNT = 2;
ComboState combos[COMBO_COUNT] = {
  { COMBO_VIBRATO_A, COMBO_VIBRATO_B, toggleVibrato,    false, false, 0 },
  { COMBO_PENTA_A,   COMBO_PENTA_B,   togglePentatonic, false, false, 0 },
};

void updateCombos(uint32_t now) {
  for (uint8_t c = 0; c < COMBO_COUNT; c++) {
    ComboState &s = combos[c];
    bool both = keyHeld[s.keyA] && keyHeld[s.keyB];
    if (both) {
      if (!s.active) {
        s.active = true;
        s.fired = false;
        s.startMs = now;
      } else if (!s.fired && now - s.startMs >= COMBO_HOLD_MS) {
        s.fired = true;
        s.onFire();
      }
    } else {
      s.active = false;
      s.fired = false;
    }
  }
}

// Bitmask of keys to mute because they are part of a currently-held combo.
uint8_t comboSuppressMask() {
  uint8_t mask = 0;
  for (uint8_t c = 0; c < COMBO_COUNT; c++) {
    if (keyHeld[combos[c].keyA] && keyHeld[combos[c].keyB]) {
      mask |= (uint8_t)((1 << combos[c].keyA) | (1 << combos[c].keyB));
    }
  }
  return mask;
}
```

- [ ] **Step 3: Switch activeNote() to a suppression mask and noteFrequency() to table select**

Replace `activeNote(bool suppressEnds)` with:

```c
// Most-recently-pressed key still held (0..6), or -1 if none.
// Keys whose bit is set in suppressMask are ignored (held combo pairs).
int activeNote(uint8_t suppressMask) {
  int best = -1;
  uint32_t bestOrder = 0;
  for (uint8_t i = 0; i < 7; i++) {
    if (!keyHeld[i]) continue;
    if ((suppressMask >> i) & 1) continue;
    if (pressOrder[i] > bestOrder) {
      bestOrder = pressOrder[i];
      best = i;
    }
  }
  return best;
}
```

Replace `noteFrequency()` with:

```c
// Frequency for a note index at an octave band (doubling per band; exact).
// The pentatonic toggle swaps which 7-entry table the keys map to.
uint16_t noteFrequency(int noteIndex, uint8_t band) {
  const uint16_t *table = pentatonicOn ? NOTE_HZ_PENTA : NOTE_HZ;
  return (uint16_t)(table[noteIndex] << band);
}
```

- [ ] **Step 4: Update loop() and the DEBUG print**

In `loop()`, replace:

```c
  updateComboToggle(now);
```
with:
```c
  updateCombos(now);
```

and replace:

```c
  bool suppressEnds = (keyHeld[0] && keyHeld[6]);
  int note = activeNote(suppressEnds);
```
with:
```c
  uint8_t suppress = comboSuppressMask();
  int note = activeNote(suppress);
```

In the `#if DEBUG` block, extend the line with the pentatonic flag (before the `vib` print):

```c
    Serial.print(F(" penta=")); Serial.print(pentatonicOn ? 1 : 0);
```

- [ ] **Step 5: Compile**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer`
Expected: success, `Sketch uses ... bytes`. Fix any errors before committing.

- [ ] **Step 6: Commit**

```bash
git add firmware/child_buzzer/config.h firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): table-driven combos + pentatonic scale toggle (keys 1+5)"
```

Hardware smoke check (when board attached): hold keys 1+5 → falling chirp, keys now play pentatonic; hold again → rising? No — falling chirp both ways (it signals "pentatonic toggled"); keys return to major. Vibrato combo 0+6 unchanged.

---

### Task 2: Mode state machine + mode-cycle combo (keys 2+4)

**Files:**
- Modify: `firmware/child_buzzer/config.h`
- Modify: `firmware/child_buzzer/child_buzzer.ino`

- [ ] **Step 1: Add mode tunables to config.h**

After the combo-pair constants, add:

```c
const uint8_t COMBO_MODE_A = 2, COMBO_MODE_B = 4;        // cycle play mode

// ---- Modes ----
// Entering mode k is announced with k+1 beeps (1 = piano .. 4 = echo).
const uint16_t MODE_BEEP_HZ     = 1047;  // C6
const uint8_t  MODE_BEEP_MS     = 60;
const uint8_t  MODE_BEEP_GAP_MS = 70;
```

- [ ] **Step 2: Add the Mode enum, cycle handler, and engine reset to child_buzzer.ino**

Near the top (after the pentatonic flag), add:

```c
// ---- Play mode ----
enum Mode : uint8_t { MODE_PIANO = 0, MODE_FX, MODE_SONG, MODE_ECHO, MODE_COUNT };
uint8_t  mode = MODE_PIANO;
```

After `togglePentatonic()`, add:

```c
// Stop all mode engines and drop transient state. Extended as engines land
// (FX in Task 3, song in Task 4, echo in Task 5).
void resetEngines() {
}

// k+1 announce beeps. Blocking (<= ~520 ms) like chirp(); acceptable for a toy.
void announceMode() {
  for (uint8_t i = 0; i <= mode; i++) {
    tone(BUZZER_PIN, MODE_BEEP_HZ); delay(MODE_BEEP_MS);
    noTone(BUZZER_PIN);             delay(MODE_BEEP_GAP_MS);
  }
  lastFreqWritten = -1;
}

void cycleMode() {
  mode = (uint8_t)((mode + 1) % MODE_COUNT);
  resetEngines();
  announceMode();
}
```

Extend the combo table to three entries:

```c
const uint8_t COMBO_COUNT = 3;
ComboState combos[COMBO_COUNT] = {
  { COMBO_VIBRATO_A, COMBO_VIBRATO_B, toggleVibrato,    false, false, 0 },
  { COMBO_PENTA_A,   COMBO_PENTA_B,   togglePentatonic, false, false, 0 },
  { COMBO_MODE_A,    COMBO_MODE_B,    cycleMode,        false, false, 0 },
};
```

- [ ] **Step 3: Restructure loop() into the mode dispatch**

Replace the frequency-computation part of `loop()` (everything between `updateCombos(now);` and the `freq != lastFreqWritten` write, exclusive) with:

```c
  uint8_t suppress = comboSuppressMask();

  // Advance the vibrato wobble on its own tick.
  if (vibratoOn && (now - lastVibratoMs >= VIBRATO_UPDATE_MS)) {
    lastVibratoMs = now;
    vibratoPhase = (vibratoPhase + 1) % VIBRATO_STEPS;
  }

  int note = activeNote(suppress);
  uint8_t band = readOctaveBand();

  // Each mode engine answers one question: what frequency right now (-1 = silence)?
  int freq = -1;
  switch (mode) {
    case MODE_PIANO:
      if (note >= 0) freq = noteFrequency(note, band);
      break;
    case MODE_FX:    // engine lands in Task 3
    case MODE_SONG:  // engine lands in Task 4
    case MODE_ECHO:  // engine lands in Task 5
    default:
      break;
  }

  // Vibrato layers over every mode's output.
  if (freq > 0 && vibratoOn) freq = applyVibrato((uint16_t)freq);
```

(The existing `if (freq != lastFreqWritten) { ... }` write stays unchanged below this.)

- [ ] **Step 4: Add mode to the DEBUG print**

In the `#if DEBUG` block, add at the start of the printed line:

```c
    Serial.print(F("mode=")); Serial.print(mode);
    Serial.print(F(" "));
```

- [ ] **Step 5: Compile**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add firmware/child_buzzer/config.h firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): mode state machine + 2+4 mode-cycle combo with beep announce"
```

Hardware smoke check: hold 2+4 → 2 beeps, keys silent (FX stub); cycle 3 more times → 3 beeps, 4 beeps, 1 beep back to piano.

---

### Task 3: FX mode (generic non-blocking sweep engine)

**Files:**
- Modify: `firmware/child_buzzer/config.h`
- Modify: `firmware/child_buzzer/child_buzzer.ino`

- [ ] **Step 1: Add FX definitions to config.h**

After the mode-beep constants, add:

```c
// ---- FX mode ----
// Every effect is one parameter row driven by a shared sweep engine:
//   FX_ONCE     sweep start->end once, then silence until re-press
//   FX_LOOP     sweep start->end, snap back to start, repeat
//   FX_PINGPONG bounce between start and end
//   FX_RETRIG   like FX_ONCE but restarts after FX_RETRIG_GAP_MS while held
enum FxBehavior : uint8_t { FX_ONCE, FX_LOOP, FX_PINGPONG, FX_RETRIG };

struct FxDef {
  uint16_t startHz, endHz;
  uint16_t stepHz;     // size of each pitch step
  uint8_t  stepMs;     // time between steps
  uint8_t  behavior;   // FxBehavior
};

const uint16_t FX_RETRIG_GAP_MS = 180;  // pause between bird chirps

const FxDef FX_DEFS[7] = {
  {  600, 1200,    8,  10, FX_PINGPONG },  // key 0: siren — slow rise/fall wail
  { 2500,  300,  110,   5, FX_ONCE     },  // key 1: laser zap — fast dive
  {  250, 1800,   20,   8, FX_ONCE     },  // key 2: slide whistle — long rise
  {  880, 1100,   44,   9, FX_PINGPONG },  // key 3: UFO warble — narrow fast wobble
  {  800, 1000,  200, 250, FX_PINGPONG },  // key 4: alarm — hard two-tone
  { 2000, 3200,  150,   6, FX_RETRIG   },  // key 5: bird — short rising chirps
  {  400, 2200,  450,  35, FX_LOOP     },  // key 6: robot babble — coarse climbing steps
};
```

- [ ] **Step 2: Add the FX engine to child_buzzer.ino**

After `applyVibrato()`, add:

```c
// ---- FX engine (MODE_FX) ----
int8_t   fxKey = -1;       // key whose effect is running; -1 = idle
uint16_t fxHz = 0;
int8_t   fxDir = 1;        // +1 stepping up, -1 stepping down
bool     fxDone = false;   // FX_ONCE finished / FX_RETRIG waiting out its gap
uint32_t fxNextMs = 0;     // next step time; retrigger time while fxDone

void fxStart(uint8_t key, uint32_t now) {
  fxKey  = (int8_t)key;
  fxHz   = FX_DEFS[key].startHz;
  fxDir  = (FX_DEFS[key].endHz >= FX_DEFS[key].startHz) ? 1 : -1;
  fxDone = false;
  fxNextMs = now + FX_DEFS[key].stepMs;
}

// FX frequency for this tick (-1 = silence). activeKey is last-pressed-wins.
int fxTick(int activeKey, uint32_t now) {
  if (activeKey < 0) {       // releasing silences immediately
    fxKey = -1;
    return -1;
  }
  if (activeKey != fxKey) fxStart((uint8_t)activeKey, now);

  const FxDef &d = FX_DEFS[fxKey];
  if (fxDone) {
    if (d.behavior == FX_RETRIG && now >= fxNextMs) {
      fxStart((uint8_t)fxKey, now);
    } else {
      return -1;
    }
  }

  if (now >= fxNextMs) {
    fxNextMs = now + d.stepMs;
    int32_t lo = min(d.startHz, d.endHz);
    int32_t hi = max(d.startHz, d.endHz);
    int32_t next = (int32_t)fxHz + (int32_t)fxDir * (int32_t)d.stepHz;
    if (next < lo || next > hi) {
      switch (d.behavior) {
        case FX_ONCE:
        case FX_RETRIG:
          fxHz = d.endHz;
          fxDone = true;
          fxNextMs = now + FX_RETRIG_GAP_MS;  // only consulted by FX_RETRIG
          break;
        case FX_LOOP:
          fxHz = d.startHz;
          break;
        case FX_PINGPONG:
          fxDir = (int8_t)-fxDir;
          next = (int32_t)fxHz + (int32_t)fxDir * (int32_t)d.stepHz;
          fxHz = (uint16_t)constrain(next, lo, hi);
          break;
      }
    } else {
      fxHz = (uint16_t)next;
    }
  }
  return (int)fxHz;
}
```

- [ ] **Step 3: Wire FX into resetEngines() and the loop dispatch**

In `resetEngines()`, add:

```c
  fxKey = -1;
  fxDone = false;
```

In the `loop()` switch, replace the `case MODE_FX:` stub line with:

```c
    case MODE_FX:
      freq = fxTick(note, now);
      break;
```

(Keep the `MODE_SONG` / `MODE_ECHO` stub cases.)

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer`
Expected: success.

- [ ] **Step 5: Commit**

```bash
git add firmware/child_buzzer/config.h firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): FX mode — 7 effects on a shared non-blocking sweep engine"
```

Hardware smoke check: cycle to FX (2 beeps). Key 0 wails like a siren while held; key 1 zaps once per press; key 5 chirps repeatedly while held; releasing any key silences instantly.

---

### Task 4: Song mode (PROGMEM melodies, knob = tempo)

**Files:**
- Modify: `firmware/child_buzzer/config.h`
- Modify: `firmware/child_buzzer/child_buzzer.ino`

- [ ] **Step 1: Add song encoding, tunables, and the seven melodies to config.h**

After the FX block, add:

```c
// ---- Song mode ----
// A song is a PROGMEM byte stream of {note, duration} pairs.
//   note byte: low 3 bits = scale degree 0..6 into NOTE_HZ (always the major
//              table), bits 3-4 = absolute octave band 0..3.
//   duration:  length in ticks; the knob scales tick length (tempo).
// Special note bytes: rest, low stomp ("boom"), short high hit ("cha").
#define SN(deg, oct) ((uint8_t)((deg) | ((oct) << 3)))
const uint8_t  SONG_REST = 0x7F;
const uint8_t  SONG_THUD = 0x7E;
const uint8_t  SONG_CLAP = 0x7D;
const uint16_t SONG_THUD_HZ = 90;
const uint16_t SONG_CLAP_HZ = 1568;

const uint16_t SONG_TICK_MS_SLOW = 350;  // knob fully left
const uint16_t SONG_TICK_MS_FAST = 110;  // knob fully right
const uint8_t  SONG_GAP_MS = 25;         // articulation gap so repeated notes don't slur

// Key 0: Twinkle Twinkle Little Star (first verse)
const uint8_t SONG_TWINKLE[] PROGMEM = {
  SN(0,1),2, SN(0,1),2, SN(4,1),2, SN(4,1),2, SN(5,1),2, SN(5,1),2, SN(4,1),4,
  SN(3,1),2, SN(3,1),2, SN(2,1),2, SN(2,1),2, SN(1,1),2, SN(1,1),2, SN(0,1),4,
};

// Key 1: Mary Had a Little Lamb
const uint8_t SONG_MARY[] PROGMEM = {
  SN(2,1),2, SN(1,1),2, SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(2,1),2, SN(2,1),4,
  SN(1,1),2, SN(1,1),2, SN(1,1),4, SN(2,1),2, SN(4,1),2, SN(4,1),4,
  SN(2,1),2, SN(1,1),2, SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(2,1),2, SN(2,1),2, SN(2,1),2,
  SN(1,1),2, SN(1,1),2, SN(2,1),2, SN(1,1),2, SN(0,1),8,
};

// Key 2: Ode to Joy (main theme)
const uint8_t SONG_ODE[] PROGMEM = {
  SN(2,1),2, SN(2,1),2, SN(3,1),2, SN(4,1),2, SN(4,1),2, SN(3,1),2, SN(2,1),2, SN(1,1),2,
  SN(0,1),2, SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(2,1),3, SN(1,1),1, SN(1,1),4,
  SN(2,1),2, SN(2,1),2, SN(3,1),2, SN(4,1),2, SN(4,1),2, SN(3,1),2, SN(2,1),2, SN(1,1),2,
  SN(0,1),2, SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(1,1),3, SN(0,1),1, SN(0,1),4,
};

// Key 3: London Bridge
const uint8_t SONG_LONDON[] PROGMEM = {
  SN(4,1),3, SN(5,1),1, SN(4,1),2, SN(3,1),2, SN(2,1),2, SN(3,1),2, SN(4,1),4,
  SN(1,1),2, SN(2,1),2, SN(3,1),4, SN(2,1),2, SN(3,1),2, SN(4,1),4,
  SN(4,1),3, SN(5,1),1, SN(4,1),2, SN(3,1),2, SN(2,1),2, SN(3,1),2, SN(4,1),4,
  SN(1,1),4, SN(4,1),4, SN(2,1),2, SN(0,1),6,
};

// Key 4: Frere Jacques
const uint8_t SONG_FRERE[] PROGMEM = {
  SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(0,1),2,  SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(0,1),2,
  SN(2,1),2, SN(3,1),2, SN(4,1),4,             SN(2,1),2, SN(3,1),2, SN(4,1),4,
  SN(4,1),1, SN(5,1),1, SN(4,1),1, SN(3,1),1,  SN(2,1),2, SN(0,1),2,
  SN(4,1),1, SN(5,1),1, SN(4,1),1, SN(3,1),1,  SN(2,1),2, SN(0,1),2,
  SN(0,1),2, SN(4,0),2, SN(0,1),4,             SN(0,1),2, SN(4,0),2, SN(0,1),4,
};

// Key 5: classic hard-rock four-note riff, transposed to B so the 0/+3/+5/+6
// semitone shape lands entirely on white keys: B-D-E | B-D-F-E | B-D-E | D-B)
const uint8_t SONG_RIFF[] PROGMEM = {
  SN(6,0),2, SN(1,1),2, SN(2,1),3, SONG_REST,1,
  SN(6,0),2, SN(1,1),2, SN(3,1),1, SN(2,1),4, SONG_REST,2,
  SN(6,0),2, SN(1,1),2, SN(2,1),3, SONG_REST,1,
  SN(1,1),2, SN(6,0),6,
};

// Key 6: stadium stomp-stomp-clap + chant hook
const uint8_t SONG_STOMP[] PROGMEM = {
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
  SN(2,1),2, SN(2,1),2, SN(1,1),2, SN(1,1),2, SN(0,1),3, SONG_REST,1, SN(0,1),4,
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
};

const uint8_t* const SONGS[7] PROGMEM = {
  SONG_TWINKLE, SONG_MARY, SONG_ODE, SONG_LONDON, SONG_FRERE, SONG_RIFF, SONG_STOMP,
};
const uint8_t SONG_LEN[7] = {  // number of {note, duration} pairs per song
  sizeof(SONG_TWINKLE) / 2, sizeof(SONG_MARY) / 2,  sizeof(SONG_ODE) / 2,
  sizeof(SONG_LONDON) / 2,  sizeof(SONG_FRERE) / 2, sizeof(SONG_RIFF) / 2,
  sizeof(SONG_STOMP) / 2,
};
```

- [ ] **Step 2: Add the song player to child_buzzer.ino**

After the FX engine, add:

```c
// ---- Song player (MODE_SONG) ----
int8_t   songKey = -1;          // -1 = stopped
uint8_t  songPos = 0;           // current {note,duration} pair
bool     songInGap = false;     // articulation gap at the end of a note
uint16_t songFreq = 0;          // 0 = rest
uint32_t songPhaseEndMs = 0;    // end of the current sound/gap phase
uint32_t songLastStamp = 0;     // pressCounter baseline for new-press detection

// Newest key pressed since the last call (-1 = none); suppressed keys ignored.
int songTakeNewPress(uint8_t suppressMask) {
  int best = -1;
  uint32_t bestOrder = songLastStamp;
  for (uint8_t i = 0; i < 7; i++) {
    if ((suppressMask >> i) & 1) continue;
    if (keyHeld[i] && pressOrder[i] > bestOrder) {
      bestOrder = pressOrder[i];
      best = i;
    }
  }
  if (best >= 0) songLastStamp = bestOrder;
  return best;
}

// Knob position -> tick length (left = slow, right = fast).
uint16_t songTickMs() {
  int raw = analogRead(KNOB_PIN);  // 0..1023
  return (uint16_t)(SONG_TICK_MS_SLOW
                    - (uint32_t)raw * (SONG_TICK_MS_SLOW - SONG_TICK_MS_FAST) / 1024);
}

uint16_t songNoteHz(uint8_t noteByte) {
  if (noteByte == SONG_REST) return 0;
  if (noteByte == SONG_THUD) return SONG_THUD_HZ;
  if (noteByte == SONG_CLAP) return SONG_CLAP_HZ;
  uint8_t deg = noteByte & 0x07;
  uint8_t oct = (noteByte >> 3) & 0x03;
  return (uint16_t)(NOTE_HZ[deg] << oct);  // songs always use the major table
}

void songLoadEvent(uint32_t now) {
  const uint8_t *song = (const uint8_t *)pgm_read_ptr(&SONGS[songKey]);
  uint8_t noteByte = pgm_read_byte(song + 2 * songPos);
  uint8_t durTicks = pgm_read_byte(song + 2 * songPos + 1);
  songFreq = songNoteHz(noteByte);
  uint32_t durMs = (uint32_t)durTicks * songTickMs();
  songInGap = false;
  songPhaseEndMs = now + (durMs > SONG_GAP_MS ? durMs - SONG_GAP_MS : durMs);
}

void songStart(uint8_t key, uint32_t now) {
  songKey = (int8_t)key;
  songPos = 0;
  songLoadEvent(now);
}

void songStop() {
  songKey = -1;
}

// Song frequency for this tick (-1 = silence). Same key stops, new key switches.
int songTick(uint32_t now, uint8_t suppressMask) {
  int pressed = songTakeNewPress(suppressMask);
  if (pressed >= 0) {
    if (pressed == songKey) songStop();
    else songStart((uint8_t)pressed, now);
  }
  if (songKey < 0) return -1;

  if (now >= songPhaseEndMs) {
    if (!songInGap) {
      songInGap = true;
      songPhaseEndMs = now + SONG_GAP_MS;
    } else {
      songPos++;
      if (songPos >= SONG_LEN[songKey]) {  // played to the end; stop (no loop)
        songStop();
        return -1;
      }
      songLoadEvent(now);
    }
  }
  if (songInGap || songFreq == 0) return -1;
  return (int)songFreq;
}
```

- [ ] **Step 3: Wire the song player into resetEngines() and the loop dispatch**

In `resetEngines()`, add:

```c
  songStop();
  songLastStamp = pressCounter;  // ignore presses from before the mode switch
```

In the `loop()` switch, replace the `case MODE_SONG:` stub line with:

```c
    case MODE_SONG:
      freq = songTick(now, suppress);
      break;
```

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer`
Expected: success. Note the flash usage — songs add ~250 bytes of PROGMEM.

- [ ] **Step 5: Commit**

```bash
git add firmware/child_buzzer/config.h firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): song mode — 7 PROGMEM melodies, knob controls tempo"
```

Hardware smoke check: cycle to song mode (3 beeps). Each key starts its melody; same key stops; different key switches; the knob audibly changes tempo; key 5 is a recognizable hard-rock riff; key 6 does boom-boom-cha.

---

### Task 5: Echo mode (record & parrot)

**Files:**
- Modify: `firmware/child_buzzer/config.h`
- Modify: `firmware/child_buzzer/child_buzzer.ino`

- [ ] **Step 1: Add echo tunables to config.h**

After the song block, add:

```c
// ---- Echo mode ----
const uint8_t  ECHO_MAX_EVENTS = 32;     // ~160 B of SRAM (5 B per event)
const uint16_t ECHO_SILENCE_MS = 1500;   // quiet time before the parrot replies
```

- [ ] **Step 2: Add the echo recorder/player to child_buzzer.ino**

After the song player, add:

```c
// ---- Echo recorder/player (MODE_ECHO) ----
struct EchoEvent {
  uint8_t  noteAndBand;  // low 3 bits = note index, bits 3-4 = octave band
  uint16_t durationMs;
  uint16_t gapMs;        // silence before this note
};

EchoEvent echoBuf[ECHO_MAX_EVENTS];
uint8_t   echoCount = 0;
bool      echoPlaying = false;
uint8_t   echoPos = 0;
bool      echoInGap = false;
uint32_t  echoPhaseEndMs = 0;
int8_t    echoLiveNote = -1;        // note currently sounding while recording
uint8_t   echoLiveBand = 0;
uint16_t  echoPendingGapMs = 0;     // gap captured when the live note opened
uint32_t  echoNoteStartMs = 0;
uint32_t  echoSilenceStartMs = 0;   // when the last note ended

void echoReset() {
  echoCount = 0;
  echoPlaying = false;
  echoLiveNote = -1;
}

void echoOpenNote(int note, uint8_t band, uint32_t now) {
  uint32_t gap = (echoCount == 0) ? 0 : (now - echoSilenceStartMs);
  echoPendingGapMs = (uint16_t)min(gap, (uint32_t)65535);
  echoLiveNote = (int8_t)note;
  echoLiveBand = band;
  echoNoteStartMs = now;
}

void echoCloseNote(uint32_t now) {
  if (echoLiveNote < 0) return;
  if (echoCount < ECHO_MAX_EVENTS) {  // buffer full: drop, keep what fits
    uint32_t dur = now - echoNoteStartMs;
    echoBuf[echoCount].noteAndBand = (uint8_t)(echoLiveNote | (echoLiveBand << 3));
    echoBuf[echoCount].durationMs = (uint16_t)min(dur, (uint32_t)65535);
    echoBuf[echoCount].gapMs = echoPendingGapMs;
    echoCount++;
  }
  echoLiveNote = -1;
  echoSilenceStartMs = now;
}

void echoStartPlayback(uint32_t now) {
  echoPlaying = true;
  echoPos = 0;
  echoInGap = false;
  echoPhaseEndMs = now + echoBuf[0].durationMs;
}

// Echo frequency for this tick (-1 = silence). Records live piano playing;
// after ECHO_SILENCE_MS of quiet it parrots the phrase back, then clears.
int echoTick(int note, uint8_t band, uint32_t now) {
  if (echoPlaying) {
    if (note >= 0) {
      echoReset();  // kid interrupted the parrot; fall through to record
    } else {
      if (now >= echoPhaseEndMs) {
        if (echoInGap) {
          echoInGap = false;
          echoPhaseEndMs = now + echoBuf[echoPos].durationMs;
        } else {
          echoPos++;
          if (echoPos >= echoCount) {
            echoReset();
            return -1;
          }
          echoInGap = true;
          echoPhaseEndMs = now + echoBuf[echoPos].gapMs;
        }
      }
      if (echoInGap) return -1;
      uint8_t nb = echoBuf[echoPos].noteAndBand;
      return (int)noteFrequency(nb & 0x07, (nb >> 3) & 0x03);
    }
  }

  // Recording: live piano behavior, transitions logged.
  if (note != echoLiveNote) {
    if (echoLiveNote >= 0) echoCloseNote(now);
    if (note >= 0) echoOpenNote(note, band, now);
  }
  if (note < 0) {
    if (echoCount > 0 && (now - echoSilenceStartMs >= ECHO_SILENCE_MS)) {
      echoStartPlayback(now);
    }
    return -1;
  }
  return (int)noteFrequency(note, band);
}
```

- [ ] **Step 3: Wire echo into resetEngines() and the loop dispatch**

In `resetEngines()`, add:

```c
  echoReset();
```

In the `loop()` switch, replace the `case MODE_ECHO:` stub (and the now-unneeded `default:` fall-through stub block) with:

```c
    case MODE_ECHO:
      freq = echoTick(note, band, now);
      break;
    default:
      break;
```

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer`
Expected: success. Check the reported "Global variables use N bytes" — should stay well under 50% of 2048 B.

- [ ] **Step 5: Commit**

```bash
git add firmware/child_buzzer/config.h firmware/child_buzzer/child_buzzer.ino
git commit -m "feat(fw): echo mode — record up to 32 notes, parrot back after silence"
```

Hardware smoke check: cycle to echo (4 beeps). Play a short phrase, wait ~1.5 s → it plays back with the same rhythm, then the toy is ready to record again. Playing during the replay interrupts it and starts a new recording.

---

### Task 6: Docs + final verification

**Files:**
- Modify: `README.md` (play instructions)
- Modify: `CLAUDE.md` (architecture summary)

- [ ] **Step 1: Update README.md play instructions**

In the README's play-instructions section, document the new gestures and modes (adapt wording to the existing README voice):

```markdown
## Modes & gestures

Hold two keys for ~0.6 s to trigger a gesture (the held keys stay silent):

| Keys | Gesture | Feedback |
|------|---------|----------|
| 1 + 7 (ends) | toggle vibrato | rising chirp |
| 2 + 6 | toggle pentatonic scale | falling chirp |
| 3 + 5 | next mode | 1–4 beeps = current mode |

Modes (announced by beep count):

1. **Piano** — the classic 7-key C-major piano; knob = octave.
2. **FX** — each key is a sound effect: siren, laser, slide whistle, UFO,
   alarm, bird, robot.
3. **Songs** — each key plays a melody (Twinkle Twinkle, Mary Had a Little
   Lamb, Ode to Joy, London Bridge, Frère Jacques, a hard-rock riff,
   a stadium stomp-and-chant); knob = tempo; same key stops.
4. **Echo** — play something, pause 1.5 s, and the toy parrots it back.

Vibrato and pentatonic work in every mode.
```

(Note: README speaks in 1-based key numbers for humans; code is 0-based.)

- [ ] **Step 2: Update CLAUDE.md architecture section**

In CLAUDE.md's Architecture section, update the combo bullet to mention the three table-driven combos (0+6 vibrato, 1+5 pentatonic, 2+4 mode cycle) and add one bullet:

```markdown
- **Four exclusive modes** (`Mode` enum: piano / FX / song / echo) cycled by the
  2+4 combo with beep-count announce. Every mode engine returns one frequency
  (or -1) per tick into the shared `lastFreqWritten` dirty-flag `tone()` write;
  vibrato and the pentatonic table-swap layer on top in any mode.
```

- [ ] **Step 3: Final compile + memory check**

Run: `arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer`
Expected: success; flash < 80% of 30720 B, globals < 50% of 2048 B. Record the numbers in the commit message.

- [ ] **Step 4: Run the full smoke-test checklist (hardware attached)**

From the spec (`docs/superpowers/specs/2026-06-12-firmware-v2-modes-design.md`, Verification section): items 1–8. If no board is attached, note that flashing + smoke test is pending.

- [ ] **Step 5: Commit**

```bash
git add README.md CLAUDE.md
git commit -m "docs: document v2 modes, combos, and smoke-test results"
```
