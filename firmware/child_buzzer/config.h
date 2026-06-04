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
