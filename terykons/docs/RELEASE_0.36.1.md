# Terykons 0.36.1 — the logo stays a moment, its version under it

Package: `terykons-0.36.1.fwsc`, identity `FM-1_73003601`. After 0.36.0 (`324b4ac`: the name).

![The logo at power-on](img/screen-logo.png)

- **The logo stays.** Until now it showed only while the FM-1 started, half a second or so, a flash. Now it stays
  about two seconds from power-on (LOGO_MS, 2200 ms) while the FM-1 starts behind it: the sound, the keys and MIDI
  work meanwhile. A key, a button or a knob turned ends it at once. (The simulator shows the page at once, as before.)
- **Its version under it:** VERSION 0.36.1 (a build without a version: DEV).

Validation on 29 October 2026: all 21 simulator scenarios, the screenshot script, the dual-core, sequencer, pattern
bank and preset tests pass. Device build/compiler checks pass: image **393720 B**, RAM **73120 / 98304 B**, pool
**332968 / 344064 B**; package **611496 B**.

Package SHA-256: `b2f469d4dc41cbcd7eb60094de72a6dab2a1d3743b2994f0017055b66fdac552`.

Not yet installed on the FM-1.
