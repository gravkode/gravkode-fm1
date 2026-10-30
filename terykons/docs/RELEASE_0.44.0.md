# Terykons 0.44.0 — the show

Package: `terykons-0.44.0.fwsc`, identity `FM-1_73004400`. After 0.43.1 (`afacf04`: the logo without the sun).

Left to play with nothing touched, the FM-1 now puts on a show: the whole screen becomes a picture of the music, with
no words on it. Made for a chain playing on its own, or a pattern left to run.

- **When:** the FM-1 plays, and no button, knob or key has been touched for SHOW's time: GLO's SCREEN page, the new
  K3 SHOW, OFF, 10 S, 30 S (as it comes), 1 MIN, 2 MIN. From any page, but not in step mode, in LOCK mode, while
  recording, over a list or on DEBUG.
- **What:** each step leaves a row of terricons, a track each in its colour, S1 at the left to D4 at the right. A
  track that began a note in the step stands up as a peak, as high as it sounded; a sound going on from before is a
  low ridge under the peaks; a silent track leaves the ground flat. The rows go back towards the horizon as the steps
  go on, one a step, smaller and dimmer, the newest at the front growing as it plays: the rhythm a range of hills
  going away. Above them HOME's sky (SKY: the stars and the moon, the sun and the clouds, or none); the horizon
  glows in the pattern's colour and flares at each kick; a new pattern (a chain moving on) shows its number a
  moment, a chain's patterns under it.
- **Ending it:** a button or a knob ends the show and does what it does: PLAY stops at once, a knob turns. The keys
  play on under it, and the show follows them. Stopping ends it.
- **Saved** with the settings (SAVE); settings from before 0.44 have SHOW at 30 S.

The manual: section 5, The show; section 4, SCREEN.

Inside: the show draws the whole screen in two canvases (144 and 96 rows) at HOME's pace (about 30 frames a second, 10
while the sound takes 75 % or more), on its own frame timer; the sound is never held up by it. Its rows are kept in a
ring of 24 (a byte a track each); the settings grow from 1472 to 1476 bytes (`SET_SIZE_043`).

Validation on 30 October 2026: all 29 simulator scenarios (`show.txt`, new: not before SHOW's time, its rows and
frames, a key under it, a knob ending it and the show again, PLAY stopping and ending it, none stopped, OFF never,
none in step mode, the values' ends, saving, settings from 0.43), the screenshot script (`screen-show.png`; no other
picture catches the show), the dual-core, sequencer, pattern bank and preset tests.
Device build/compiler checks pass: image **409248 B**, RAM **94244 / 98304 B**, pool **332968 / 344064 B**; package
**611496 B**. Not yet seen on an FM-1: how smooth the show runs under a heavy patch (10 frames a second at 75 % or
more).

Package SHA-256: `65c5ae4489fdca5c800a28a07d9c0109f66dcaec1b544f5157271254ddf5a81e`.
