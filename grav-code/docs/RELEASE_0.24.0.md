# GRAV CODE 0.24.0 — live recording

Package: `grav_code-0.24.0.fwsc`, identity `FM-1_73002400`. After 0.23 (`ced71c7`: the sequencer's screen).

REC now records while the sequencer plays, with a metronome to play against.

- **REC while playing:** what you play goes into the selected track as it goes round (PLAY red, REC lighting on
  each beat, REC LIVE in the bottom band; the page shows the track's piano roll, following the page playing). REC
  again stops recording, the sequencer plays on; stopping ends it. REC while stopped (step recording) and then PLAY
  records from the first step.
- **Always quantized:** a note goes to the step playing when played in its first half, or to the next one when in
  its second half; that next step then does not play it again this time round (it was just heard, and would sound
  twice). The note keeps the key's velocity (or GLO's fixed one); its length is how long the key is held, in steps
  (at least one). Notes within 60 ms of each other make a chord on one step, up to four.
- **Recording over a loop:** a step's first note replaces what the step held; every other step stays. On a drum
  track any key records the drum's hit. SELECT picks the track as usual, so tracks can be recorded one after
  another without stopping.
- **The metronome:** GLO's K3 is METRONOME now: REC (as it comes) clicks on each quarter note while recording live,
  the bar's first higher (2.4 kHz against 1.6 kHz) and louder; OFF stays silent. A short sine dying away over about
  20 ms, mixed after the effect at MASTER's volume (about −23 dBFS at half volume). Saved with the settings, in a
  byte reserved since 0.18 (older settings: REC). The version moved under the knobs, beside the identity.
- **Clearing a track:** SEQ held and REC: the selected track's steps all off, its length kept.

The sequencer itself gained three small things, in the audio interrupt: where a note played now belongs
(`seq_quantize`, under the lock), a step a track skips once (`skip`), and a beat event on each quarter note while
asked for (`beats`). Nothing else in its timing changes; PLAY no longer ends step recording, it goes on live.

The installer page (`web/installer.html`, every firmware's site): its "Also install m-bootloader" box is now unticked
whenever the page opens. A browser restored an earlier tick on a reload, and m-bootloader was written without being
asked for this time (harmless: the FM-1 came back on the firmware).

Validation on 26 October 2026:

- All **15 simulator scenarios** pass. The new `rec.txt` checks a note early in a step and one late in it (written to
  the next step, and that step silent once, then playing the next time round), a chord, the length from the key
  held, the velocity, REC on and off while playing, REC then PLAY, the stop ending it, a drum track, clearing, the
  metronome's clicks on and off and their level (silence with it off), and the setting through a restart.
- The sequencer's unit test checks the quantizing (stopped, early, late, a one-step track), the skip (once, cleared
  by a start) and the beats (every 4 steps, the bars' first marked, none unless asked); the pattern bank and preset
  tests pass. The DSP is unchanged: the 1008 bit-exact dual-core comparisons pass.
- Device build/compiler checks pass: image **399556 B** (+2032), RAM **91736 / 98304 B** (**6568 B free**), pool
  **332968 / 344064 B** (**11096 B free**). Package **611496 B**; no soft-double helpers, unsafe instruction patterns
  or direct register access introduced.

Package SHA-256: `0d298b957007b3a3a9961b918a3cebc7c7535fb31e677e81dcd11a237f7bee26`.

Not installed on the FM-1 in this change. How the quantizing feels with the FM-1's keys (their scan and the screen's
frame add up to ~20 ms), the click's level against a mix, and notes from MIDI in (not recorded yet) need checking.
