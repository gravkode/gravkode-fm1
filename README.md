# GRAV CODE

**A polyphonic synthesizer firmware for the M-VAVE FM-1, on all 24 sound engines of Mutable Instruments' Plaits,
through Mutable Instruments' Beads.**

Up to four voices, each its own engine, played from the FM-1's keys with velocity, over MIDI or by a 64-step
sequencer, shaped by an envelope, moved by an LFO and played through Beads' grains, delay and reverb, all on the
FM-1's own panel and screen.

<p align="center"><img src="grav-code/docs/img/readme-playing.png" width="480" alt="GRAV CODE on the FM-1's screen: a chord on CHORDS, three keys held, each filled to its velocity and pressure"></p>

[Русская версия](README.ru.md) · [The manual](grav-code/docs/MANUAL.md)

---

Current version: **0.18.0** — sixteen patterns, picked with HOME and a white key, K4 or a MIDI program change, and
switched at the pattern's end without a gap; each saved by itself into the flash. The tempo shows at the top.

## What it does

- **24 engines from Plaits:** virtual analog, waveshaping, 2-operator FM, granular formants, additive, wavetables,
  chords, speech, swarms, filtered noise, particles, inharmonic strings, modal resonators, bass drum, snare, hi-hat,
  and the eight of Plaits 1.2: virtual analog with a filter, phase distortion, three banks of 6-operator FM (DX7
  patches), wave terrains, a string machine and chiptune. Each with Plaits' own TIMBRE, MORPH and HARMONICS.
- **An effect from Beads:** its granular processor (DENSITY, TIME, SIZE, SHAPE, PITCH, FEEDBACK), its delay, its four
  recording qualities (digital to dirty tape) and its reverb, with FREEZE and SEED on the panel's buttons.
- **A sequencer of eight tracks:** four synth tracks (S1–S4), each its own engine and sound, voices (the four shared
  out: MONO, POLY or none), and four drum tracks (D1–D4), each a bass drum, snare or hi-hat of Plaits' in a voice of
  its own. Each its steps: 64 in four pages of 16, played on the white keys like a TR, a length of 1 to 64 each
  (polymeters), notes and chords with their velocity and length; sixteen patterns, switched at the pattern's end;
  the song's tempo, each pattern's swing; step recording, MIDI clock out and in (it follows one); each track's notes
  on its MIDI channel (1–8).
- **Polyphony:** up to four voices (POLY), or one with legato (MONO).
- **Velocity** from the FM-1's keys (or a fixed one), pressure sent as polyphonic aftertouch.
- **An envelope** (attack, decay, release) and **an LFO** to TIMBRE, MORPH or pitch.
- **Settings that stay** across a power cycle and a firmware update.
- **MIDI** in over USB and TRS, out over USB; the FM-1 is also a **USB sound card**.
- **Two-core voice rendering** and a **DEBUG page** showing average/peak load, worker timing, audio xruns and voice cuts.
- **A web installer** (Chrome or Edge), and the stock firmware back from the same page.

## The screen and its pages

<p align="center"><img src="grav-code/docs/img/readme-pages.png" width="488" alt="Eight pages: SEQ (the steps playing, TEMPO, SWING, LENGTH), OSC (TIMBRE, MORPH, HARMONICS), MOD (LFO RATE, DEPTH, DEST), ENV (ATTACK, DECAY, RELEASE), FX GRAIN (DENSITY, TIME, SIZE, frozen), FX MIX (DRY/WET, REVERB, QUALITY), GLO (VOICES, VELOCITY, the version) and DEBUG"></p>

| Button | Page | K1 | K2 | K3 |
| --- | --- | --- | --- | --- |
| HOME | SEQ | TEMPO (30 to 300 BPM) | SWING (50 to 75 %) | LENGTH (1 to 64 steps) |
| HOME again | OSC | TIMBRE | MORPH | HARMONICS |
| LFO | MOD | LFO RATE (0.05 to 20 Hz) | LFO DEPTH | LFO DEST (TIMBRE, MORPH, PITCH) |
| ENV | ENV | ATTACK (0 to 2 s) | DECAY (0 to 2 s, to 70 %) | RELEASE (0 to 4 s) |
| GLO | GLO | VOICES, the track's (OFF, MONO, 2, 3, 4; four in all) | VELOCITY (ON, OFF) | the version |
| FX | FX GRAIN | DENSITY (none at the middle: clocked left, random right) | TIME (how far back) | SIZE (backwards, forwards, at the end the delay) |
| FX again | FX SHAPE | SHAPE (the grains' envelope) | PITCH (-24 to +24 semitones) | FEEDBACK |
| FX again | FX MIX | DRY/WET | REVERB | QUALITY (DIGITAL, VINTAGE, CLEAN TAPE, DIRTY TAPE) |
| GLO again | DEBUG | one/two cores | reset peaks/counters | — |

SELECT picks the track (S1–S4, D1–D4) the keys, PRESET, OSC, VOICES and the steps are for; PRESET picks its
engine (on a drum track: bass drum, snare, hi-hat), OCT- and OCT+ the octave (-2 to +2), MASTER the volume, SAVE
stores it all. SEQ turns step mode on (the white keys are the steps; hold one and turn K1..K3 for its note,
velocity, length; the black keys 1–8 the tracks; OCT- / OCT+ the pages; SEQ held and a step: the length), PLAY
starts and stops, REC (stopped) records notes and chords step by step. HOME held and a white key, or K4, picks the
pattern (playing: at the pattern's end). The open page's button blinks, faster on its next page; PLAY flashes green on the beat. EDIT freezes
the effect's recording (FREEZE); SEL seeds a grain by hand, and held 2 s lets the grains come on their own again
(SEED). The effect is off with DRY/WET and REVERB at 0, and costs nothing then.

DEBUG keeps two-core mode on by default. Its controls are temporary and are not saved. Heavy FX settings can
still trigger voice cuts above 90% of an audio block; the smoothed CPU meter can hide short peaks. See
[the measured results](grav-code/docs/DUAL_CORE_RESULTS.md) and [the debug guide](grav-code/docs/DUAL_CORE_DEBUG.md).

## The engines

K1 TIMBRE, K2 MORPH, K3 HARMONICS; in PRESET's order (Plaits' own):

| # | Engine | TIMBRE | MORPH | HARMONICS |
| --- | --- | --- | --- | --- |
| 1 | ANALOG | Pulse to square to sync | Triangle to saw to notch | Detune |
| 2 | WAVESHAPE | Folding | Asymmetry | Shaper curve |
| 3 | FM 2-OP | Index | Feedback | Ratio |
| 4 | GRAIN | Formant | Formant width | Formant ratio |
| 5 | ADDITIVE | Strongest harmonic | Bump shape | Number of bumps |
| 6 | WAVETABLE | Row | Column | Bank |
| 7 | CHORDS | Inversion | Waveform | Chord |
| 8 | SPEECH | Species | Phoneme or word part | Vowels, then words |
| 9 | SWARM | Density | Grain length | Pitch randomness |
| 10 | NOISE | Clock | Resonance | Low, band, high-pass |
| 11 | PARTICLE | Density | Reverb to band-pass | Randomness |
| 12 | STRING | Excitation | Decay | Material |
| 13 | MODAL | Excitation | Decay | Material |
| 14 | BASS DRUM | Brightness | Decay | Attack, overdrive |
| 15 | SNARE | Drum modes | Decay | Tone against noise |
| 16 | HI-HAT | High-pass | Decay | Metal against noise |
| 17 | VA VCF | Cutoff | Waveform, sub | Resonance |
| 18 | PHASE DIST | Amount | Asymmetry | Frequency |
| 19 | 6-OP FM 1 | Brightness | Envelope stretch | Patch: basses, synths |
| 20 | 6-OP FM 2 | Brightness | Envelope stretch | Patch: keys, plucks, percussion |
| 21 | 6-OP FM 3 | Brightness | Envelope stretch | Patch: organs, pads, strings, brass |
| 22 | TERRAIN | Path radius | Path offset | Terrain |
| 23 | STRING MCH | Chorus, filter | Waveform | Chord |
| 24 | CHIPTUNE | Arpeggio | Pulse width, sync | Chord |

A note plays an engine as Plaits' trigger would: most engines sound while the note is held; STRING and MODAL are
struck, then keep sounding; the drums, the 6-OP FM banks, SPEECH's words and PARTICLE take each note as a gate. The
manual has the details: [grav-code/docs/MANUAL.md](grav-code/docs/MANUAL.md).

## Install

1. Build or download the package, `grav_code-VERSION.fwsc`.
2. Make the site and serve it (or use the published one):

   ```bash
   python3 tools/make_site.py grav-code grav-code/build/grav_code-0.18.0.fwsc 0.18.0 grav-code/build/site
   python3 -m http.server 8080 --directory grav-code/build/site
   ```

3. In Chrome or Edge, open `http://localhost:8080/install/`, connect the FM-1 by USB and press Install.

The same page writes M-VAVE's stock firmware back. Installing is at your own risk.

## Build

GRAV CODE builds with fm1-base's `tools/build.py`: JieLi's toolchain (Linux x86-64; on macOS it runs in Docker) and
JieLi's AC79 SDK.

```bash
export JIELI_TOOLCHAIN="$HOME/.jieli/toolchain" AC79_SDK="$HOME/fw-AC79_AIoT_SDK"
python3 tools/build.py grav-code --release 0.18.0      # grav-code/build/grav_code-0.18.0.fwsc
python3 tools/build.py grav-code --host                # the simulator, for the tests:
grav-code/build/host/grav_code_host grav-code/tests/basic.txt grav-code/build/host
python3 tools/build.py grav-code --web                 # the browser emulator (Emscripten)
```

The tests (`grav-code/tests/`): `basic.txt` (keys, octave, saving, MIDI), `engines.txt` (every engine plays),
`voices.txt` (polyphony, MONO, velocity), `fx.txt` (the effect: its pages, FREEZE, SEED, QUALITY, saving),
`dual_core.txt` (DEBUG and worker scheduling), `seq.txt` (the sequencer), `tracks.txt` (the synth tracks), `drums.txt` (the drum tracks), `patterns.txt` (the patterns), `seq_test.c` (its timing, to the sample, and
`bank_test.c`, the patterns' flash: `run_seq_test.py`), `screens.txt` (the screenshots). More in
[grav-code/README.md](grav-code/README.md).

## The repository

| Path | What |
| --- | --- |
| `grav-code/` | GRAV CODE: the app (`app.c`), its DSP (`dsp/`: Plaits' engines per voice, Beads, the mixer), its voices and modulation (`synth/`), the sequencer (`seq/`), Plaits, Beads and the other vendored code (`vendor/`), tests, docs, the site's page |
| `device/`, `include/`, `lib/`, `host/`, `web/`, `tools/` | fm1-base: the FM-1 platform, the simulator, the emulator, the installer, the build ([FM1-BASE.md](FM1-BASE.md)) |
| `bsp/` | felucca-bsp: the FM-1's chip and board support |
| `template/`, `tests/`, `ble/` | fm1-base's reference app and its platform tests, its Bluetooth stack (not built into GRAV CODE) |

## Credits and licences

- Built on **fm1-base** by Charles Vestal and **felucca-bsp** by Leo Kuroshita (Hügelton Instruments), MPL-2.0;
  see [CREDITS.md](CREDITS.md) and [LICENSING.md](LICENSING.md).
- Sound engines: **Mutable Instruments Plaits**; effect: **Mutable Instruments Beads**; their library **stmlib**; by
  Émilie Gillet, MIT License ([grav-code/vendor/plaits/LICENSE](grav-code/vendor/plaits/LICENSE),
  [grav-code/vendor/beads/LICENSE](grav-code/vendor/beads/LICENSE)); what else is vendored and under which licence:
  [grav-code/vendor/README.md](grav-code/vendor/README.md).
- GRAV CODE's own code: MPL-2.0.

M-VAVE and FM-1 are trademarks of their owners. GRAV CODE is not affiliated with M-VAVE or Mutable Instruments.
