# Terykons 0.42.2 — MULTI at no cost to the sound

Package: `terykons-0.42.2.fwsc`, identity `FM-1_73004202`. After 0.42.1 (`29bcc2e`: MULTI for a DAW).

Recording a heavy patch in MULTI (6-OP FM voices, the reverb, 140 BPM), 0.42.1 broke up: the FM-1's renders ran to
6.3 ms of their 5.8 (87 blocks missed, 669 voices shed in a session). The patch was near the edge already, and 0.42.1
spent 16 % of the first core converting the ten channels to the computer's clock. And a drum came in distorted.
Three changes, all in MULTI (STEREO is as it was):

- **The second core does the work for the computer.** Each block's sound done, the resampling of its ten channels is
  handed to the second core, which otherwise sits idle until the next block; the first core goes straight on. The
  ten channels themselves are gathered where the effect is mixed (on the second core while the effect runs), not in
  a pass of their own. Measured on the FM-1 while a Mac records all ten channels: the first core's load 19 % idle,
  24 % recording (0.42.1: 62 to 70 %); the second core's job takes ~0.55 ms of each 5.8 ms block, off the first
  core's time.
- **A fixed level with headroom.** The mix on inputs 1-2 and the tracks on 3-10 come at half what a full MASTER gives
  the speaker (6 dB of headroom), whatever MASTER says. 0.42.0 sent the tracks at a full MASTER's level through the
  master's limiter: a drum with DRIVE hit it (the "overdrive" on D1), which at MASTER 37 % one never heard.
- **The FM-1 alone on them.** 1-2 carried the master output: the computer's own playback through the FM-1 included,
  so a DAW monitoring its inputs fed its sound back round (a stutter, an echo building). Now the ten channels carry
  the FM-1's own sound alone; the computer's playback and the metronome's click are heard, not sent.

Also: the input's USB endpoint is declared synchronous (it runs on the computer's clock since 0.42.1), so a Mac now
makes one device of the FM-1 in MULTI, Terykons FM-1 with 10 inputs and 2 outputs; 0.42.1 showed the input and the
output as two. A DAW can take both from the one.

In Ableton Live: Input Device Terykons FM-1, Output Device any at 44.1 kHz, Sample Rate 44100; Input Config 1/2
stereo, 3 to 10 mono (the manual, section 10).

Checked on an FM-1 with macOS (`tools/uac_record.swift`): a bench build (`FM1B_UAC_SAW=1`) recorded with the output
playing, every frame of the eight sawtooth channels in order, input 44,099.92 Hz, output 44,099.93, the job 0.57 ms
on the second core, nothing under- or overrun; a 1 kHz tone played through the FM-1 left inputs 1-2 silent; this
build: S1's note on input 3 at -25 dBFS (0.42.1: -18), D1's hits on 7 at -12 (0.42.1: -6), the mix on 1-2 at -12
with MASTER at 37 %, the other inputs silent; the first core 19 % idle, 24 % recording, no voice shed, no block
missed. The second core's job reads nothing from the flash (its disassembly checked).

fm1-base: `plat_usb_tracks()` now hands the app a buffer of all PLAT_USB_CH channels (the mix on 1-2 as the app
wants it there, then its tracks), which the app fills in `app_render` and hands back with `plat_usb_tracks_push()`;
the platform resamples it in `uac_rs_job` (bsp/chip/usb.c: in RAM, loading nothing from the flash, so it runs on the
second core while the main loop may have the flash off), its state on cache lines of its own, published with `csync`.
A channel silent through the block is passed as silence, unfiltered. The engine (dsp/grav_engine.c `effect_chunk`)
writes the ten channels at `GRAV_USB_GAIN` through the master's limiter. `fm1_remote.py log`: `rs` (the job's us),
`under`, `over`.

Validation on 30 October 2026: all 27 simulator scenarios (`usb.txt`: the mix on its channel too, `usb_mix`), the
screenshot script, the dual-core, sequencer, pattern bank and preset tests, fm1-base's platform tests (`uac-multi`:
a silent channel exact; its template's Bluetooth expectations fail as on 0.41.1). Device build/compiler checks pass:
image **403376 B**, RAM **93764 / 98304 B**, pool **332968 / 344064 B**; package **611496 B**.

Package SHA-256: `2721267daf83d6618288deb0f3d97a89d263e80fccd6b56a04b24cfe4d6af556`.
