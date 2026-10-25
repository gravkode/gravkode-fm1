# GRAV CODE 0.16.0

Package: `grav_code-0.16.0.fwsc`, identity `FM-1_73001600`.

- **Four synth tracks, S1 to S4**, playing together: each its own engine, OSC knobs (TIMBRE, MORPH, HARMONICS), voices
  and steps, with its own LENGTH (polymeters), at one TEMPO and SWING, through the one envelope, LFO and effect.
- **An engine per voice**: the FM-1's four voices are shared out among the tracks (each OFF, MONO or POLY, four in
  all); each voice builds its track's engine, anew only when that changes. A track's VOICES up takes a free voice,
  else one from the track with the most. A fresh FM-1: WAVESHAPE, VA VCF, CHORDS, STRING, one voice each.
- **SELECT picks the track** on every page, with the four tracks' list for a moment; the keys, MIDI in, PRESET, OSC,
  GLO VOICES and step mode are for it. In step mode the black keys 5–8 pick it too (lit selected, flashing with
  its notes). SEQ shows the selected track's steps and the four tracks' lights.
- **MIDI**: each track's notes go out on its channel (S1 on 1 to S4 on 4), the keys' on the selected track's.
- Saved settings and patterns from 0.15 and before load: their sound and pattern become S1's.

Validation: device build and memory/code checks (image 342 KB, RAM 74 KB of 96, pool 333 KB of 336), the eight GRAV
CODE simulator scripts (with `tracks.txt`), the sequencer's timing test (with polymeters and a 0.15 pattern) and
the real-thread DSP test (768 bit-exact comparisons). Not yet measured on the FM-1: the load of four different
engines at once, with and without the effect.
