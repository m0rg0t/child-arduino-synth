#include "config.h"

// ---- Per-key debounce + press-order state ----
bool     keyHeld[7]      = { false, false, false, false, false, false, false };
bool     lastReading[7]  = { false, false, false, false, false, false, false };
uint32_t lastChangeMs[7] = { 0, 0, 0, 0, 0, 0, 0 };
uint32_t pressOrder[7]   = { 0, 0, 0, 0, 0, 0, 0 };  // 0 = not held; larger = newer
uint32_t pressCounter    = 1;  // 1-based; 0 stays reserved as the "not held" sentinel

int      lastFreqWritten = -1;  // -1 means noTone() is currently active

// ---- Vibrato runtime state (the shape/size live in config.h) ----
bool     vibratoOn      = false;
bool     pentatonicOn   = false;
uint8_t  vibratoPhase   = 0;
uint32_t lastVibratoMs  = 0;

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

// Frequency for a note index at an octave band (doubling per band; exact).
// The pentatonic toggle swaps which 7-entry table the keys map to.
uint16_t noteFrequency(int noteIndex, uint8_t band) {
  const uint16_t *table = pentatonicOn ? NOTE_HZ_PENTA : NOTE_HZ;
  return (uint16_t)(table[noteIndex] << band);
}

// Knob (0..1023) -> octave band 0..OCTAVE_BANDS-1.
uint8_t readOctaveBand() {
  int raw = analogRead(KNOB_PIN);
  uint8_t band = (uint8_t)((uint32_t)raw * OCTAVE_BANDS / 1024);
  // analogRead returns 0..1023, so band is at most 3; this clamp is a guard for
  // any platform whose ADC could report a full-scale 1024.
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

void loop() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < 7; i++) {
    debounceKey(i, now);
  }

  updateCombos(now);

  // Advance the vibrato wobble on its own tick.
  if (vibratoOn && (now - lastVibratoMs >= VIBRATO_UPDATE_MS)) {
    lastVibratoMs = now;
    vibratoPhase = (vibratoPhase + 1) % VIBRATO_STEPS;
  }

  uint8_t suppress = comboSuppressMask();
  int note = activeNote(suppress);
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
    Serial.print(F(" penta=")); Serial.print(pentatonicOn ? 1 : 0);
    Serial.print(F(" vib=")); Serial.println(vibratoOn ? 1 : 0);
  }
#endif
}
