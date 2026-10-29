# ENV follow-up: repeated notes and six-op release

The first `0.14.1-FIX` enabled the outer volume envelope for six-op FM, but only tested attacks
starting from silence. It missed two interactions:

- `envelope_note_on` retained a preceding release's nonzero level. Replaying the same note therefore
  started a nominally long attack near its previous volume, even if the DX patch had already become quiet.
- Key-off also released the DX patch's own gates. A short native release could end its output while
  the app's longer release was still running.

`0.14.1-ENV2` restarts a nonzero attack from zero after a 2 ms fade of the preceding level. Release
rate changes also apply while a release is in progress. For six-op FM, the native gate stays held
through the app's release; once the outer level is zero, the native voice and FIFO are reset before
sleeping. This prevents a deferred old DX release from returning with the next note. The allocated
patch bank is retained, avoiding a full 32-patch unpack on each note.

## Device capture

Captured the **Terykons FM-1 USB audio input**, not a microphone. Both controlled recordings use
6-OP FM 2, TIMBRE 65, MORPH 50, HARMONICS 75, C4 at velocity 100, MONO, FX/FREEZE/LFO off, DECAY 0,
master at 50%. Three short repeated notes use ATTACK 100 / RELEASE 100. No SAVE was pressed.
The screen-reader HELLO confirmed the second recording ran `Terykons 0.14.1-ENV2`.

Approximate early-note RMS, relative to ATTACK 0 on the same recording:

| Case | First fix | ENV2 |
| --- | ---: | ---: |
| ATTACK 100 from silence | -17.2 dB | -18.2 dB |
| First short repeat | -5.4 dB | -20.5 dB |
| Second short repeat | -1.5 dB | -19.5 dB |
| Third short repeat | +1.0 dB | -20.1 dB |

Windows cover approximately 50–150 ms after note-on, aligning the MIDI event log to PCM using the
two zero-release note endings. Values are measurements of this controlled case, not a claim that
every DX patch has the same amplitude response. A naturally decayed carrier still cannot be restored
by extending the outer release. Sustain remains fixed at 70%.

Local recordings and event logs: `build/env-hardware/baseline-controlled-0.14.1.wav` and
`after-controlled-env2.wav`, with the corresponding `.json` files. Reproduce with
`python terykons/tests/hardware_env.py --out PATH.wav` (requires mido/python-rtmidi and ffmpeg).
The script temporarily selects the patch/controls and records only the explicitly named FM-1 input.

## Regression checks

The extended audio test failed against the first fix: one tested bank retained only 0.439 of held-note
energy after release, and slow repeated attacks had 1.017 times the fast repeated-attack energy.
After ENV2, the three banks' release/held energy ratios were 0.932 / 0.937 / 0.929 and their slow/fast
repeat ratios were 0.000571 / 0.001178 / 0.000686. Separate checks cover live release adjustment.
All six simulator scripts and the 768 concurrent DSP comparisons passed.

The device build passed code/memory checks: image 329108 bytes, RAM 68180 / 98304 bytes,
pool 332776 / 344064 bytes. The device listening check by the player remains separate from these
recorded signal and state measurements.

A four-note device check used both cores (masks 5 / 10); after key-off all four voices finished the
1.2 s outer release and became idle. CPU1 errors, rejected jobs, xruns and shed voices stayed at zero.
The lifetime OVER counter included one earlier engine-setup peak while no voice was cut. The crash
log reported no crash recorded. After testing, the panel was left on ENV, four voices, 10/50/30,
with FX and LFO depth off; none of the test settings were saved.
