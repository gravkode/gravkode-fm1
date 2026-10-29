# Terykons 0.25.0 — recording, finished

Package: `terykons-0.25.0.fwsc`, identity `FM-1_73002500`. After 0.24 (`a388440`: live recording, the metronome).

What live recording still lacked: a swing for each track, a count-in, notes from a MIDI keyboard, and a way to erase
while recording.

- **Each track its own swing.** SEQ's K2 SWING is the selected track's now: 50 % straight to 75 %, its value grey
  while the track still follows the pattern's swing. SEQ held and K2 sets the pattern's swing and makes every track
  follow it again (SEQ let go then does not switch step mode). A swung hi-hat over a straight kick, say. Saved in each
  track's byte reserved until now: patterns saved before keep the pattern's swing on every track.
- **A count-in.** GLO's K3 METRONOME has three positions: OFF, REC (clicks while recording live, as in 0.24), COUNT
  (the same, and a bar of clicks before recording that starts from a stop: REC, then PLAY). Counting in, nothing
  plays and nothing is recorded but a note in its last half step, which goes to the first step; the bottom band
  counts IN 4 .. IN 1; MIDI start goes out with the first step. Not while following a MIDI clock.
- **MIDI in recorded.** Notes coming in over USB or TRS while recording live are recorded as the keys' are, with
  their own velocity and length (up to eight held at once). The audio interrupt queues them; the main loop writes
  them, through the same code as the keys.
- **Erasing while recording.** SEQ held while recording live switches off the selected track's steps as the
  sequencer passes them, for as long as it is held (ERASE in the bottom band).

**How the swing is played now.** The steps come on a straight grid; a track's swing puts its notes on the second of
each pair later, by (2 × swing − 100) % of a step, and their ends with them. For a track following the pattern this
is where 0.24 played it, to the sample (the sequencer's test checks swing at 75 % exactly, on the clock in too); the
MIDI clock out stays straight, as it was.

Validation on 26 October 2026:

- All **15 simulator scenarios** pass; `rec.txt` now also checks a track's own swing and SEQ held with K2, the
  count-in (four clicks, no step, an early note not recorded, the first step after it), a MIDI note recorded with its
  velocity and length, and erasing while SEQ is held.
- The sequencer's unit test adds a track at 75 % on a straight pattern (each note and its end where swing puts them,
  the other track on the grid), a bad swing refused, and the count-in (four beats, no MIDI start or note until the
  bar is over, quantizing waiting for its last half step). The pattern bank, preset and 1008 bit-exact dual-core
  tests pass.
- Device build/compiler checks pass: image **401044 B** (+1488), RAM **91992 / 98304 B** (**6312 B free**), pool
  **332968 / 344064 B** (**11096 B free**). Package **611496 B**; no soft-double helpers, unsafe instruction patterns
  or direct register access introduced.

Package SHA-256: `dd28b21543fa9d1b26ad72f5adacbc9d2660583dec5d6016b1a2c48ece77d2fc`.

Not installed on the FM-1 in this change: the swing of different tracks by ear, the count-in, a MIDI keyboard's
notes and erasing need trying there.
