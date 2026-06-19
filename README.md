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
arduino-cli upload  --fqbn arduino:avr:nano:cpu=atmega328old -p /dev/cu.usbserial-XXXX firmware/child_buzzer
# Genuine Nanos (new bootloader): drop the :cpu=atmega328old suffix. Clone
# boards hang in avrdude sync without it.
```

## How to play

- **Press a key** → its note sounds while held (one note at a time; newest key wins).
- **Turn the knob** → shifts all keys up/down by octaves (deep ↔ squeaky) in Piano
  and Echo modes; controls tempo in Songs mode.

## Modes & gestures

Hold two keys for ~0.6 s to trigger a gesture (the held keys stay silent during the hold):

| Keys       | Gesture                | Feedback        |
|------------|------------------------|-----------------|
| 1 + 7 (ends) | toggle vibrato       | rising chirp    |
| 2 + 6      | toggle pentatonic scale | falling chirp  |
| 3 + 5      | next mode              | 1–4 beeps = current mode |

Modes (announced by beep count when you cycle into them):

1. **Piano** — the classic 7-key C-major piano; knob = octave.
2. **FX** — each key is a sound effect: siren, laser, slide whistle, UFO,
   alarm, bird, robot.
3. **Songs** — each key plays a melody (key 1 = Twinkle Twinkle Little Star,
   key 2 = Mary Had a Little Lamb, key 3 = Ode to Joy, key 4 = a pop-ballad
   chorus hook, key 5 = the full pop ballad (verse → pre-chorus → chorus),
   key 6 = a hard-rock riff, key 7 = a stadium stomp-and-chant); knob = tempo;
   press the same key again to stop.
4. **Echo** — play something, pause 1.5 s, and the toy parrots it back.

Vibrato works in every mode; pentatonic changes the keyboard notes (Piano and
Echo). The knob sets the octave in Piano and Echo, and the tempo in Songs.

## Roadmap

- v2: line/aux audio output jack (design + parts list in the spec appendix).

## License

Released under the [MIT License](LICENSE) — free to use, modify, and build on
with attribution.
