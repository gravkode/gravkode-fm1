# Terykons 0.41.0 — chains

Package: `terykons-0.41.0.fwsc`, identity `FM-1_73004100`. After 0.40.0 (`3361883`: patterns copied, pasted,
cleared).

**A chain:** HOME held, white keys one after another, the same one again too (1, 1, 2, 3; up to 16). Let go of HOME:
those patterns play in that order, round, each coming in at the end of the one before (when its longest track with
steps comes round), a pattern twice in a row simply playing again.

- Stopped, the chain starts from its first at once; playing, its first is queued, as one pattern picked would be.
- Stopping takes it back to its first: PLAY again starts it from the top.
- A pattern still being saved when its end comes keeps playing once more, as before; the chain waits with it, its
  place not lost.
- One pattern picked by hand ends the chain: HOME and one key, K4, a MIDI program change.
- The grid (HOME held) shows it in its bottom line: CHAIN and its patterns, the one playing lit (while a chain is
  set, in place of the copy's buttons; they still work).
- A chain lives in RAM: not saved, gone at power-off.

Next: the song (0.42), on SEQ's third press.

Validation on 30 October 2026: all 26 simulator scenarios (new: `chain.txt`: a chain made stopped, its order and
repeats while playing, round; stopped in its middle, back to its first; one picked by hand; a chain made while
playing, its first queued; K4 and a program change ending it), the screenshot script, the dual-core, sequencer,
pattern bank and preset tests pass. Device build/compiler checks pass: image **399936 B**, RAM **73728 / 98304 B**,
pool **332968 / 344064 B**; package **611496 B**.

Package SHA-256: `0ab940f778760e8b2e1c5e5f710bac0b5746dd7d4342939f3fd82df075f2de0d`.

Not yet installed on the FM-1.
