# GRAV CODE 0.29.3 — lower clouds, a clean moon, every icon in its box

Package: `grav_code-0.29.3.fwsc`, identity `FM-1_73002903`. After 0.29.2 (`69d390a`: a flat sky, HOME's scene taller).

![HOME by day](img/screen-seq.png) ![HOME at night](img/screen-night.png)

Reviewed screen by screen in the simulator before it was committed.

**HOME:**
- **The clouds are lower and puffier.** By day they drift low over the heaps' tops, never cut by the screen's top
  edge. Each is seven puffs now, not four.
- **The terricons are a little bigger** (a tenth taller, a little wider).
- **A clean crescent moon.** Its unlit part is the sky's own colour, no dark disc, and its glow and its ripple of
  light follow its lit edge alone.
- **The selected heap's name and marker have a shadow,** so they stay readable when a cloud passes behind them.
- **CPU sits over the level,** their edges in line at the bottom band's right.

**The engines' icons, every one inside its box.** Most were drawn edge to edge, and some grew past their box as
they played (KICK's rings on a hit, the 6-op FM spheres, NOISE's cloud, PARTICLE's orbits, SYN SNARE's noise, FM
2-OP's loops, VA VCF's peak), so they looked cut, the small ones especially. Now:
- every icon is drawn inside a margin of its box: 6 pixels on the engine's splash, 2 on its sound page, 1 in the
  tracks' list and the presets' browser;
- light is still clipped to the whole box, so a glow can reach the margin;
- the icons that grew past their box were sized to keep inside it at their fullest;
- PARTICLE was sized again for the sound page, where its orbits had become too small.

A check for this: each of the 27 icons drawn in each of its three sizes, at 40 moments, its knobs low, middle and
high, struck and not. No lit pixel lands in the box's outer frame (3 pixels on the splash, 1 elsewhere).

Validation on 29 October 2026:

- All 18 simulator scenarios pass, and the screenshot script.
- The pictures are new wherever HOME or an icon shows (the splash, the sound pages, the lists, the presets'
  pages, the engines' poster), GLO's version, and the three GIFs.
- The sequencer, pattern bank, preset and 1018 bit-exact dual-core tests pass.
- Device build/compiler checks pass: image **422612 B** (+576), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `089ede7fc6a2ecf5ce55303a5c3cf58ea4d5520bee4387b8eda11770c99e0d7f`.

Not yet installed on the FM-1.
