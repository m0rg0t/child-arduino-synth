# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware (Arduino Nano / ATmega328P) for a 7-key mini-piano toy driving a KY-006
passive buzzer. Seven switches play a C-major scale, an analog knob shifts the
octave, and a "squeeze both ends" gesture toggles a vibrato wobble. There is no
host application — the deliverable is the flashed `.ino`. Also versioned here:
`stl/` (enclosure models), `WIRING.md` (pin-by-pin), and the design spec/plan
under `docs/superpowers/`.

## Build / flash / debug

The sketch lives in `firmware/child_buzzer/` (sketch dir name must match the
`.ino` for the Arduino toolchain).

```bash
arduino-cli core install arduino:avr                                      # one-time
arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer         # build
arduino-cli upload  --fqbn arduino:avr:nano -p /dev/cu.usbserial-XXXX firmware/child_buzzer
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200          # read DEBUG output
```

- **Clone Nanos** (old bootloader) need `--fqbn arduino:avr:nano:cpu=atmega328old`.
- IDE path: open the `.ino`, Board → Arduino Nano, Processor → ATmega328P, Upload.

There is **no automated test harness** — verification is the serial `DEBUG`
output plus the manual smoke-test checklists in
`docs/superpowers/specs/2026-06-03-child-buzzer-synth-design.md` (v1) and
`docs/superpowers/specs/2026-06-12-firmware-v2-modes-design.md` (v2 modes).
`DEBUG` is a
compile-time flag in `config.h` (currently `1`); when set it prints
`mode/note/band/freq/penta/ec/ep/vib` over Serial @115200 every 200 ms.

## Architecture

Two files, by deliberate separation of concerns:

- **`config.h`** — all *tunables* (pins, `NOTE_HZ` table, `OCTAVE_BANDS`,
  debounce/combo timings, vibrato depth/rate/LUT, the FX parameter rows
  (`FX_DEFS`), the PROGMEM song tables, `DEBUG`). Re-tune the toy by
  editing here only; never put behavior in this header.
- **`child_buzzer.ino`** — all *logic*. `loop()` is the orchestrator each tick:
  debounce all keys → detect combos → advance vibrato phase → dispatch to the
  active mode engine → write `tone()`/`noTone()`.

Constraints and patterns that span the code (read these before changing behavior):

- **One hardware tone timer ⇒ monophonic.** Only one note can sound. Polyphony
  is impossible without leaving `tone()`. Playback is **last-pressed-wins**: a
  monotonically increasing `pressCounter` stamps `pressOrder[i]` on each press
  (0 = released); `activeNote()` returns the held key with the highest stamp.
- **`lastFreqWritten` dirty-flag.** `tone()` is only re-called when the target
  frequency actually changes (incl. `-1` ⇒ `noTone()`). Anything that should
  force the tone to re-assert (e.g. `chirp()`) must reset it to `-1`.
- **Integer-only math in `loop()`** to stay fast: octaves shift by doubling
  (`NOTE_HZ[i] << band`, exact, no floats); vibrato modulates via a `±1000`
  sine LUT and a per-mille depth factor (`applyVibrato`).
- **Switches are `INPUT_PULLUP`** — a pressed key reads **LOW** (shorts pin to
  GND); no external resistors. Each key has its own `DEBOUNCE_MS` filter.
- **Three table-driven combos, all edge-triggered.** Holding a pair for
  `COMBO_HOLD_MS` fires its gesture exactly once per hold: keys 0+6 toggle
  vibrato, keys 1+5 toggle pentatonic, keys 2+4 cycle the play mode. State is
  tracked in a `ComboState` array (`combos[]`); while any combo is active its
  two keys are muted via a bitmask passed to `activeNote()` and mode engines
  (`comboSuppressMask()`).
- **Four exclusive modes** (`Mode` enum: piano / FX / song / echo) cycled by
  the 2+4 combo with beep-count announce. Every mode engine returns one
  frequency (or -1) per tick into the shared `lastFreqWritten` dirty-flag
  `tone()` write; vibrato and the pentatonic table-swap layer on top in any
  mode.
- **Wraparound-safe timing throughout.** All elapsed-time checks use the
  `now - since >= interval` idiom; new timing code must follow the same
  pattern to stay safe across `millis()` rollover.

Pin map (also in `config.h` / `WIRING.md`): keys C–B → D2–D8, buzzer → D9,
octave knob wiper → A0.

## Scope / roadmap

v1 is buzzer-only. A passive line/aux audio-jack add-on (no firmware change) is
designed and parts-listed in the spec appendix, **deferred to v2**.
