# Terykons 0.38.0 — tap tempo

Package: `terykons-0.38.0.fwsc`, identity `FM-1_73003800`. After 0.37.0 (`a9c73d8`: each synth track its own ENV
and LFO).

![TEMPO, tapped](img/screen-tempo.png)

**Tap the tempo.** On the TEMPO page (HOME from HOME's terricons) every key now taps the tempo instead of playing:
tap along with the beat, and from the second tap TEMPO is the taps' average over the last four beats. The TEMPO knob
lights and eases to it, the bottom band says TAP TEMPO and the BPM (the first tap: TAP AGAIN), and the band's hint
reads ANY KEY TAP TEMPO.

- A pause longer than 2.5 s starts a new count; the tempo stays within 30 to 300 BPM.
- While the song plays it follows at once, and plays on. SAVE keeps it, as TEMPO turned.
- A tap plays no note and sends no MIDI. A key held from another page still lets its note go.
- HOME held with a white key still picks a pattern; in LOCK mode (EDIT) the white keys are still the steps.
- Following a MIDI clock in, the clock leads: a tap says TEMPO MIDI CLOCK and changes nothing.

Why the keys: the FM-1's fourteen buttons all have their jobs, and on the TEMPO page the keys had none worth keeping;
they are the biggest thing on the panel to tap with one hand.

Validation on 30 October 2026: all 23 simulator scenarios (new: `tap.txt`: the average, uneven taps, a new count,
the limits, while playing, a pattern picked, a MIDI clock in, LOCK mode, a held note let go, saving), the screenshot
script (`screen-tempo.png` now tapped), the dual-core, sequencer, pattern bank and preset tests pass. Device
build/compiler checks pass: image **395520 B**, RAM **73504 / 98304 B**, pool **332968 / 344064 B**; package
**611496 B**.

Package SHA-256: `b42c3126d1a9b295f6122bfbc0b133145adbd7c021e3fcf25646f7e5a3bab2f4`.

Not yet installed on the FM-1.
