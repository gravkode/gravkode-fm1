# Terykons 0.27.0 — HOME's rings, the presets' browser

Package: `terykons-0.27.0.fwsc`, identity `FM-1_73002700`. After 0.26.2 (`2c1d443`: the mixer, solos).

From the brief "HOME UI redesign & preset architecture": HOME shows the whole song instead of one track, presets are
browsed on their own (the engine follows the preset), and the mixer's level is its own.

![One bar playing, in the simulator: HOME's rings, step mode's grid](img/seq-playing.gif)

- **HOME: eight concentric rings**, a track each, S1 the outermost (radius 64) and D4 the innermost (15), each in a
  colour of its own (rose to violet). A track with no step on in its pattern has none; a quiet one is its colour,
  dim and round; a sounding one bends and swells with its own sound, a glow under it brighter the louder; a muted
  one (or one silenced by a solo) is grey and still. The selected track's ring is thicker, white at its core, with a
  marker; its name in the middle, its level at the bottom right. How a ring moves comes from its engine (three waves
  round it, their counts and speeds): a kick a strong quick bulge as it hits, a snare a sharper one, a hi-hat small
  quick teeth, a bass big smooth waves, a pad slow ones; no sound analysis.
- **Its level is the track's own**, before the mix: the render's per-track peak each block (`grav_track_peak`, 0.26),
  followed on the UI's side with an instant attack and a release of the engine's look (90..500 ms). The audio path is
  unchanged; the page redraws alone (240×144, ~13 ms over DMA) about 30 times a second while anything moves (10 while
  the sound takes 75 % or more), not at all when still.
- **The bottom band** names the track (TRK 03), its engine, MUTE / SOLO / FREEZE and the load, and its sound large (a *
  once changed after its preset); a moment's message (TEMPO 120 ...) or the count-in in its place.
- **The header** shows the pattern (P01) and PLAY (a triangle), STOP (a square) or REC (a red dot), on every page.
- **The piano roll is gone:** HOME goes rings → OSC → the sound's pages; step mode (SEQ) shows every track's steps.
- **The presets' browser (PRESET):** every sound the selected track can play, engine after engine in ALGO's order,
  each engine's four factory sounds then the user's saved on it (a drum track: its six drums'). Each row shows the
  engine's icon, the name and its kind (SYNTH, BASS, LEAD, PAD, PLUCK, KEYS, DRUM, FX, USER). Turning PRESET plays
  each sound at once, the engine changing with it, round the list; the steps, the mixer, the tempo and the other
  tracks stay; nothing is written to the flash. OCT+ shows the engine's sounds alone; OCT- puts the sound back as it
  was when it opened; it closes 3 s after the last turn, with a page's button, another track or ALGO.
- **A sound changed after its preset** keeps the name, marked * (it read CUSTOM before); loading or saving clears it.
- **Factory sounds' names** can be 16 characters long (the table in the firmware), each with a kind: the room the
  library of named sounds needs. User records (10-character names, no editor yet) are unchanged in the flash.
- **The mixer's VOL** is its own fader per track (settings, 1428 bytes now; older ones load at 100): a preset never
  changes it. Before, K1 was the sound's OUTPUT LEVEL, which goes with the preset. With every fader at 100 the sound is
  the same, sample for sample.
- **ALGO's screen** shows the engine's place: 3 / 24 (a drum track: of 6).

Validation on 28 October 2026:

- All **18 simulator scenarios** pass. The new `browser.txt`: the rings (none, then one with a step; sounding while
  playing; still once muted, and once silent), the browser (96 sounds on a synth track, 24 on a drum track; the next
  sound, past an engine's last the next engine's first; OCT- back as it was, no octave change; OCT+ the engine's
  alone, round; closing after 3 s keeping it, with HOME, with SELECT), the steps, VOL, SEND and mute unchanged while
  sounds change playing, nothing written, the * after a knob and gone on loading. `mixer.txt` checks VOL and 0.26's
  settings; `presets.txt` and `sound_pages.txt` the * and the browser in place of PRESET's old round.
- **1018 bit-exact dual-core comparisons** (the faders with the sends, one core or two); the sequencer, pattern bank
  and preset tests pass.
- Device build/compiler checks pass: image **411156 B** (+5952), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**, package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `55d791365f76381724b63099469c3e1738db2f2fed298c2bbeb39e0e53d0d230`.

Not installed on the FM-1 in this change: the rings' frame rate and look on the panel, with eight tracks, the effect
and four voices, and changing sounds while playing, need checking there.

Later, as the brief says: the factory library's names (cities of Donetsk oblast), user presets' longer names with a
name editor or SysEx, and pan (a stereo path up to the effect).
