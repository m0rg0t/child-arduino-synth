#include "config.h"

// ---- Per-key debounce + press-order state ----
bool     keyHeld[7]      = { false, false, false, false, false, false, false };
bool     lastReading[7]  = { false, false, false, false, false, false, false };
uint32_t lastChangeMs[7] = { 0, 0, 0, 0, 0, 0, 0 };
uint32_t pressOrder[7]   = { 0, 0, 0, 0, 0, 0, 0 };  // 0 = not held; larger = newer
uint32_t pressCounter    = 1;  // 1-based; 0 stays reserved as the "not held" sentinel

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
