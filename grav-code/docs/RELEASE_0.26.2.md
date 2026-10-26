# GRAV CODE 0.26.2 — SEL from every page, FX on DRY/WET first

Package: `grav_code-0.26.2.fwsc`, identity `FM-1_73002602`. After 0.26.1 (`74f1166`: solo).

Found on the FM-1: after FX, SEL did not open the mixer (on the FX pages SEL was SEED).

- **SEL opens the mixer from every page**, the FX pages too, as it is let go; SEL again goes back to the page it was
  opened on.
- **SEED is SEL held on an FX page** (0.4 s): grains by hand while it is held, MANUAL from then on; held 2 s more, the
  grains come on their own again (AUTO), as before. A tap no longer seeds.
- **FX opens on FX MIX** (DRY/WET, REVERB, QUALITY), then FX GRAIN, then FX SHAPE, round; its button blinks slowest on
  the first.

Validation on 26 October 2026: all 17 simulator scenarios pass (`fx.txt`: the new order, SEED held, AUTO after 2 s
more, a tap opening the mixer from an FX page and back; `mixer.txt`: the mixer from FX MIX). Device build/compiler
checks pass: image **405204 B**, RAM **92120 / 98304 B**, package **611496 B**.

Package SHA-256: `226925e7ce5c12ae302ddda4a65401c91eed0d2c9fff086e70da0de99eb741a8`.
