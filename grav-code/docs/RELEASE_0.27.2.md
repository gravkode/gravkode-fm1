# GRAV CODE 0.27.2 — the code tidied, the rings drawn faster

Package: `grav_code-0.27.2.fwsc`, identity `FM-1_73002702`. After 0.27.1 (`0fa0e36`: VOICES never takes another
track's last voice).

A refactoring release. The FM-1 does what 0.27.1 did; the code is easier to read and change, and HOME's rings cost
less to draw.

**The app (`app.c`), split where it had grown:**

- `input()` (381 lines) is now a handler for each thing: `key_down`, `key_up` and `keys_input` for the keys;
  `button_press` (one `case` a button) with `play_press`, `rec_press` and `octave_press`; `save_let_go`,
  `seq_let_go` and `sel_follow` for the buttons let go; `encoder_turn` with `select_turn`, `algo_turn`,
  `page_knob_turn`, `lfo_dest_turn` and `debug_turn`.
- `lights()` (160 lines): `rec_follow` (recording live following the sequencer, erasing), `seq_follow` (the notes,
  steps and beats played), and the keys' lights of each mode apart: `step_lights`, `mixer_lights`,
  `pattern_lights`, `slot_lights`.
- `draw()` (156 lines): `draw_header` (with `draw_battery`), `draw_page`, `draw_bottom` (with `draw_midi_in`).
- `app_frame()` (132 lines): `frame_changes`, `seq_frame`, `mixer_frame`, then the one redraw.
- `app_init()`'s settings loading (115 lines): `settings_load`, `settings_upgrade` (an older record brought up to
  date, version by version) and `settings_valid`.

**Shared instead of repeated:** one red (`C_RED`, 12 places), the animation frame (`frame_ms`, 5), "a list is over
the page" (`list_open`, 4), a 6-op FM sound back to AUTO (`controls_auto`), a sound put on a track (`track_sound_set`:
a preset, and the browser's OCT- cancel), the last MIDI message in (the screen's and the screen reader's), REC
STEP's message, step mode off and HOME's rings view. The browser's first sound is chosen in one plain loop.

**Removed, unused since earlier versions:** `preset_next` (PRESET's round of an engine's sounds, before 0.27's
browser), `grav_plaits_engine` (one engine on every voice, before the tracks), a text-only knob kind (GLO's version,
before 0.24), the encoders' names (a branch no encoder reached), and stale comments (GLO's K3, the mixer's K1).

**Faster: HOME's rings.** A thick ring was drawn as w x w one-pixel lines along each of its 96 segments (a sounding
ring's glow: 16 or 25 lines a segment). It is now drawn as the same line's pixels with a w x w square on each, a run
of them along a row or a column filled as one rectangle: the same pixels, a fraction of the writes. Every ring's
angles are worked out once a frame instead of once a ring. Measured on the host (8 rings sounding, the selected one
too): 88 µs a frame before, 36 µs now; on the FM-1 the main loop has that much more time between frames.

**Two small fixes on the way:** a key held on the mixer no longer sends polyphonic pressure (for a note it never
played), and no longer keeps a chord of the keys open.

Validation on 28 October 2026:

- **The same behaviour, checked against 0.27.1:** a simulator built from 0.27.1 and this one ran every scenario plus
  two scripts made for this (the lights in every mode: 35 snapshots of the buttons' and keys' lights; the rings
  while all eight tracks play, muted, selected, stopped). Their whole output (every message said, MIDI out, lights,
  check) is the same, line for line. With the simulator's CPU figure held still (it is the host's own time), the 38
  screenshots (the manual's, the presets', the sound pages', 12 of the rings) are the same, pixel for pixel. A
  separate test drew rings both ways and found the same pixels.
- All 18 simulator scenarios pass, and the screenshot script. The sequencer, pattern bank, preset and 1018 bit-exact
  dual-core tests pass.
- Device build/compiler checks pass: image **410452 B** (−1072), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `e5d03a11480df7a83ddadad0f6bec0a6b3654e0e9df3a39e9f7e7968c5aad225`.

Not yet installed on the FM-1.
