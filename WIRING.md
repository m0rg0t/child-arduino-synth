# Wiring — Child Buzzer Synth (v1)

Board: Arduino Nano. All switches use the internal pull-ups, so a pressed key
just connects its pin to GND (reads LOW). No external resistors needed.

## Keyboard switches (7)

| Key | Note | Nano pin | Switch wiring |
|-----|------|----------|---------------|
| 1 (lowest)  | C | D2 | one leg → D2, other leg → GND |
| 2 | D | D3 | one leg → D3, other leg → GND |
| 3 | E | D4 | one leg → D4, other leg → GND |
| 4 | F | D5 | one leg → D5, other leg → GND |
| 5 | G | D6 | one leg → D6, other leg → GND |
| 6 | A | D7 | one leg → D7, other leg → GND |
| 7 (highest) | B | D8 | one leg → D8, other leg → GND |

Tie every switch's "other leg" to a shared GND rail.

## KY-006 passive buzzer

| KY-006 pin | Connect to |
|------------|------------|
| S (signal) | D9 |
| − (GND)    | GND |
| centre pin (labelled `+` or `VCC`) | leave unconnected (passive module; varies by board) |

## Rotary angle sensor (potentiometer) — octave knob

| Pot pin | Connect to |
|---------|------------|
| one outer leg   | 5V |
| other outer leg | GND |
| wiper (middle)  | A0 |

If the knob shifts pitch the "wrong" way, swap the two outer legs (5V ↔ GND).

## Power & ground

Power the Nano over USB (or regulated 5V). All grounds (switches, buzzer, pot)
share one common GND.

## How to play

- Press a key → its note sounds while held (one note at a time, newest wins).
- Turn the knob → shifts all keys across 4 octaves (deep ↔ squeaky) in Piano
  and Echo modes; sets the tempo in Songs mode.
- Two-key hold gestures (~0.6 s): lowest + highest → vibrato (rising chirp);
  2nd + 6th → pentatonic scale (falling chirp); 3rd + 5th → next mode,
  announced by 1–4 beeps (Piano / FX / Songs / Echo). Full mode guide is in
  README.md ("Modes & gestures").

## Troubleshooting

- Keys register (serial `DEBUG` shows `note`/`freq`) but no sound → check the
  KY-006 S and − connections; the joint is known to go intermittent after
  enclosure reassembly. Consider soldering + strain relief.
