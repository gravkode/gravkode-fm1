# GRAV CODE 0.33.0 — the lake: the effect's pages under the terricons

Package: `grav_code-0.33.0.fwsc`, identity `FM-1_73003300`. After 0.32.0 (`5487d8c`: HALL, TAPE, ECHO).

![HALL](img/screen-fx-hall.png) ![TAPE](img/screen-fx-tape.png) ![ECHO](img/screen-fx-echo.png)

![The lake while the pattern plays, in the simulator](img/fx-lake.gif)

**The effect's pages show what it does.** HALL, TAPE and ECHO are a lake at night under HOME's terricons: the same
terricons, drawn by the same code (`draw_ridge`), smaller, each track's in its colour, bending with its sound as on
HOME and mirrored in the water. A track with no step is a grey heap, so the ridge is never empty. On the water:

- **A drop:** a note going into the effect (its track's SEND above 0) falls in, a white ring as bright as it is
  loud. A chord is one drop: the ring grows while its attack rises.
- **The echoes:** ECHO's repeats follow each drop, TIME apart (as heard: fitted to the line), thicker rings in the
  theme's colour, the first as strong as AMOUNT sends, each next weaker by FEEDBACK.
- **The tail:** HALL's reverb ripples on as long as it sounds, fainter rings, more of them as DIFFUSE is higher,
  brighter as TONE is, carried further as TIME is longer.
- **MOD** sways the rings, and the reflections while the water moves. **QUALITY** shows as the medium: VINTAGE's
  rings on every other row, CLEAN TAPE's wavering, DIRTY TAPE's broken up over a grain.
- **The page's own part is the brighter:** the tail on HALL, the echoes on ECHO, both on TAPE.

The drops and the tail are measured from the effect as it plays (`grav_fx_meters`: the loudest sample since the last
frame of what is sent in and of HALL's tail; the sound itself is not touched). With nothing going in the water is
still and nothing is redrawn; while it moves, the lake alone is redrawn, about 30 times a second (10 while the sound
is busy), as HOME's terricons are.

**The knobs** of the three pages are small now, in the bottom band, as the mixer's; under them ECHO's time (1/8.
375 MS), FX ON or OFF, and CPU. The FX pages no longer show OCT, PRESET and VOL.

Validation on 29 October 2026:

- All 19 simulator scenarios pass, and the screenshot script (the FX pages now shot while the pattern plays).
  `fx.txt` checks the lake: no drop off the effect's pages; a drop with the effect off, no tail; a drop and the
  tail's ripples on HALL; the repeats on ECHO; the water still, and its strip no longer redrawn, once all has died.
- HOME's terricons are pixel for pixel as before (their drawing shared with the lake).
- The 1018 bit-exact dual-core comparisons pass with the meters in; the sequencer, pattern bank and preset tests pass.
- Device build/compiler checks pass: image **382004 B** (+7152), RAM **70080 / 98304 B** (**28224 B free**; the lake
  takes 1800 B), pool **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction
  patterns or direct register access introduced.

Package SHA-256: `d1988d803b9416d21343aecc32df18c1b9238b2f47e76293c6fd89209c2367ef`.

Not yet installed on the FM-1: the lake's frame rate and the load beside the sound are to be seen there.
