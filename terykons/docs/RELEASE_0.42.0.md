# Terykons 0.42.0 — multitrack over USB

Package: `terykons-0.42.0.fwsc`, identity `FM-1_73004200`. After 0.41.1 (`2b66eda`: simpler chains).

![The USB page: AUDIO MULTI, each track's input on the computer above it](img/screen-usb.png)

**GLO again: USB.** GLO's round is now GLO, MIDI, USB, SCREEN, DEBUG. The USB page's K1 AUDIO says what a computer
gets over the cable:

- **STEREO** (as it comes, as before): the mix on the computer's inputs 1-2 (at MASTER), and the computer's sound
  through the FM-1's speaker and headphones.
- **MULTI**: the mix on inputs 1-2, and each of the eight tracks on an input of its own: S1 to S4 on 3 to 6, D1 to D4
  on 7 to 10. Recorded into a DAW (Ableton Live, say), every track is a track of its own. A track's input is its
  voices after its fader (VOL), mono, without the effect (that stays on 1-2), and at the same level whatever MASTER
  says. In MULTI the computer plays nothing through the FM-1: ten channels fill most of what the FM-1's full-speed
  USB carries.

A turn reconnects the FM-1 to the computer (about a second), with its new inputs. Above the knob every track with its
input (STEREO: a red dash); under it what 1-2 carry. Saved with the settings (SAVE; a byte reserved since 0.39, so the
settings keep their size); older settings are STEREO.

16-bit, 44.1 kHz, no driver: the FM-1 presents itself in MULTI as a class-compliant USB audio device with a 10-channel
input (bcdDevice 3.90, so a computer never takes it for the stereo one). Each packet carries 920 bytes of a full-speed
frame's 1023; it is on the wire for most of each millisecond, so in MULTI the next packet is queued at 10 kHz (at the
2 kHz that suffices for stereo, some missed their frame and macOS's stream broke: seen on the bench).

Checked on an FM-1 with macOS, recording all ten inputs as the driver hands them over (`tools/uac_record.swift`):

- a bench build (`FM1B_UAC_SAW=1`: sawtooths on the tracks' inputs, each frame a value of its own, through the same
  ring and packets as the tracks) recorded 20 s stopped and 20 s playing: every frame of the eight in order, none
  lost, repeated or changed (`tools/uac_check.py`), and no gap reported by the driver;
- this build: a note held on S1 came in on input 3 alone, two hits of D1 on 7, one of D2 on 8; the other tracks'
  inputs silent; the mix on 1-2 at MASTER.

Not yet tried: Windows, an iPad. Not measured: what the 10 kHz polling takes of the first core (in the timer's
interrupt; the DEBUG page's CPU is the render's).

fm1-base: `usb_audio_tracks = N` in fm1.toml (`FM1B_UAC_CH` = N + 2, up to 8 tracks) builds MULTI in; the app switches
it with `plat_usb_audio_multi` and fills `plat_usb_tracks()` in `app_render` (bsp/chip/usb.c: the descriptors, the
tracks' ring beside the master's, `usb_uac_switch`). RAM 73728 → 86948 B (the tracks' ring, 8 KB; a render's tracks,
4 KB).

Validation on 30 October 2026: all 27 simulator scenarios (new: `usb.txt`: GLO's round, K1 STEREO and MULTI, a key's
note on its track's input alone, a drum's on its own, a fader at 0, saving, settings from 0.38; five scenarios that
crossed GLO's pages now pass USB), the screenshot script (`screen-usb.png` new), the dual-core, sequencer, pattern
bank and preset tests, fm1-base's platform tests (its template's Bluetooth expectations fail as they do on 0.41.1).
Device build/compiler checks pass: image **401520 B**, RAM **86948 / 98304 B**, pool **332968 / 344064 B**; package
**611496 B**.

Package SHA-256: `8d06de5ab1ab933248eb6ed6a8160f49ec54423af38a68128f69c55b80f8ea4a`.

Installed on the FM-1 (and checked there, above).
