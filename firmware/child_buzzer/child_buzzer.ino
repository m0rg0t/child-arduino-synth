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

// ---- Play mode ----
enum Mode : uint8_t { MODE_PIANO = 0, MODE_FX, MODE_SONG, MODE_ECHO, MODE_COUNT };
uint8_t  mode = MODE_PIANO;

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

// ---- FX engine (MODE_FX) ----
int8_t   fxKey = -1;       // key whose effect is running; -1 = idle
uint16_t fxHz = 0;
int8_t   fxDir = 1;        // +1 stepping up, -1 stepping down
bool     fxDone = false;   // FX_ONCE finished / FX_RETRIG waiting out its gap
uint32_t fxLastStepMs = 0;  // last step time; completion time while fxDone

void fxStart(uint8_t key, uint32_t now) {
  fxKey  = (int8_t)key;
  fxHz   = FX_DEFS[key].startHz;
  fxDir  = (FX_DEFS[key].endHz >= FX_DEFS[key].startHz) ? 1 : -1;
  fxDone = false;
  fxLastStepMs = now;
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
    if (d.behavior == FX_RETRIG && now - fxLastStepMs >= FX_RETRIG_GAP_MS) {
      fxStart((uint8_t)fxKey, now);
    } else {
      return -1;
    }
  }

  if (now - fxLastStepMs >= d.stepMs) {
    fxLastStepMs = now;
    int32_t lo = min(d.startHz, d.endHz);
    int32_t hi = max(d.startHz, d.endHz);
    int32_t next = (int32_t)fxHz + (int32_t)fxDir * (int32_t)d.stepHz;
    if (next < lo || next > hi) {
      switch (d.behavior) {
        case FX_ONCE:
        case FX_RETRIG:
          fxHz = d.endHz;
          fxDone = true;  // FX_RETRIG measures its gap from completion via fxLastStepMs
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

// ---- Song player (MODE_SONG) ----
int8_t   songKey = -1;            // -1 = stopped
uint8_t  songPos = 0;             // current {note,duration} pair
bool     songInGap = false;       // articulation gap at the end of a note
uint16_t songFreq = 0;            // 0 = rest
uint32_t songPhaseStartMs = 0;    // when the current sound/gap phase began
uint32_t songPhaseLenMs = 0;      // length of the current phase
uint32_t songLastStamp = 0;       // pressCounter baseline for new-press detection
uint32_t songStartOrder = 0;      // press stamp that started the current song

// Newest key pressed since the last call (-1 = none); suppressed keys ignored.
// Presses on suppressed (combo) keys are consumed so they can't fire later.
int songTakeNewPress(uint8_t suppressMask) {
  int best = -1;
  uint32_t bestOrder = songLastStamp;
  uint32_t maxSeen = songLastStamp;
  for (uint8_t i = 0; i < 7; i++) {
    if (!keyHeld[i]) continue;
    if (pressOrder[i] > maxSeen) maxSeen = pressOrder[i];
    if ((suppressMask >> i) & 1) continue;
    if (pressOrder[i] > bestOrder) {
      bestOrder = pressOrder[i];
      best = i;
    }
  }
  songLastStamp = maxSeen;  // suppressed presses are dropped, not deferred
  return best;
}

// Knob position -> tick length (left = slow, right = fast).
uint16_t songTickMs() {
  int raw = analogRead(KNOB_PIN);  // 0..1023
  return (uint16_t)(SONG_TICK_MS_SLOW
                    - (uint32_t)raw * (SONG_TICK_MS_SLOW - SONG_TICK_MS_FAST) / 1023);
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
  songPhaseStartMs = now;
  songPhaseLenMs = (durMs > SONG_GAP_MS) ? durMs - SONG_GAP_MS : durMs;
}

void songStart(uint8_t key, uint32_t now) {
  songKey = (int8_t)key;
  songStartOrder = pressOrder[key];
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
  // A song started by the first finger of a combo gesture is cancelled once
  // the gesture engages (that same press becomes suppressed).
  if (songKey >= 0 && ((suppressMask >> songKey) & 1) &&
      pressOrder[songKey] == songStartOrder) {
    songStop();
  }
  if (songKey < 0) return -1;

  if (now - songPhaseStartMs >= songPhaseLenMs) {
    if (!songInGap) {
      songInGap = true;
      songPhaseStartMs = now;
      songPhaseLenMs = SONG_GAP_MS;
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
uint32_t  echoPhaseStartMs = 0;     // when the current sound/gap phase began
uint32_t  echoPhaseLenMs = 0;       // length of the current phase
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
  echoPhaseStartMs = now;
  echoPhaseLenMs = echoBuf[0].durationMs;
}

// Echo frequency for this tick (-1 = silence). Records live piano playing;
// after ECHO_SILENCE_MS of quiet it parrots the phrase back, then clears.
int echoTick(int note, uint8_t band, uint8_t suppress, uint32_t now) {
  if (echoPlaying) {
    if (note >= 0) {
      echoReset();  // kid interrupted the parrot; fall through to record
    } else {
      if (now - echoPhaseStartMs >= echoPhaseLenMs) {
        if (echoInGap) {
          echoInGap = false;
          echoPhaseStartMs = now;
          echoPhaseLenMs = echoBuf[echoPos].durationMs;
        } else {
          echoPos++;
          if (echoPos >= echoCount) {
            echoReset();
            return -1;
          }
          // Even a 0-length gap yields one silent tick — that re-articulates
          // repeated notes past the lastFreqWritten dirty flag. Don't
          // optimize it away.
          echoInGap = true;
          echoPhaseStartMs = now;
          echoPhaseLenMs = echoBuf[echoPos].gapMs;
        }
      }
      if (echoInGap) return -1;
      uint8_t nb = echoBuf[echoPos].noteAndBand;
      return (int)noteFrequency(nb & 0x07, (nb >> 3) & 0x03);
    }
  }

  // Recording: live piano behavior, transitions logged.
  if (note != echoLiveNote) {
    if (echoLiveNote >= 0) {
      if (keyHeld[echoLiveNote] && ((suppress >> echoLiveNote) & 1)) {
        // Live note vanished because its key joined a combo: drop it rather
        // than record the gesture's first key as a junk event.
        echoLiveNote = -1;
        echoSilenceStartMs = now;
      } else {
        echoCloseNote(now);
      }
    }
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

// Two quick beeps as toggle feedback. Briefly blocking (~220 ms) — fine for a
// toy; recovery is via lastFreqWritten = -1 below (a playing song resumes).
// Blocking time is not credited to song/echo phase timers — a playing melody skips ~220 ms.
void chirp(uint16_t hz1, uint16_t hz2) {
  tone(BUZZER_PIN, hz1); delay(CHIRP_TONE_MS);
  noTone(BUZZER_PIN);    delay(CHIRP_GAP_MS);
  tone(BUZZER_PIN, hz2); delay(CHIRP_TONE_MS);
  noTone(BUZZER_PIN);    delay(CHIRP_GAP_MS);
  lastFreqWritten = -1;              // force the next loop to re-assert the tone
}

void toggleVibrato()    { vibratoOn    = !vibratoOn;    chirp(CHIRP_LO_HZ, CHIRP_HI_HZ); }  // rising
void togglePentatonic() { pentatonicOn = !pentatonicOn; chirp(CHIRP_HI_HZ, CHIRP_LO_HZ); }  // falling

// Stop all mode engines and drop transient state (called on every mode change).
void resetEngines() {
  fxKey = -1;
  fxDone = false;
  songStop();
  songLastStamp = pressCounter;  // ignore presses from before the mode switch
  echoReset();
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

// ---- Combo gestures (hold two keys COMBO_HOLD_MS to fire once per hold) ----
struct ComboState {
  uint8_t keyA, keyB;
  void (*onFire)();
  bool active;        // both keys currently held (candidate window)
  bool fired;         // already fired during this hold
  uint32_t startMs;
};

const uint8_t COMBO_COUNT = 3;
ComboState combos[COMBO_COUNT] = {
  { COMBO_VIBRATO_A, COMBO_VIBRATO_B, toggleVibrato,    false, false, 0 },
  { COMBO_PENTA_A,   COMBO_PENTA_B,   togglePentatonic, false, false, 0 },
  { COMBO_MODE_A,    COMBO_MODE_B,    cycleMode,        false, false, 0 },
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
    if (combos[c].active) {
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
  now = millis();  // combo callbacks (chirps/announce) block; don't hand engines a stale tick

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
    case MODE_FX:
      freq = fxTick(note, now);
      break;
    case MODE_SONG:
      freq = songTick(now, suppress);
      break;
    case MODE_ECHO:
      freq = echoTick(note, band, suppress, now);
      break;
    default:
      break;
  }

  if (freq == 0) freq = -1;  // engines may use 0 for "rest"; never hand 0 to tone()

  // Vibrato layers over every mode's output.
  if (freq > 0 && vibratoOn) freq = applyVibrato((uint16_t)freq);

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
    Serial.print(F("mode=")); Serial.print(mode);
    Serial.print(F(" "));
    Serial.print(F("note=")); Serial.print(note);
    Serial.print(F(" band=")); Serial.print(band);
    Serial.print(F(" freq=")); Serial.print(freq);
    Serial.print(F(" penta=")); Serial.print(pentatonicOn ? 1 : 0);
    Serial.print(F(" ec=")); Serial.print(echoCount);
    Serial.print(F(" ep=")); Serial.print(echoPlaying ? 1 : 0);
    Serial.print(F(" vib=")); Serial.println(vibratoOn ? 1 : 0);
  }
#endif
}
