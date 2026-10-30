# Terykons 0.42.1 — MULTI for a DAW

Package: `terykons-0.42.1.fwsc`, identity `FM-1_73004201`. After 0.42.0 (`5af815e`: multitrack over USB).

0.42.0's MULTI worked on the cable (every frame came in) but not in Ableton: recordings broke every few seconds, and
with an output device at 48 kHz Ableton would not start. Two causes, both fixed for MULTI:

- **The clock.** The FM-1's sound runs on its own crystal: about **44,119.7 Hz** by a Mac's clock (0.045 % fast).
  0.42.0 sent it so, while the Mac's own devices (and the FM-1's output, which the computer paces) run on the
  computer's clock: a DAW recording from the FM-1 and playing through another device saw the two drift apart, ~20
  frames a second, and broke the recording to catch up. MULTI now converts all ten channels to the computer's clock
  on the FM-1 (an 8-tap windowed-sinc resampler, its rate steered by its buffer), so the inputs come at exactly
  44,100 frames a second by the cable: measured on a Mac, input **44,099.92 Hz**, output **44,099.88 Hz**. The DAW
  can play through any device at 44.1 kHz (the Mac's speakers, headphones, the FM-1). Clean: its error stays 62 dB
  under a sine to 11 kHz (in time and in level, whatever the delay); the pitch is the same.
- **The buffer.** A render's frames reach the USB buffer only when it ends, and the stereo buffer (~2.9 ms) ran dry
  once a render took more than ~35 % of its time (Terykons with the effect on often does): a hold, a click in the
  recording. MULTI has its own, deeper buffer (~7 ms ahead): enough for a render taking nearly all its time.

Also in MULTI: the computer's sound through the FM-1 again, as in STEREO (0.42.0 had none). The inputs come ~7 ms
later than in STEREO. STEREO is unchanged, byte for byte on the cable.

The cost: the resampling takes ~0.9 ms of each 5.8 ms render (~16 % CPU), only while a computer records in MULTI.

In Ableton Live: Input Device Terykons FM-1, Output Device any at 44.1 kHz (an output at 48 kHz: set it to 44.1 in
macOS's Audio MIDI Setup, or choose the FM-1), Sample Rate 44100 (the manual, section 10).

Checked on an FM-1 with macOS (`tools/uac_record.swift`: the driver's frames as they are), a bench build
(`FM1B_UAC_SAW=1`: sawtooths on the tracks' channels, through the same resampler's ring and packets): 30 s recording
with the output playing, and 20 s with a pattern playing (render peak 3.2 of 5.8 ms): every frame of the eight in
order, no gap reported by the driver; the rates above. `tools/fm1_remote.py log` now shows MULTI's costs (`rs`: the
last render's resampling, us; `svc`, `n`: the USB polling's time and calls).

fm1-base: bsp/chip/usb.c's MULTI (FELUCCA_UAC_CH > 2): its descriptors keep the output (bcdDevice 3.D0; 0.42.0's
3.90), its own ring (`rm_ring`, 640 frames, the fill held near 320 at a render's start), `uac_tap_m` (in `.ram_text`;
an 8-tap Kaiser fractional delay in integers, 33 phases interpolated, a PI loop on the fill), `uac_packet_m` (the 44.1
pattern exactly). A new host test, `tests/uac_multi_test.c` (in `tests/run_tests.sh`): 30 simulated seconds, renders
taking up to 95 % of their time, the I2S off by up to 200 ppm: exactly 44,100 frames a second, nothing dropped or
repeated, sines on all ten channels clean.

Validation on 30 October 2026: all 27 simulator scenarios, the screenshot script, the dual-core, sequencer, pattern
bank and preset tests, fm1-base's platform tests (with `uac-multi`; its template's Bluetooth expectations fail as
on 0.41.1). Device build/compiler checks pass: image **403256 B**, RAM **92196 / 98304 B**, pool **332968 / 344064
B**; package **611496 B**.

Package SHA-256: `2abf31fdbd08f5bb65c1b65cac8d0b2ce5ab46e41c5ae403949eadb581d7e23f`.
