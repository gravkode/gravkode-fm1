# Terykons 0.43.0 — panning

Package: `terykons-0.43.0.fwsc`, identity `FM-1_73004300`. After 0.42.2 (`13cebec`: MULTI at no cost to the sound).

Each track now has its place between the left and the right.

- **PAN, SEL again.** SEL opens the mixer, as before; SEL again opens PAN; SEL a third time goes back to the page the
  mixer was opened on. K1 places the selected track: L50 (full left) to C (the centre) to R50 (full right); SELECT
  picks the track. Each track is a strip as on the mixer: its name, a pan pot (its pointer up in the centre), its PAN
  in words, its sound on two meters, left and right, and M and S. The keys mute and solo here as on the mixer.
- **The sound.** PAN places a track's own sound. What it sends through the effect (SEND) goes in as before, and the
  reverb's and the echo's tails spread over both sides as they always have. Constant power: a track at a side is
  3 dB louder on that side and silent on the other, as loud moved as in the middle. Every track in the centre, the
  sound is as before, sample for sample.
- **Saved** with the settings (SAVE); settings from before 0.43 have every track in the centre. PAN is not locked on
  a step (LOCK NOT ON THIS PAGE).
- **USB audio MULTI:** the mix on the computer's inputs 1-2 has the pans; each track's own input (3 to 10) stays mono,
  to be placed again in the DAW.

The manual: section 4, PAN.

fm1-base and the engine: `grav_sound_t` has `pan` (-1 to 1). A voice off the centre gets gains √2·cos and √2·sin
(exactly 1 and 1 in the centre); while any live voice is off it, `effect_chunk` mixes the dry sound in stereo and
the effect's mono send apart, adding back only what the effect makes (it always gives `in + wet`). With every track
in the centre the old path runs unchanged. The settings grow from 1464 to 1472 bytes (`SET_SIZE_042`).

Validation on 30 October 2026: all 28 simulator scenarios (`pan.txt`, new: the centre alike on both sides, full left
silent on the right and full right on the left, R25 louder on the right, a track's own input still mono, the keys,
saving, settings from 0.42 and 0.38; `mixer.txt`, `fx.txt`, `locks.txt`, `browser.txt` pass the extra SEL), the
screenshot script, the dual-core test (its tracks now panned apart too: one core and two the same, bit for bit), the
sequencer, pattern bank and preset tests.
Device build/compiler checks pass: image **406272 B**, RAM **93828 / 98304 B**, pool **332968 / 344064 B**; package
**611496 B**. Not yet measured on an FM-1: the stereo dry mix's cost on the second core (two multiplies a voice a
sample, only while a track is off the centre).

Package SHA-256: `276f1dd1d6c5881e0ce9e213db27a6524f5025daf6f4967cac5f9d363e54b41a`.
