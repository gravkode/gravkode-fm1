# GRAV CODE 0.30.1 — a bigger step grid; the day without a sky

Package: `grav_code-0.30.1.fwsc`, identity `FM-1_73003001`. After 0.30.0 (`6101c9c`: the engine in the header).

![One bar playing, in the simulator: HOME by day at the left, step mode's bigger grid at the right](img/seq-playing.gif)

**Step mode's grid, bigger.** The header names the engine now (0.30), so step mode's bottom band no longer repeats
the track and its engine. It is one line, 32 pixels high, as HOME's is: PLAY or STOP, REC or STEP, the pattern,
UNSAVED or FREEZE, CPU. Its room goes to the grid:
- the rows are 15 pixels apart (11 before), their steps 12 x 13 (12 x 9);
- the tracks' names are larger, each in its colour;
- the grid runs on down to the screen's foot, the pattern's pages under it.

TEMPO, SWING and LENGTH stay at the page's top. Like HOME's scene, the grid is drawn into two canvases (the
screen's holds 144 rows at most): the page, and its foot under it, both as it plays.

**The day without a sky.** By DAY HOME has no sky behind it now, only black, as OFF has. Its sun still flares at
each kick, and its three soft clouds drift across, four times slower than 0.30's, still at the tempo's pace. On
black they are whiter (nine tenths white at most), and they are blended over whatever is under them, so the sun
shows through a cloud passing it, with no dark rim.

**A fix:** the marker over the selected terricon was drawn as a flat bar since 0.29.3 (its shadow covered all but
its top row). It is a triangle again.

Validation on 29 October 2026:

- All 18 simulator scenarios pass, and the screenshot script.
- The pictures are new: step mode, HOME by day, the playing GIF and the day GIF.
- The sequencer, pattern bank, preset and 1018 bit-exact dual-core tests pass.
- Device build/compiler checks pass: image **422980 B** (−64), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `ce8a0134cd9709063833ce824f7f578e3951c4be4a0da774c32ff80a343a1cca`.

Not yet installed on the FM-1.
