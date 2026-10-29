# Terykons 0.39.0 — the MIDI page

Package: `terykons-0.39.0.fwsc`, identity `FM-1_73003900`. After 0.38.0 (`dbe86b4`: tap tempo).

![The MIDI page](img/screen-midi.png)

**GLO again: MIDI.** GLO's round is now GLO, MIDI, SCREEN, DEBUG. The MIDI page sets how Terykons talks to other gear:

- **K1 CHANNEL:** the selected track's MIDI channel (SELECT picks the track), OFF or 1 to 16. At first S1 to S4 are on
  1 to 4 and D1 to D4 on 5 to 8, as before. The keys', the arp's and the sequencer's notes go out on it; OFF sends
  none (the track still plays).
- **K2 IN:** SELECTED (as it comes, as before): a note in on any channel plays the selected track. CHANNELS: a note in
  on a track's channel plays that track (on a channel several share, all of them), so a DAW or another sequencer can
  play all eight tracks at once; a note on a channel no track has plays the selected one.
- **K3 CLOCK:** OFF, IN, OUT or IN+OUT (as it comes, as before): whether the MIDI clock, with Start, Continue and Stop,
  is followed from MIDI in, sent out while the song plays by its own tempo, both or neither. CLOCK OUT turned off
  while playing sends a Stop; CLOCK IN turned off while following a clock stops the song.

Above the knobs every track with its channel (the selected one lit, OFF a red dash), under them what IN does.

**No stuck notes.** A note out keeps its channel: its note-off goes where its note-on went, even if the track's channel
changed meanwhile. A note in held goes back to the tracks it started on, even if IN or the selected track changed.
Recording live records the notes that play the selected track.

Saved with the settings (SAVE); settings from before 0.39 keep MIDI as it was. Program change in (any channel: a
pattern) is unchanged.

Storage: settings 1452 → 1464 bytes (each track's channel, IN, CLOCK).

Validation on 30 October 2026: all 24 simulator scenarios (new: `midi.txt`: GLO's round, a key's note on its
channel and its note-off after a change, OFF, the sequencer's notes on D1's channel, IN SELECTED and CHANNELS, two
tracks on one channel, a held note across an IN change, the four CLOCK settings, saving, settings from 0.38; five
scenarios that crossed GLO's pages now pass MIDI), the screenshot script (`screen-midi.png` new), the dual-core,
sequencer, pattern bank and preset tests pass. Device build/compiler checks pass: image **397440 B**, RAM
**73696 / 98304 B**, pool **332968 / 344064 B**; package **611496 B**.

Package SHA-256: `16e30a337b8d7da33f93e7605bbe18ce24f3bdbb1429871540822e7f33730baf`.

Not yet installed on the FM-1.
