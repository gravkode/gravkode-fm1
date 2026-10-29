# Terykons 0.18.0

Package: `terykons-0.18.0.fwsc`, identity `FM-1_73001800`.

- **Sixteen patterns.** Each has its own steps, each track's LENGTH, and SWING; the tracks' sound and the TEMPO are
  the song's, the same whichever pattern plays.
- **Picking one:** HOME held shows the sixteen and lights them on the white keys (the one playing lit, the next
  blinking, one with steps mid, an empty one dim); a white key picks it. K4 steps to the next or the one before. A
  MIDI program change 0–15 picks 1–16. HOME tapped still changes the page, now on its release.
- **Switching:** stopped, at once; playing, the one picked is queued (NEXT, blinking; P1 >3 on SEQ) and comes in at
  the end of the pattern playing, when its longest track with steps comes round. Every track starts again from its
  first step.
- **Saving:** a pattern you leave is saved by itself. Patterns are written into the flash a page at a time, into
  slots erased beforehand, so a save makes no gap in the sound, and one cut short (the power lost) loses nothing;
  the older copies are erased once nothing has sounded for 3 s. SAVE saves the pattern playing, and writes the
  settings (now with the TEMPO and the pattern you are on) only when they changed.
- **The header** shows the song's tempo in its middle (amber while a MIDI clock coming in leads); the page's name
  moved to the right.
- The pattern saved by 0.17 and before becomes pattern 1, with its tempo.

Validation: device build and memory/code checks (image 347 KB, RAM 85 KB of 96, pool 333 KB of 336), the ten GRAV
CODE simulator scripts (with `patterns.txt`), the sequencer's timing test (switching at the pattern's end, waiting
for a save), the patterns' bank test on a flash of its own (a save cut short, a damaged copy, a failed write, the
wear, a full bank) and the real-thread DSP test (960 bit-exact comparisons). Not yet checked on the FM-1: that a
save while playing makes no audible gap, and the erase after 3 s of quiet.
