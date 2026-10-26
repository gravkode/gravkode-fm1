# GRAV CODE 0.23.0 — the sequencer's screen

Package: `grav_code-0.23.0.fwsc`, identity `FM-1_73002300`. After 0.22 (`a9ffe6e`: the sound editor, the knobs).

0.22 took the keyboard off the screen. This version gives that room to the sequencer: SEQ (HOME) is redrawn as a
step sequencer that moves with the music.

![The SEQ page playing a bar, in the simulator](img/seq-playing.gif)

- **The steps:** the selected track's 16 steps of the page shown, in four beats of four (each beat's first slot a
  little lighter), 52 pixels tall. A step on is an amber bar, darker at its foot, as tall as its velocity, with a
  white tick at its note's height among the track's notes (none on a drum track) and a dot for each further note of
  a chord. A held step is framed; recording, the step the next chord goes to is marked red.
- **As it plays:** the step playing lights its slot; a step on flashes white and leaps six pixels as it begins, then
  settles back over 180 ms. A cursor under the steps glides from the step playing to the next, in time (the tempo,
  a MIDI clock's too).
- **The pages:** four bars, the one shown amber, a white mark travelling through them where the track plays.
- **The tracks' map:** every track's 16 steps of the page it is playing (stopped: of the page shown, or its last if
  shorter): S1–S4 then D1–D4, the selected one amber, each with its length and its step playing, so tracks of
  different lengths are seen drifting apart. A note flashes white as it begins.
- **SEQ's knobs in the bottom band**, small: TEMPO, SWING, LENGTH (a held step's NOTE, VELOCITY, LENGTH), growing
  and lit as they turn, as on every page. Under them PLAY/STOP, REC and its step / STEP / CLOCK, the pattern and the
  next one, UNSAVED or PENDING for the preset, CPU. A moment's message takes the line's left part (the volume too,
  as it moves: SEQ has no VOL field).

Only the strip under the track's line is redrawn while it plays: 240×122 pixels, about 30 times a second, 10 while
the sound takes 75 % or more; a knob still redraws alone in its tile. Stopped, nothing moves, and nothing moves under
a list (tracks, patterns, preset slots) until it goes. No change to the sound, the sequencer's timing or what is
saved.

Validation on 26 October 2026:

- All **14 simulator scenarios** pass, plus the screenshot script. The sequencer's scenario now checks the strip is
  redrawn alone while playing (65 frames in 2.1 s, the last under 40 ms before) and not at all once stopped.
- Sequencer timing, pattern bank and preset tests pass. The DSP is unchanged; the dual-core comparison was not
  re-run.
- Device build/compiler checks pass: image **396532 B** (+1344), RAM **91544 / 98304 B** (**6760 B free**), pool
  **332968 / 344064 B** (**11096 B free**). Package **611496 B**; no soft-double helpers, unsafe instruction patterns
  or direct register access introduced.

Package SHA-256: `7e8b4e347b27d68617ab5ca7836202be201ab3d480704ea64b2c63c5d3956245`.

Not installed on the FM-1 in this change. How smooth the animation looks on the panel, and the CPU and screen load
with eight tracks and the effect, need checking on the FM-1. The GIF is a simulator capture.
