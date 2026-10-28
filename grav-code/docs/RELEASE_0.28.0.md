# GRAV CODE 0.28.0 — HOME's terricons

Package: `grav_code-0.28.0.fwsc`, identity `FM-1_73002800`. After 0.27.2 (`7d5e934`: the code tidied).

![One bar playing, in the simulator: HOME's terricons at the left, step mode's eight tracks at the right](img/seq-playing.gif)

HOME shows the song as a ridge of eight terricons instead of 0.27's eight concentric rings. What they show and how
they move is unchanged; only the shape and the layout are new.

- **A terricon for each track.** S1 stands at the left and D4 at the right, all on one ground line, each in its
  track's colour (rose, orange, amber, yellow, green, teal, blue, violet, left to right). Each has its own height
  and place, S1 the tallest. Where two overlap, the lower one stands in front and hides the foot of the one behind.
- **The same states as the rings:**
  - a track with no step on has no terricon (its place stays empty);
  - a quiet one is its colour, dim and still;
  - a sounding one bends with its own sound: its level, measured before the mix and followed with an attack and a
    release, makes waves run along its slopes, with a glow under its line brighter the louder;
  - a muted one (or one silenced by another's solo) is grey and still.
- **Each engine moves in its own way:** a kick a strong, quick bulge, and its peak jumps up as it hits; a snare a
  sharper one; a hi-hat small, quick teeth; a bass big smooth waves; a pad slow ones. The waves leave the feet and
  the peak where they are.
- **The selected track's terricon** is thicker and white at its core, with a marker and its name over its peak. Its
  level, six segments in its colour, is now at the top right.
- **The rest of HOME is unchanged:** the bottom band (TRK, engine, MUTE / SOLO / NO VOICE / FREEZE, CPU, the sound's
  name), K1 TEMPO, K2 SWING, K3 LENGTH, about 30 frames a second while anything sounds (10 when the sound is busy).

**In the code:** `draw_terricons` replaces `draw_rings`. A terricon is a line of 45 points from foot to foot. The
ground under it is filled black first, so it hides what stands behind it; then its line is drawn with `thick_line`
(0.27.2's fast thick line, now open-ended). The rings' state is renamed after what it is: each track's level
(`ui.level`, `levels_update`), its colour and look (`TRACK_HUE`, `TRACK_LOOK`). The test hooks follow:
`terricons`, `terricons_sounding`, `terricons_heard` (before: `rings...`).

Validation on 28 October 2026:

- All 18 simulator scenarios pass, and the screenshot script. `browser.txt` and `playback.txt` check the terricons
  through the renamed hooks: none at first, one for each track with a step, sounding and still as they play, stop
  and are muted.
- The pictures are new: HOME (`screen-seq.png`), the lists and the presets' browser over it, GLO's version and the
  playing GIF.
- The sequencer, pattern bank, preset and 1018 bit-exact dual-core tests pass.
- Device build/compiler checks pass: image **410756 B** (+304), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `1ebb2b83791ee2a622c74432875277c79cefbb1d60fa688211f744aa4041819b`.

Not yet installed on the FM-1.
