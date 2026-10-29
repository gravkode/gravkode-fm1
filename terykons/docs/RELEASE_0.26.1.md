# Terykons 0.26.1 — solo

Package: `terykons-0.26.1.fwsc`, identity `FM-1_73002601`. After 0.26.0 (`e63090d`: the mixer).

The mixer's black keys 1–8 solo the tracks now (the white ones still mute them).

- A black key solos its track; again, not. Several can be soloed at once. While any track is soloed only the soloed
  ones sound, whatever their mutes; the mutes come back as they were once none is. Solos are not saved.
- Each strip has S beside M: S lit in the theme's colour while the track is soloed, M red while it is muted; a
  silent track's strip (muted, or another soloed) is dim. The black keys are lit while their track is soloed.
- At the top of every page a silent selected track shows MUTE, a soloed one SOLO; in step mode a silent track's row
  is red.
- On the mixer SELECT picks the track K1 and K2 are for (the black keys did before).

As the mutes, a solo is the sequencer's: a silent track's steps start no notes and its sounding notes end; the keys
still play it.

Validation on 26 October 2026: all 17 simulator scenarios pass (`mixer.txt`: a solo silencing another track's MIDI
notes, two solos, a mute under a solo kept, both solos off); the 1018 bit-exact dual-core comparisons, the
sequencer and preset tests pass. Device build/compiler checks pass: image **405268 B** (+464), RAM
**92120 / 98304 B** (unchanged), package **611496 B**.

Package SHA-256: `6a1b2f0dbcef8b173496eb603ac0ae4110595f00af8f8b0c1bc92f26cbeb610e`.

Not installed on the FM-1 in this change.
