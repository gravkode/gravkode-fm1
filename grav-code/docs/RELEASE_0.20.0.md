# GRAV CODE 0.20.0

Package: `grav_code-0.20.0.fwsc`, identity `FM-1_73002000`.

- **An icon for every engine**, drawn by code and alive: it moves with the engine's three knobs (a drum's TONE, DECAY
  and SHAPE's first) and with its track's notes. WAVESHAPE's wave folds further as TIMBRE goes up, FM 2-OP's
  modulator orbits wider with its index, ADDITIVE's harmonics gather where TIMBRE puts them, CHORDS stacks its
  chord's notes, STRING rings after each pluck, a kick's rings jump at each hit, 6-OP FM shows its operators. All 27
  engines, the drums' second models among them; lines in the theme's two hues, a strike's flash white.
- **The sound page** (OSC; a drum's DRUM and SHAPE) shows the icon in a live area above the knobs, with a line on
  the engine beside it. Elsewhere the engine is its name alone, as before.
- **The splash:** turning PRESET shows the new engine for 1.5 s over the page: its icon alive, its name, its line.
- **The live area** is redrawn alone between the screen's redraws, about 30 times a second (10 while the sound takes
  over 75 % of its time), and stops while a list is over it.

Validation: device build and memory/code checks (image 373 KB, RAM 85 KB of 96, pool 333 KB of 336), the eleven
GRAV CODE simulator scripts and `icons.txt` (where the live area is, its frames, the splash), the sequencer's and
the bank's tests, the real-thread DSP test. Not yet checked on the FM-1: that the animation leaves the sound alone
(DEBUG's PEAK and XRUN with the sound page open and eight tracks playing).
