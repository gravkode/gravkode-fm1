# Terykons 0.36.0 — GRAV CODE is Terykons now

Package: `terykons-0.36.0.fwsc`, identity `FM-1_73003600`. After 0.35.0 (`540ba07`: parameter locks).

![The logo at power-on](img/screen-logo.png)

**The name.** The firmware is Terykons: on its screens TERYKONS, as they write in capitals (the splash, the crash
and safe-mode screens, the screen reader's first words), over USB (Terykons, Terykons FM-1), in its package (`terykons-0.36.0.fwsc`) and in the
repository (the app's folder is `terykons/`; `tools/build.py terykons`, the simulator `terykons_host`). The
manual, the READMEs and the site say so. The names inside the code (`grav_*`, `GRAV_*`, `dsp/grav_*`) stay as they
were.

**The logo.** At power-on, before the settings are read, the FM-1 shows five terricons in ink before a teal sun, on
paper, TERYKONS and SYNTHESIZER FOR THE M-VAVE FM-1 under them (`app_splash`).

**Nothing else changes.** The identity, the settings, the presets and the patterns in the flash are as they were: an
FM-1 with GRAV CODE takes Terykons over it and carries on.

Validation on 29 October 2026:

- All 21 simulator scenarios pass under the new name, and the screenshot script (`screen-logo.png` new, drawn by
  the FM-1's own splash code).
- The 1018 bit-exact dual-core comparisons, the sequencer, pattern bank and preset tests pass.
- Device build/compiler checks pass: image **393512 B** (+688, the logo), RAM **73120 / 98304 B** (**25184 B free**),
  pool **332968 / 344064 B**. Package **611496 B**; no 64-bit multiply beside a load, no soft-double helpers, no
  direct register access.

Package SHA-256: `1a82818df12a7ab201d445a3ae36877e3e03d305b13df76a2e51a19923ce94b8`.

Not yet installed on the FM-1.
