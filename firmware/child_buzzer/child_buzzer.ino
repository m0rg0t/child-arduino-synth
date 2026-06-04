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
