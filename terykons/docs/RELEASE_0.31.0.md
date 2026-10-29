# Terykons 0.31.0 — the track alone: a piano roll, a drum's pages; SAVE's sweep

Package: `terykons-0.31.0.fwsc`, identity `FM-1_73003100`. After 0.30.2 (`d139c36`: night as it comes).

![SEQ again on S1: its notes as a piano roll](img/screen-seq-notes.png) ![SEQ again on D1: its steps, a page a row](img/screen-seq-drum.png)

![One bar playing, in the simulator: S1's piano roll at the left, D1's pages at the right](img/seq-notes.gif)

**SEQ again: the track alone.** SEQ turns step mode on, every track on the screen, as before; pressed again it now
shows the selected track alone, the whole page its own, under TEMPO, SWING and LENGTH. A third press goes back to
the page step mode was entered from (HOME, as ever, to the terricons). The bottom band names the track, with NOTES
or STEPS. The keys work as in step mode: a white key a step on or off, held with K1 to K3 its notes, velocity and
length; OCT- / OCT+ the pages; the black keys another track, the view following it.

- **A synth track: its notes as a piano roll.** The page's 16 steps across (the white keys), 20 semitones up, a
  keyboard at the left with each C named. A note is a bar on its row, as long as its length (a note held from the
  page before goes on into it), brighter the harder, its start lit; a chord, its notes one over the other. The roll
  follows the notes: it stays while they fit and centres them when one goes out; one still out is a mark at its
  edge. The step playing has its column lighter, the step held is framed.
- **A drum track: all its steps.** Its 64 steps a page a row, the page on the white keys on a ground of its colour;
  a step on shows its velocity as a bar up from its foot; past the LENGTH, dots.

**SAVE's sweep.** A tap on SAVE lights the 16 white keys one after another, first to last (35 ms each), then all of
them for a quarter of a second, then they show what they showed before. SAVE FAILED lights none.

Before, SEQ pressed in step mode turned it off; now that takes a third press (or HOME). The tests and the screenshot
script tap SEQ accordingly.

Validation on 29 October 2026:

- All 19 simulator scenarios pass, and the screenshot script. New checks: SEQ's three presses, the roll's range, a
  step put on and off in it, a drum track's view by a black key, SAVE's sweep from start to end.
- The pictures are new: the piano roll, the drum's pages and their GIF; the others shot again (S1 has three more
  notes, D1 32 steps).
- The sequencer, pattern bank, preset and 1018 bit-exact dual-core tests pass.
- Device build/compiler checks pass: image **425524 B** (+2464), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `6de80a0927cd4bd6a270290c6e8430cda6f225bf9ac3fb7df5b75c2ee1d9a745`.

Not yet installed on the FM-1.
