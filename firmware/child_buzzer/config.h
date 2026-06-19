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

// ---- FX mode ----
// Every effect is one parameter row driven by a shared sweep engine:
//   FX_ONCE     sweep start->end once, then silence until re-press
//   FX_LOOP     sweep start->end, snap back to start, repeat
//   FX_PINGPONG bounce between start and end
//   FX_RETRIG   like FX_ONCE but restarts after FX_RETRIG_GAP_MS while held
enum FxBehavior : uint8_t { FX_ONCE, FX_LOOP, FX_PINGPONG, FX_RETRIG };

struct FxDef {
  uint16_t startHz, endHz;
  uint16_t stepHz;     // size of each pitch step (must be >= 1)
  uint8_t  stepMs;     // time between steps
  uint8_t  behavior;   // FxBehavior
};

const uint16_t FX_RETRIG_GAP_MS = 180;  // pause between bird chirps

// Kept in SRAM (56 B) for simple access; bulky song tables go in PROGMEM instead.
const FxDef FX_DEFS[7] = {
  {  600, 1200,    8,  10, FX_PINGPONG },  // key 0: siren — slow rise/fall wail
  { 2500,  300,  110,   5, FX_ONCE     },  // key 1: laser zap — fast dive
  {  250, 1800,   20,   8, FX_ONCE     },  // key 2: slide whistle — long rise
  {  880, 1100,   44,   9, FX_PINGPONG },  // key 3: UFO warble — narrow fast wobble
  {  800, 1000,  200, 250, FX_PINGPONG },  // key 4: alarm — hard two-tone
  { 2000, 3200,  150,   6, FX_RETRIG   },  // key 5: bird — short rising chirps
  {  400, 2200,  450,  35, FX_LOOP     },  // key 6: robot babble — coarse climbing steps
};

// ---- Song mode ----
// A song is a PROGMEM byte stream of {note, duration} pairs.
//   note byte: low 3 bits = scale degree 0..6 into NOTE_HZ (always the major
//              table), bits 3-4 = absolute octave band 0..3.
//   duration:  length in ticks; the knob scales tick length (tempo) (must be >= 1 tick; events shorter than SONG_GAP_MS stretch by the gap)
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

// Key 3: Let It Go (Frozen) — chorus hook only. Monophonic white-key reduction
// in C major. The signature rise: the held syllable leaps up to C6 (SN(0,2)),
// then the figure shifts up a step so the next peak is D6 (SN(1,2)). Resolves
// on the tonic C. See docs/superpowers/specs/2026-06-18-letitgo-song-design.md.
const uint8_t SONG_LETITGO_HOOK[] PROGMEM = {
  SN(4,1),2, SN(4,1),2, SN(0,2),6,  SN(5,1),2, SN(5,1),2, SN(1,2),6,        // rising "let it go" pair
  SN(0,2),2, SN(6,1),2, SN(5,1),2, SN(4,1),2, SN(5,1),4, SONG_REST,2,       // answer phrase, descending
  SN(4,1),2, SN(4,1),2, SN(2,1),2, SN(1,1),2, SN(0,1),8,                    // closing line, resolves to C
};

// Key 4: Let It Go (Frozen) — full arrangement: verse -> pre-chorus -> chorus,
// played once. Verse anchored in A natural minor, chorus in its relative
// C major; both sit on white keys so the whole tune needs no accidentals (the
// format has none). Verse low (band 1), chorus lifts to bands 1-2.
const uint8_t SONG_LETITGO[] PROGMEM = {
  // verse
  SN(2,1),4, SN(2,1),2, SN(3,1),2, SN(2,1),2, SN(1,1),2, SN(0,1),4,        // A-minor phrase 1
  SN(1,1),2, SN(1,1),2, SN(2,1),2, SN(3,1),2, SN(2,1),6,                   // phrase 2
  SN(2,1),4, SN(2,1),2, SN(3,1),2, SN(4,1),2, SN(2,1),2, SN(1,1),4,        // phrase 3 (reaches up to G)
  SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(1,1),2, SN(5,0),6,                   // phrase 4, settles low on A
  // pre-chorus (rising build into the chorus register)
  SN(5,0),2, SN(0,1),2, SN(1,1),2, SN(2,1),2, SN(3,1),4,
  SN(2,1),2, SN(3,1),2, SN(4,1),2, SN(5,1),2, SN(4,1),4,
  SN(4,1),2, SN(5,1),2, SN(6,1),2, SN(0,2),4, SONG_REST,2,                 // lift onto high C
  // chorus
  SN(4,1),2, SN(4,1),2, SN(0,2),6,  SN(5,1),2, SN(5,1),2, SN(1,2),6,       // rising "let it go" pair
  SN(0,2),2, SN(6,1),2, SN(5,1),2, SN(4,1),2, SN(5,1),4, SONG_REST,2,      // answer 1, descending
  SN(4,1),2, SN(4,1),2, SN(0,2),6,  SN(5,1),2, SN(5,1),2, SN(1,2),6,       // rising pair again
  SN(0,2),2, SN(6,1),2, SN(5,1),2, SN(4,1),2, SN(0,1),4, SONG_REST,2,      // answer 2
  SN(4,1),2, SN(5,1),2, SN(6,1),2, SN(0,2),2, SN(6,1),2, SN(5,1),2, SN(4,1),4,  // bridge-up line
  SN(5,1),2, SN(4,1),2, SN(2,1),4, SONG_REST,2,                            // short tag
  SN(4,1),2, SN(4,1),2, SN(2,1),2, SN(1,1),2, SN(0,1),8,                   // closing line, resolves to C
};

// Key 5: Smoke on the Water (main riff, transposed to B so the 0/+3/+5/+6
// semitone shape lands entirely on white keys: B-D-E | B-D-F-E | B-D-E | D-B)
const uint8_t SONG_SMOKE[] PROGMEM = {
  SN(6,0),2, SN(1,1),2, SN(2,1),3, SONG_REST,1,
  SN(6,0),2, SN(1,1),2, SN(3,1),1, SN(2,1),4, SONG_REST,2,
  SN(6,0),2, SN(1,1),2, SN(2,1),3, SONG_REST,1,
  SN(1,1),2, SN(6,0),6,
};

// Key 6: We Will Rock You (stomp-stomp-clap + chant hook)
const uint8_t SONG_ROCKYOU[] PROGMEM = {
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
  SN(2,1),2, SN(2,1),2, SN(1,1),2, SN(1,1),2, SN(0,1),3, SONG_REST,1, SN(0,1),4,
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
  SONG_THUD,2, SONG_THUD,2, SONG_CLAP,2, SONG_REST,2,
};

const uint8_t* const SONGS[7] PROGMEM = {
  SONG_TWINKLE, SONG_MARY, SONG_ODE, SONG_LETITGO_HOOK, SONG_LETITGO, SONG_SMOKE, SONG_ROCKYOU,
};
const uint8_t SONG_LEN[7] = {  // number of {note, duration} pairs per song
  sizeof(SONG_TWINKLE) / 2,      sizeof(SONG_MARY) / 2,    sizeof(SONG_ODE) / 2,
  sizeof(SONG_LETITGO_HOOK) / 2, sizeof(SONG_LETITGO) / 2, sizeof(SONG_SMOKE) / 2,
  sizeof(SONG_ROCKYOU) / 2,
};

// ---- Echo mode ----
const uint8_t  ECHO_MAX_EVENTS = 32;     // ~160 B of SRAM (5 B per event)
const uint16_t ECHO_SILENCE_MS = 1500;   // quiet time before the parrot replies

// ---- Debug ----
#define DEBUG 1   // set to 1 to print state over Serial @115200

#endif // CONFIG_H
