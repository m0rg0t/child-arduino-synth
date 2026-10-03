// Compile the production sketch itself; only Arduino hardware primitives are fake.
#include <cassert>
#include <iostream>
#include "../firmware/child_buzzer/child_buzzer.ino"

void reset() {
    fake::reset();
    for (int i = 0; i < 7; ++i) {
        keyHeld[i] = lastReading[i] = false;
        lastChangeMs[i] = pressOrder[i] = 0;
    }
    pressCounter = 1; lastFreqWritten = -1;
    vibratoOn = false; vibratoPhase = 0; lastVibratoMs = 0;
    pentatonicOn = false; mode = MODE_PIANO;
    for (auto &combo : combos) { combo.active = combo.fired = false; combo.startMs = 0; }
    resetEngines();
}
void tick(uint32_t now) { fake::now = now; loop(); }
void setKey(int key, bool held) { fake::pins[KEY_PINS[key]] = held ? LOW : HIGH; }

void test_debounce_priority_and_release() {
    reset(); setup();
    for (auto pin : KEY_PINS) assert(fake::modes[pin] == INPUT_PULLUP);
    assert(fake::modes[BUZZER_PIN] == OUTPUT);
    setKey(1, true); tick(100); tick(104); assert(activeNote(0) == -1);
    tick(105); assert(activeNote(0) == 1); assert(fake::audio.back() == NOTE_HZ[1]);
    auto writes = fake::audio.size(); tick(106); assert(fake::audio.size() == writes);
    setKey(3, true); tick(110); tick(115); assert(activeNote(0) == 3);
    setKey(3, false); tick(120); tick(125); assert(activeNote(0) == 1);
    setKey(1, false); tick(130); tick(135); assert(activeNote(0) == -1);
    assert(fake::audio.back() == -1);
}
void test_bounce_and_clock_rollover() {
    reset(); setKey(2, true); tick(UINT32_MAX - 2);
    tick(1); assert(!keyHeld[2]); tick(2); assert(keyHeld[2]);
    reset(); setKey(2, true); tick(10); setKey(2, false); tick(13);
    setKey(2, true); tick(16); tick(20); assert(!keyHeld[2]); tick(21); assert(keyHeld[2]);
}
void test_combo_threshold_once_rearm_and_rollover() {
    reset(); keyHeld[0] = keyHeld[6] = true;
    pressOrder[0] = 2; pressOrder[6] = 3;
    uint32_t start = UINT32_MAX - 300;
    updateCombos(start); assert(activeNote(comboSuppressMask()) == -1);
    updateCombos(start + COMBO_HOLD_MS - 1); assert(!vibratoOn);
    updateCombos(start + COMBO_HOLD_MS); assert(vibratoOn);
    assert(fake::audio.size() == 4); assert(lastFreqWritten == -1);
    updateCombos(start + 2000); assert(vibratoOn); assert(fake::audio.size() == 4);
    keyHeld[0] = false; updateCombos(2000); keyHeld[0] = true;
    updateCombos(2001); updateCombos(2601); assert(!vibratoOn);
}
void test_octaves_and_vibrato_bounds() {
    reset();
    for (int raw = 0; raw <= 1024; ++raw) {
        fake::knob = raw;
        assert(readOctaveBand() == std::min(raw / 256, 3));
    }
    for (int key = 0; key < 7; ++key) for (int band = 0; band < 4; ++band) {
        auto base = noteFrequency(key, band); assert(base == NOTE_HZ[key] * (1 << band));
        for (vibratoPhase = 0; vibratoPhase < VIBRATO_STEPS; ++vibratoPhase) {
            auto hz = applyVibrato(base);
            assert(hz >= base * 960 / 1000 && hz <= base * 1040 / 1000);
        }
    }
}
void test_all_combo_actions_and_mode_reset() {
    for (int c = 0; c < COMBO_COUNT; ++c) {
        reset(); keyHeld[combos[c].keyA] = keyHeld[combos[c].keyB] = true;
        fxKey = 2; songKey = 2; echoCount = 3; echoPlaying = true;
        updateCombos(100); updateCombos(699);
        assert(!vibratoOn && !pentatonicOn && mode == MODE_PIANO);
        updateCombos(700);
        assert((c != 0) || vibratoOn); assert((c != 1) || pentatonicOn);
        if (c == 2) {
            assert(mode == MODE_FX && fxKey == -1 && songKey == -1);
            assert(echoCount == 0 && !echoPlaying);
        }
        auto writes = fake::audio.size(); updateCombos(2000);
        assert(fake::audio.size() == writes);
    }
    reset(); for (int i = 0; i < MODE_COUNT; ++i) cycleMode();
    assert(mode == MODE_PIANO);
}
void test_fx_bounds_release_and_rollover() {
    reset();
    for (int key = 0; key < 7; ++key) {
        fxKey = -1; uint32_t now = UINT32_MAX - 100;
        const auto &def = FX_DEFS[key];
        for (int i = 0; i < 1000; ++i, now += def.stepMs) {
            int hz = fxTick(key, now);
            assert(hz == -1 || (hz >= min(def.startHz, def.endHz) && hz <= max(def.startHz, def.endHz)));
        }
        assert(fxTick(-1, now) == -1 && fxKey == -1);
    }
}
void test_song_tables_controls_and_rollover() {
    reset(); fake::knob = 0; assert(songTickMs() == SONG_TICK_MS_SLOW);
    fake::knob = 1023; assert(songTickMs() == SONG_TICK_MS_FAST);
    for (int key = 0; key < 7; ++key) {
        const auto *song = pgm_read_ptr(&SONGS[key]);
        assert(SONG_LEN[key] > 0);
        for (int pos = 0; pos < SONG_LEN[key]; ++pos) {
            auto note = pgm_read_byte(song + 2 * pos);
            assert(note == SONG_REST || note == SONG_THUD || note == SONG_CLAP ||
                   ((note & 7) < 7 && (note >> 5) == 0));
            assert(pgm_read_byte(song + 2 * pos + 1) > 0);
            assert(songNoteHz(note) < 10000);
        }
    }
    keyHeld[0] = true; pressOrder[0] = ++pressCounter;
    uint32_t now = UINT32_MAX - 50;
    assert(songTick(now, 0) > 0); assert(songKey == 0);
    assert(songTick(now + songPhaseLenMs - 1, 0) > 0);
    now += songPhaseLenMs;
    assert(songTick(now, 0) == -1 && songInGap);
    now += SONG_GAP_MS; assert(songTick(now, 0) > 0 && songPos == 1);
    pressOrder[0] = ++pressCounter; assert(songTick(now + 1, 0) == -1 && songKey == -1);
    pressOrder[1] = ++pressCounter; keyHeld[1] = true;
    assert(songTick(now + 2, 0) > 0 && songKey == 1);
    assert(songTick(now + 3, 1 << 1) == -1 && songKey == -1);
    assert(songTick(now + 4, 0) == -1);  // suppressed press was consumed
}
void test_echo_capacity_clamping_playback_and_interrupt() {
    reset(); uint32_t now = UINT32_MAX - 100;
    echoTick(2, 1, 0, now); echoTick(-1, 1, 0, now + 200);
    assert(echoCount == 1 && echoBuf[0].durationMs == 200 && echoBuf[0].gapMs == 0);
    echoTick(-1, 1, 0, now + 200 + ECHO_SILENCE_MS - 1); assert(!echoPlaying);
    echoTick(-1, 1, 0, now + 200 + ECHO_SILENCE_MS); assert(echoPlaying);
    assert(echoTick(-1, 1, 0, now + 201 + ECHO_SILENCE_MS) == noteFrequency(2, 1));
    assert(echoTick(4, 2, 0, now + 202 + ECHO_SILENCE_MS) == noteFrequency(4, 2));
    assert(!echoPlaying && echoCount == 0 && echoLiveNote == 4);
    echoReset();
    for (int i = 0; i < ECHO_MAX_EVENTS + 3; ++i) {
        echoOpenNote(i % 7, 0, i * 100000U); echoCloseNote(i * 100000U + 70000U);
    }
    assert(echoCount == ECHO_MAX_EVENTS);
    for (int i = 0; i < ECHO_MAX_EVENTS; ++i) assert(echoBuf[i].durationMs == 65535);
    echoReset(); echoOpenNote(1, 0, 0); echoCloseNote(1);
    echoOpenNote(2, 0, 70000); assert(echoPendingGapMs == 65535);
    echoReset(); keyHeld[0] = true; echoTick(0, 0, 0, 100);
    echoTick(-1, 0, 1, 200); assert(echoCount == 0 && echoLiveNote == -1);
}
void test_echo_captures_octave_at_press_time() {
    reset();
    assert(echoTick(1, 0, 0, 100) == noteFrequency(1, 0));
    assert(echoTick(1, 1, 0, 200) == noteFrequency(1, 1));
    echoTick(-1, 1, 0, 350);
    assert(echoCount == 1);
    assert(echoBuf[0].noteAndBand == 1 && echoBuf[0].durationMs == 250);
    echoStartPlayback(400);
    assert(echoTick(-1, 3, 0, 401) == noteFrequency(1, 0));
}
int main() {
    test_debounce_priority_and_release(); test_bounce_and_clock_rollover();
    test_combo_threshold_once_rearm_and_rollover(); test_octaves_and_vibrato_bounds();
    test_all_combo_actions_and_mode_reset(); test_fx_bounds_release_and_rollover();
    test_song_tables_controls_and_rollover(); test_echo_capacity_clamping_playback_and_interrupt();
    test_echo_captures_octave_at_press_time();
    std::cout << "9 actual-sketch test groups passed\n";
}
