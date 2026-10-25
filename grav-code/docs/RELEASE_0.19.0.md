# GRAV CODE 0.19.0

Package: `grav_code-0.19.0.fwsc`, identity `FM-1_73001900`.

- **Six drums.** The drum tracks play GRAV CODE's own drums, built on Plaits' drum models; each renders only the model
  chosen. PRESET on a drum track steps among six: BASS DRUM, SYN KICK, SNARE, SYN SNARE, HI-HAT, RING HAT. The second
  models are the ones Plaits' drum engines sent to their AUX out, unused until now; they are brought to the first
  ones' peaks.
- **Six knobs a drum**, on two pages: HOME on a drum track goes SEQ, DRUM, SHAPE, SEQ (HOME blinking faster on each).
  DRUM: TUNE (a half semitone a detent, two octaves either way), DECAY, TONE. SHAPE: the kick's PUNCH (the attack's
  pitch jump), SWEEP (the body's pitch bend; on SYN KICK its time) and DRIVE (on SYN KICK its dirt), which Plaits
  folds onto HARMONICS one after the other; the snare's SNAPPY, the hi-hat's NOISE, each with DRIVE and LEVEL.
- **The same sound after the update:** settings saved by 0.18 load with TUNE at the note and SHAPE taken from
  HARMONICS as Plaits does; the analog kick renders sample for sample as Plaits' bass drum did.
- **The header** no longer names the page: the page's button blinks (faster on its second and third pages).

Validation: device build and memory/code checks (image 350 KB, RAM 85 KB of 96, pool 333 KB of 336), the ten GRAV
CODE simulator scripts (`drums.txt`: the six drums, the two pages, 0.18's settings), the sequencer's and the bank's
tests, the real-thread DSP test (960 bit-exact comparisons, the second models among them), the analog kick compared
sample for sample with Plaits' bass drum, the six drums' peaks measured. Not yet heard on the FM-1.
