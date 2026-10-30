# Terykons 0.41.1 — simpler chains

Package: `terykons-0.41.1.fwsc`, identity `FM-1_73004101`. After 0.41.0 (`a063ffe`: chains).

0.41.0 added every white key pressed with HOME held to the chain: a pattern picked, then another (a change of mind),
made a chain, and a key pressed twice went in twice. Now:

- **Each pattern once.** A key pressed again while HOME is held is not added: pressing a pattern twice is still just
  that pattern, no chain. A chain is two or more different patterns, in the order pressed (1, 2, 3).
- **Stopping ends the chain** (CHAIN OFF): the pattern it was on stays, nothing queued. (0.41.0 took it back to its
  first.) One pattern picked by hand still ends it too; HOME and the key of the pattern playing ends it and plays on.

Also: the tap tempo's taps (0.38) shift without `memmove`, which the FM-1's C library does not declare (it linked,
but the build warned); the device build is now without warnings.

Validation on 30 October 2026: all 26 simulator scenarios (`chain.txt`: a key again not added; stopping ends the
chain, the pattern it was on staying, nothing queued), the screenshot script, the dual-core, sequencer, pattern bank
and preset tests pass. Device build/compiler checks pass: image **400064 B**, RAM **73728 / 98304 B**, pool
**332968 / 344064 B**; package **611496 B**.

Package SHA-256: `2d33a5704ce6d51257d7e86a89f7675c5b4ad2fb1b56c5277c02bb632ae73bcd`.

Not yet installed on the FM-1.
