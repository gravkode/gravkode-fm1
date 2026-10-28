# GRAV CODE 0.27.1 — VOICES no longer silences other tracks

Package: `grav_code-0.27.1.fwsc`, identity `FM-1_73002701`. After 0.27.0 (`425ad0d`: HOME's rings, the presets'
browser).

Found on the FM-1: after turning VOICES up on GLO, only one track played and the others fell silent. Every synth
track starts with one of the four voices, so none is free. Turning S4 up to 4 took the other three tracks' only
voices and left S1..S3 at OFF. The sequencer still sent their notes, but no voice played them. The sequencer and the
drum tracks were not at fault: all eight tracks play.

- **VOICES never takes another track's last voice.** Turning a track up takes a free voice first, then one from a
  track with more than one (the one with the most). If no voice can be taken, VOICES stays as it was and says NO VOICE
  FREE: turn another track down first. Turning a track down frees the voice, as before.
- **GLO shows how the voices are shared out**, above the knobs: S1..S4 each with its number (OFF in red, the selected
  track lit) and FREE, the voices no track has.
- **HOME says NO VOICE** (red, in the bottom band) while a synth track at OFF is selected. The band's state
  (NO VOICE, MUTE, SOLO, FREEZE) now ends clear of CPU, and FREEZE no longer draws over another state.

A track left at OFF by 0.27.0 gets its voice back when its VOICES is turned up: it takes one from a track with more
than one. On the FM-1 as found: S1..S3 up to MONO each, S4 then back to MONO.

Validation on 28 October 2026:

- All **18 simulator scenarios** pass, and the screenshot script. The new `playback.txt` turns on four steps on each of the eight tracks and
  plays them: each track's notes go out on its MIDI channel and every ring sounds. With every voice taken, S4's
  VOICES up is refused and nothing falls silent. S1..S3 are turned OFF (they fall silent, the rest play on) and S4
  goes up to 4. S1..S3 then come back one by one, each taking one of S4's voices, until all eight sound again. Run
  on 0.27.0's rule, the same scenario fails as found on the FM-1: S4 at 4, S1..S3 at OFF, only S4 and the drums
  sounding.
- `voices.txt`, `tracks.txt` and `dual_core.txt` free voices explicitly (other tracks OFF) before a track takes them,
  and check that a refused VOICES says NO VOICE FREE.
- The sequencer, pattern bank, preset and 1018 bit-exact dual-core tests pass.
- Device build/compiler checks pass: image **411524 B** (+368), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `69cc0b14ac9e33155a16a9ea31346978dec64013852df202c0e087a7f9cc05d7`.

Not yet installed on the FM-1.
