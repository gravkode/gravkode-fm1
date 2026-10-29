# Terykons 0.40.0 — patterns copied, pasted, cleared

Package: `terykons-0.40.0.fwsc`, identity `FM-1_73004000`. After 0.39.0 (`819f9a8`: the MIDI page).

![HOME held: the patterns, and under them OCT- COPY, OCT+ PASTE, REC CLEAR](img/screen-patterns.png)

**As on Elektron's machines,** with HOME held (the grid of sixteen):

- **OCT- copies** the pattern playing: its steps, every track's LENGTH and SWING, its locks. The grid's top shows COPY
  and its number.
- **OCT+ pastes** the copy into the pattern playing. To copy pattern 1 into pattern 5: on 1, HOME + OCT-; pick 5;
  HOME + OCT+.
- **REC clears** the pattern playing: every step and lock gone, every track 16 steps long, SWING straight.

A paste or a clear is heard at once, playing or stopped: the tracks go on from the step they are on, and the locks
heard so far are let go. It is saved as any edit (the pattern left, or SAVE). The octave, REC and the page stay as
they were; the grid's bottom line names the three buttons.

The copy is kept in the flash, in a slot of its own in the patterns' bank (no RAM taken): a restart keeps it, and
PASTE then says PATTERN without its number. PASTE NOTHING COPIED when there is none; PASTE WAIT while the copy is
still on its way into the flash (a moment). Playing with the bank full, COPY STOP TO COPY: stopped, it is made. An
older firmware finds no pattern in the copy's slot and erases it; the patterns are not touched. There is no undo.

Validation on 30 October 2026: all 25 simulator scenarios (new: `copy.txt`: nothing to paste, a copy, a paste into
an empty pattern, a clear, a paste while playing, the copy across a restart), the screenshot script
(`screen-patterns.png`: the buttons under the grid), the dual-core, sequencer, pattern bank (new: the copy's slot,
newest over older, beside the sixteen; a paste and a clear) and preset tests pass. Device build/compiler checks
pass: image **398800 B**, RAM **73696 / 98304 B**, pool **332968 / 344064 B**; package **611496 B**.

Package SHA-256: `158d05cad672102b720bde14b31579ed5941cedf05ab1fbab311228a536a552f`.

Not yet installed on the FM-1.
