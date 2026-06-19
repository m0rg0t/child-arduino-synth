# Design: "Let It Go" in song mode (two slots)

Date: 2026-06-18
Status: approved (design), pending on-device pitch verification

## Goal

Add a buzzer melody arrangement of *Let It Go* (Frozen) to **song mode**:

- **Key index 4** (the 5th physical key, README "key 5", currently *Frère
  Jacques*) → **full arrangement**: verse → pre-chorus → chorus, played once.
- **Key index 3** (the 4th physical key, README "key 4", currently *London
  Bridge*) → **short chorus hook** only.

*Frère Jacques* and *London Bridge* are displaced (removed). The other five
songs are unchanged.

## Constraints (from the existing hardware/firmware)

These are fixed by the toy and the song engine; the arrangement is built to fit
them, not the other way around:

1. **Monophonic.** One `tone()` voice → melody line only. No chords, no
   harmony, no accompaniment.
2. **White keys only.** A song note byte encodes a scale degree 0–6 into the
   major table `NOTE_HZ` (`C D E F G A B`) plus an octave band 0–3. There is no
   representation for sharps/flats. Any non-diatonic note in the source must be
   nudged to the nearest white key — the same approach already documented for
   *Smoke on the Water*.
3. **No looping.** `songTick()` stops at the end of the table; a song plays once
   per press (press the same key again to stop early, a different key to
   switch).
4. **Knob = tempo.** `songTickMs()` maps the knob to 350 ms/tick (left, slow) …
   110 ms/tick (right, fast). A ballad sounds best toward the slow end. No
   per-song tempo storage — the player is tempo-agnostic.
5. **Data, not engine.** This is purely new `PROGMEM` tables + roster wiring.
   The song engine (`songTick`, `songLoadEvent`, `songNoteHz`, gaps, rests) is
   not modified.

## Musical approach

*Let It Go* sits in a relative minor→major key pair (verse in a minor mode, the
chorus lifting to its relative major — one shared key signature). That maps
cleanly onto white keys by anchoring:

- **Verse** in **A natural minor** (`A B C D E F G` — all white keys),
- **Chorus** in its relative **C major** (`C D E F G A B` — all white keys).

So the bulk of the tune lands on white keys with no accidentals. The few
chromatic / raised-leading-tone moments get rounded to the nearest white key.
Octave placement: verse low (bands 0–1), pre-chorus rising, chorus up in bands
1–2 so the signature rising "let it go" sequence reads clearly on the buzzer.

This is a **melodic reduction** — recognizable contour within the monophonic,
white-key, diatonic envelope — not a faithful reproduction of the recording.

## Data model (unchanged format)

Two new `PROGMEM` byte streams of `{note, duration}` pairs, same encoding as the
existing seven songs (`SN(deg, oct)`, `SONG_REST` for rests):

- `SONG_LETITGO[]` — verse + pre-chorus + chorus. Est. ~60–80 pairs
  (~120–160 B PROGMEM).
- `SONG_LETITGO_HOOK[]` — the chorus phrase only. Est. ~16–28 pairs.

Flash budget is a non-issue (32 KB part, current sketch is small; the v2 song
set added only ~250 B total).

## Files changed

- `firmware/child_buzzer/config.h`
  - Remove `SONG_LONDON[]` and `SONG_FRERE[]`.
  - Add `SONG_LETITGO_HOOK[]` (slot 3) and `SONG_LETITGO[]` (slot 4).
  - Update `SONGS[]`, `SONG_LEN[]`, and the per-key comments to the new roster.
- `README.md` — update the songs list (key 4 = Let It Go hook, key 5 = Let It
  Go full).
- `docs/superpowers/specs/2026-06-12-firmware-v2-modes-design.md` — update the
  song-list entries for keys 4 and 5 so the canonical reference stays accurate.

## Verification (empirical, on hardware)

There is no automated audio test; pitch correctness is verified by ear. The
Arduino is attached, so the loop is:

1. Transcribe → `arduino-cli compile --fqbn arduino:avr:nano`.
2. Flash with `cpu=atmega328old` (this board's old-bootloader requirement).
3. Cycle to song mode (hold keys 2+4 → 3 announce beeps).
4. Press key 4 (index 3) → short chorus hook; press key 5 (index 4) → full
   arrangement. Confirm both are recognizable; turn the knob to a slow tempo.
5. The user listens and flags any off notes; iterate on the tables and re-flash.

Acceptance: both keys play a recognizable *Let It Go*, the full version moves
through verse → pre-chorus → chorus, tempo responds to the knob, and the other
five songs still play. Record the final compiled flash size.

## Out of scope

Chords/harmony (hardware can't), lyrics (note data only — no text reproduced),
looping, per-song tempo, and any change to the song engine or other modes.
