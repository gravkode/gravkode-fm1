# Terykons 0.15.0

Package: `terykons-0.15.0.fwsc`, identity `FM-1_73001500`.

- **The sequencer**, one track: 64 steps in four pages of 16, a length of 1 to 64, a note or a chord of up to four
  per step with its velocity and its length in steps, tempo 30–300 BPM, swing 50–75 %.
- **Timing to half a millisecond**: the sequencer's events land on a 24-sample grid; the sound is rendered between
  them, not at the 5.8 ms blocks.
- **MIDI**: the sequencer's notes over USB (channel 1); Start, 24 clocks a quarter note and Stop while it plays; a
  MIDI clock coming in (USB or TRS) leads it, with Start, Continue and Stop, its tempo measured and swing on it.
- **SEQ is the main page** (HOME; HOME again: OSC, the sound): the steps of a page (velocity, note height, chords,
  the step playing), TEMPO, SWING, LENGTH. Only the steps' strip is redrawn as the step moves.
- **Step mode (SEQ)**: the white keys are the steps (a tap on or off), a step held with K1..K3 its note, velocity
  and length, the first black keys and OCT- / OCT+ the pages, SEQ held and a step the length.
- **Step recording (REC, stopped)**: a note or a chord a step; SELECT skips. PLAY starts and stops.
- **The lights**: the open page's button blinks, faster on its next page (FX: GRAIN, SHAPE, MIX; HOME: SEQ, OSC;
  GLO: GLO, DEBUG); PLAY flashes green on each beat, longer on the bar's first; red with REC blinking while
  recording; in step mode the keys show the steps and the pages.
- **DEBUG** moves to GLO's second press (REC records now).
- SAVE keeps the pattern (its own store object); writing to the flash stops the sound for a moment.

Settings remain compatible with 0.12–0.14.1; a FM-1 without a saved pattern starts with an empty one at 120 BPM.

Validation: device build and memory/code checks (image 337 KB, RAM 72 KB of 96, pool 333 KB of 336), the seven
Terykons simulator scripts (with `seq.txt`), the sequencer's timing test (`seq_test.c`: straight, swing, length,
chords, stop, clock out and in, to the sample) and the real-thread DSP test. Not yet measured on the FM-1: the
timing by ear, MIDI clock with a DAW, and the load with the sequencer playing.
