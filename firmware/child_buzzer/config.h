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

// Alternate 7-key mapping: C-major pentatonic spilling into the next octave,
// so random mashing always sounds consonant. Index 0..6 = C D E G A C' D'.
const uint16_t NOTE_HZ_PENTA[7] = { 262, 294, 330, 392, 440, 523, 587 };

// ---- Timing ----
const uint16_t DEBOUNCE_MS   = 5;    // per-key debounce window
const uint16_t COMBO_HOLD_MS = 600;  // hold a combo key pair this long to fire its gesture

// Two-key hold gestures (hold COMBO_HOLD_MS to fire once per hold).
// Symmetric pairs are the easiest two-finger spans for small hands.
const uint8_t COMBO_VIBRATO_A = 0, COMBO_VIBRATO_B = 6;  // ends: toggle vibrato
const uint8_t COMBO_PENTA_A   = 1, COMBO_PENTA_B   = 5;  // toggle pentatonic
const uint8_t COMBO_MODE_A    = 2, COMBO_MODE_B    = 4;  // cycle play mode

// ---- Modes ----
// Entering mode k is announced with k+1 beeps (1 = piano .. 4 = echo).
const uint16_t MODE_BEEP_HZ     = 1047;  // C6
const uint8_t  MODE_BEEP_MS     = 60;
const uint8_t  MODE_BEEP_GAP_MS = 70;

// Toggle-feedback chirps (vibrato = rising, pentatonic = falling).
const uint16_t CHIRP_LO_HZ  = 1568;  // ~G6
const uint16_t CHIRP_HI_HZ  = 2093;  // ~C7
const uint8_t  CHIRP_TONE_MS = 70;
const uint8_t  CHIRP_GAP_MS  = 40;

// ---- Vibrato ----
const uint16_t VIBRATO_DEPTH_PERMILLE = 40;  // +/-4.0% pitch wobble
const uint16_t VIBRATO_UPDATE_MS      = 10;  // how often to nudge the pitch
// VIBRATO_STEPS * VIBRATO_UPDATE_MS = one wobble cycle (16 * 10 ms ~= 6.25 Hz).
// VIBRATO_LUT is one sine cycle, scaled to +/-1000 (the wobble shape).
const uint8_t VIBRATO_STEPS = 16;
const int16_t VIBRATO_LUT[VIBRATO_STEPS] = {
     0,   383,   707,   924,  1000,   924,   707,   383,
     0,  -383,  -707,  -924, -1000,  -924,  -707,  -383
};

// ---- Debug ----
#define DEBUG 1   // set to 1 to print state over Serial @115200

#endif // CONFIG_H
