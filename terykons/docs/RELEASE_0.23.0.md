# Terykons 0.23.0 — the sequencer's screen

Package: `terykons-0.23.0.fwsc`, identity `FM-1_73002300`. After 0.22 (`7bcbd3d`: the sound editor, the knobs).

0.22 took the keyboard off the screen. This version gives that room to the sequencer, in two views: HOME shows the
selected track alone, SEQ (step mode) every track.

![One bar playing, in the simulator: HOME's piano roll, step mode's eight tracks](img/seq-playing.gif)

- **HOME: one track, a piano roll.** Its 16 steps of a page across, in beats of four, numbered under them; its notes
  up and down, from its lowest to its highest (at least an octave), the two named at the left and a line at each C.
  A note is a bar at its pitch, as long as its length, brighter the harder it plays; a chord, a bar for each note; a
  drum track's hits, a block each, bigger the harder. It shows the page playing (stopped or recording: the page
  shown). As it plays the step's column lights, a white line sweeps across in time (the tempo, a MIDI clock's too),
  and the notes flash white as they begin. Recording, the next chord's step is marked red.
- **SEQ (step mode): every track, a grid.** Eight rows, S1–S4 then D1–D4, named at the left, the selected one on an
  amber ground: the 16 steps of the page shown, as the white keys are; a step on lit, brighter the harder (the
  selected track's amber, the others' grey), a notch for a chord, a dot past a track's length. Each track's step
  playing lights, its notes flashing, so tracks of different lengths are seen drifting apart. A held step is framed.
- **The buttons follow the views.** HOME always shows the track alone (from step mode too: step mode ends). SEQ
  shows every track, from any page; SEQ again goes back to the page it was pressed on (before, it stayed on SEQ's
  page and showed the one track). Another page's button ends step mode, so the keys play again.
- **The pages**, under either: four bars, the one shown amber, a white mark travelling where the track plays.
- **SEQ's knobs in the bottom band**, small: TEMPO, SWING, LENGTH (a held step's NOTE, VELOCITY, LENGTH), growing
  and lit as they turn, as on every page. Under them PLAY/STOP, REC and its step / STEP / CLOCK, the pattern and the
  next one, UNSAVED or PENDING for the preset, CPU. A moment's message takes the line's left part (the volume too,
  as it moves: SEQ has no VOL field).

Only the part under the track's line is redrawn while it plays: 240×122 pixels, about 30 times a second, 10 while
the sound takes 75 % or more; a knob still redraws alone in its tile. Stopped, nothing moves, and nothing moves
under a list (tracks, patterns, preset slots) until it goes. No change to the sound, the sequencer's timing or what
is saved.

Validation on 26 October 2026:

- All **14 simulator scenarios** pass, plus the screenshot script. The sequencer's scenario now checks the strip is
  redrawn alone while playing (65 frames in 2.1 s, the last under 40 ms before) and not at all once stopped, and the
  buttons' paths: SEQ and SEQ again from OSC, SEQ then LFO, SEQ then HOME.
- Sequencer timing, pattern bank and preset tests pass. The DSP is unchanged; the dual-core comparison was not
  re-run.
- Device build/compiler checks pass: image **397524 B** (+2336), RAM **91544 / 98304 B** (**6760 B free**), pool
  **332968 / 344064 B** (**11096 B free**). Package **611496 B**; no soft-double helpers, unsafe instruction patterns
  or direct register access introduced.

Package SHA-256: `b41cd8ed648604744c3c8ea2154f837ad41cf8885f954c98cfc2d846459bf076`.

A first build of this version, with one view for both (the selected track's steps as bars, a map of every track
below), ran on the FM-1; this one is not yet installed. The GIF is a simulator capture.
