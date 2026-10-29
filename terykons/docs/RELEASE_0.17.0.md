# Terykons 0.17.0

Package: `terykons-0.17.0.fwsc`, identity `FM-1_73001700`.

- **Four drum tracks, D1 to D4**, after the synth tracks: each plays one of Plaits' drums (BASS DRUM, SNARE or
  HI-HAT) in a voice of its own, so the synth tracks keep all four of theirs. Each has its own OSC knobs, steps and
  LENGTH, at the one TEMPO and SWING. A fresh FM-1's kit: a kick, a snare, a closed and an open hi-hat.
- **PRESET on a drum track** steps among the three drums; GLO VOICES shows DRUM; a new step plays the drum's own
  note. The envelope and the LFO stay the synth tracks': a drum decays on its own (MORPH).
- **Step mode:** the black keys 1–8 pick S1–S4 and D1–D4 (lit selected, flashing with their notes); the pages are
  on OCT- / OCT+. SELECT goes on past S4 to D1–D4. The tracks' list and SEQ's lights show all eight.
- **MIDI:** the drum tracks' notes go out on channels 5–8 (the synth tracks' on 1–4).
- **Overload:** the protection takes a synth voice before a drum's.
- **Saving:** the pattern is two store objects now (the synth tracks with the tempo, as 0.16 saved it, and the drum
  tracks), both written straight from the pattern. Settings and patterns from 0.16 load: the kit as it comes, the
  drum tracks empty.

Validation: device build and memory/code checks (image 342 KB, RAM 84 KB of 96, pool 333 KB of 336), the nine GRAV
CODE simulator scripts (with `drums.txt`), the sequencer's timing test (a drum track in the polymeter, the two
records) and the real-thread DSP test (960 bit-exact comparisons, now with four synth voices and four drums). Not
yet measured on the FM-1: the load of four synth voices and four drums at once, with and without the effect.
