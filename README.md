# Child Buzzer Synth

A simple 7-key mini-piano toy for a child, built on an Arduino Nano with a
KY-006 passive buzzer. Seven keys play a C-major scale, a knob shifts the
octave, and a "squeeze both ends" gesture toggles a vibrato wobble.

Design spec: `docs/superpowers/specs/`. Wiring: `WIRING.md`. Enclosure models:
`stl/`.

## Firmware

- Sketch: `firmware/child_buzzer/child_buzzer.ino`
- Tunables (pins, note table, octaves, timings, vibrato, DEBUG):
  `firmware/child_buzzer/config.h`

### Flashing with the Arduino IDE

1. Open `firmware/child_buzzer/child_buzzer.ino`.
2. Tools → Board → **Arduino Nano**.
3. Tools → Processor → **ATmega328P** (or **"ATmega328P (Old Bootloader)"** for clones).
4. Tools → Port → your Nano's serial port.
5. Click **Upload (→)**.

### Flashing with arduino-cli

```bash
arduino-cli core install arduino:avr
arduino-cli compile --fqbn arduino:avr:nano firmware/child_buzzer
arduino-cli upload  --fqbn arduino:avr:nano -p /dev/cu.usbserial-XXXX firmware/child_buzzer
# Old-bootloader clones: add :cpu=atmega328old to the FQBN.
```

## How to play

- **Press a key** → its note sounds while held (one note at a time; newest key wins).
- **Turn the knob** → moves all keys up/down by octaves (deep ↔ squeaky).
- **Hold lowest + highest keys together (~0.6 s)** → toggles vibrato on/off
  (two-beep chirp confirms).

## Roadmap

- v2: line/aux audio output jack (design + parts list in the spec appendix).
