# Terykons 0.37.0 — each synth track its own ENV and LFO

Package: `terykons-0.37.0.fwsc`, identity `FM-1_73003700`. After 0.36.2 (`b25902f`: the TEMPO page).

![ENV](img/screen-env.png) ![MOD](img/screen-mod.png)

- **Each synth track's own.** Until now one envelope (ENV) and one LFO (MOD) served all four synth tracks. Now S1 to S4
  each have their own ATTACK, DECAY, RELEASE and LFO RATE, DEPTH, DEST; the ENV and MOD pages show the selected
  track's. A pad can swell slowly on S2 while S1's pluck stays short; S3 can wobble while S4 stays still.
- **Part of the sound.** SAVE keeps each track's. A user preset now holds its ENV and LFO with the rest of the sound;
  a factory preset brings them as a sound starts (ATTACK 10, DECAY 50, RELEASE 30; LFO RATE 50, DEPTH 0, off). A preset
  saved before 0.37 loads as it did, leaving the track's ENV and LFO as they were.
- **Lockable.** In LOCK mode (EDIT) ATTACK, DECAY, RELEASE, LFO RATE and LFO DEPTH lock on a step, like OSC's knobs;
  LFO DEST does not.
- **Drum tracks** have neither (their drums decay by their own DECAY): on D1 to D4 the pages say THE LFO IS S1-S4'S and
  A DRUM ENVELOPES ITSELF, and a knob turned there LFO/ENV S1-S4 ONLY.
- **Older settings** give every synth track the one ENV and LFO they had; settings saved by 0.37 still read in an older
  firmware (it takes S1's).

Storage: a preset record 184 → 192 bytes (a marker, then the LFO's three and the envelope's three); the bank reads
both sizes. Settings 1428 → 1452 bytes.

Also: the screenshot script went from the terricons to the drum's DRUM page one HOME short since 0.36.2 (the TEMPO page
now between); fixed, the drum pictures as they were.

Validation on 30 October 2026: all 22 simulator scenarios (new: `envlfo.txt`: tracks independent, drum tracks
guarded, a user preset bringing its ENV and LFO back, the factory's, an ATTACK lock, settings from 0.36), the
screenshot script, the dual-core, sequencer, pattern bank and preset tests (an old 184-byte record and a new one) pass.
Device build/compiler checks pass: image **395360 B**, RAM **73472 / 98304 B**, pool **332968 / 344064 B**; package
**611496 B**.

Package SHA-256: `f57f82b73935f15af986688b2c2d224fc4b16c6d0aeefa9ae550c8a2dd84ce97`.

Not yet installed on the FM-1.
