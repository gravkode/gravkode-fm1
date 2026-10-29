# Terykons 0.36.2 — the TEMPO page

Package: `terykons-0.36.2.fwsc`, identity `FM-1_73003602`. After 0.36.1 (`b6857c9`: the logo held).

![TEMPO](img/screen-tempo.png)

**The tempo, where it can be found.** HOME's terricons kept TEMPO, SWING and LENGTH on K1..K3, but showed no knobs:
the tempo was there, hidden. Now HOME from the terricons opens a page of their own, TEMPO: **TEMPO** (K1, 30 to 300
BPM), **SWING** (K2, the selected track's) and **LENGTH** (K3, its steps) as knobs; HOME again the sound's pages
(OSC, ...), as before. HOME's round: the terricons, TEMPO, OSC, the sound's pages, OUTPUT, the terricons. The
terricons themselves are as they were (K1..K3 still turn the same three there).

Validation on 30 October 2026: all 21 simulator scenarios (the ones that went from the terricons straight to OSC now
pass through TEMPO), the screenshot script (`screen-tempo.png` new), the dual-core, sequencer, pattern bank and
preset tests pass. Device build/compiler checks pass: image **393960 B**, RAM **73120 / 98304 B**, pool
**332968 / 344064 B**; package **611496 B**.

Package SHA-256: `d131ff2ec486dc193b581a71ccfcc06aa58dde0fbbff84d821a67abc9f64fb58`.

Not yet installed on the FM-1.
