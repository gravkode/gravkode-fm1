# GRAV CODE

GRAV CODE turns the M-VAVE FM-1 into a polyphonic synthesizer on all 24 sound engines of Mutable Instruments'
Plaits: up to four voices, each its own engine, played from the keys with velocity or over MIDI, shaped by an
envelope and moved by an LFO. Everything is on the FM-1's own panel and screen; the settings stay across a power
cycle.

![GRAV CODE playing a chord on CHORDS: three keys held, each filled to its velocity (amber) and its pressure (white), TIMBRE 65, MORPH 50, HARMONICS 75](img/screen-playing.png "A chord on CHORDS")

---

## Contents

1. Install
2. The screen
3. Controls
4. The pages
5. The engines
6. Voices and velocity
7. Saving
8. MIDI and USB audio
9. CPU and its limits
10. Troubleshooting
11. Credits and licences

---

## 1. Install

**Try it first:** the emulator on the site runs the same code in the browser, with sound, played with the mouse, a
touch screen or the computer keyboard.

**Install from the browser:** in Chrome or Edge, connect the FM-1 by USB (a data cable), open the installer and
press Install. The browser asks once to allow MIDI: allow it. Don't unplug the cable while it writes; the FM-1
restarts into GRAV CODE when it is done. Coming from another firmware, leave it running as it is: no update mode is
needed.

**Install from a terminal:** `python3 tools/fm1_install.py grav_code-VERSION.fwsc` (it needs Python's `mido` and
`python-rtmidi`).

**Back to the stock firmware:** the installer's last section writes M-VAVE's own firmware back. Installing is at
your own risk.

---

## 2. The screen

![The OSC page at rest: GRAV CODE and the page's name at the top, the battery at the right; WAVESHAPE; TIMBRE 65, MORPH 50, HARMONICS 75 with their bars; the keyboard; OCT +0, PRESET WAVESHAPE, VOL 50%, CPU 0%](img/screen-osc.png "The OSC page")

From the top:

- **The header:** GRAV CODE, the page you are on (OSC, MOD, ENV or GLO), and the battery: three bars, green when
  full, yellow, then red; a lightning bolt while the FM-1 runs on USB power.
- **The engine** that plays, by its name.
- **The page's three parameters**, each with its value (0 to 100) and a bar: K1, K2 and K3 change them.
- **The keyboard:** a key held fills to its velocity (amber) and, pressed further, to its pressure (white).
- **The bottom lines:** OCT (the octave), PRESET (the engine), VOL (MASTER), and when MIDI has come in, the last
  message and where from (USB or TRS). CPU is the share of its time the sound takes: it turns red once a block of
  sound came late (heard as a crackle), until the next power-on.

---

## 3. Controls

| Control | What it does |
| --- | --- |
| Keys | Play, with velocity (or a fixed one: GLO). 27 keys, F3 to G5 at OCT +0 |
| OCT- / OCT+ | The octave, -2 to +2. Keys held keep their notes until they are let go |
| PRESET (knob) | The engine: all 24 in Plaits' order, round from the last to the first |
| K1, K2, K3 | The page's three parameters (the GLO page: its settings) |
| HOME | The OSC page: TIMBRE, MORPH, HARMONICS |
| LFO | The MOD page: LFO RATE, LFO DEPTH, LFO DEST |
| ENV | The ENV page: ATTACK, DECAY, RELEASE |
| GLO | The GLO page: VOICES, VELOCITY, the version |
| MASTER | The volume, glided: no step in the sound |
| SAVE | Stores the settings; lit while there are changes not stored |

The other buttons (FX, SEL, EDIT, ARP, SEQ, PLAY, REC) and knobs (SELECT, ALGO, K4) only say their name or count for
now: they are free for what comes next.

**At power-on:** OCT- and OCT+ held is the panel's setup (the knobs' and keys' calibration); REC held starts the
chip's update mode, a way to install when GRAV CODE starts but its USB can't be reached.

---

## 4. The pages

### OSC (HOME)

The engine's own three parameters, as on Plaits: **TIMBRE** (K1), **MORPH** (K2) and **HARMONICS** (K3). What each
does depends on the engine: section 5. They start at 65, 50 and 75.

### MOD (LFO)

![The MOD page: LFO RATE 50, LFO DEPTH 40, LFO DEST 0](img/screen-mod.png "The MOD page, the LFO's depth up")

One sine LFO for all the voices, free-running (a note doesn't restart it):

| Knob | Parameter | Range |
| --- | --- | --- |
| K1 | LFO RATE | 0.05 Hz to 20 Hz (50: 10 Hz) |
| K2 | LFO DEPTH | 0 (off) to 100 |
| K3 | LFO DEST | 0 to 33: TIMBRE; 34 to 67: MORPH; 68 to 100: PITCH |

At full depth it sweeps TIMBRE or MORPH by half their range either side of the knob, or the pitch by 2 semitones
either side. It starts at RATE 50, DEPTH 0 (off).

### ENV (ENV)

![The ENV page: ATTACK 10, DECAY 50, RELEASE 30](img/screen-env.png "The ENV page")

Each voice's envelope, which shapes its level:

| Knob | Parameter | Range |
| --- | --- | --- |
| K1 | ATTACK | 0 to 2 s |
| K2 | DECAY | 0 to 2 s, down to a sustain of 70 % |
| K3 | RELEASE | 0 to 4 s, after the key is let go |

It starts at ATTACK 10 (0.2 s), DECAY 50 (1 s) and RELEASE 30 (1.2 s).

> The engines with an envelope of their own play through it, as Plaits' low-pass gate lets them: BASS DRUM, SNARE,
> HI-HAT, the three 6-OP FM banks (each patch's envelopes), and SPEECH while it speaks a word.

### GLO (GLO)

![The GLO page: VOICES 4, VELOCITY ON, version 0.12.0, ID FM-1_73001200, 24 engines](img/screen-glo.png "The GLO page")

The firmware and its global settings:

| Knob | Setting | Values |
| --- | --- | --- |
| K1 | VOICES | MONO, 2, 3, 4 (the default) |
| K2 | VELOCITY | ON (right: the keys play as they land), OFF (left: at a fixed 100) |
| K3 | VERSION | the firmware's version, said aloud |

Under them, the package's identity (the installer's) and the number of engines. Section 6 has more on both settings.

---

## 5. The engines

PRESET steps through all 24 engines of Plaits, in Plaits' own order: its first sixteen, then the eight its firmware
1.2 added. K1 is TIMBRE, K2 MORPH, K3 HARMONICS; what they do, engine by engine:

| # | Engine | TIMBRE (K1) | MORPH (K2) | HARMONICS (K3) |
| --- | --- | --- | --- | --- |
| 1 | ANALOG | Square: narrow pulse, square, then hard-sync formants | Saw: triangle to saw, then a widening notch | Detune between the two waves |
| 2 | WAVESHAPE | Wavefolding | Asymmetry | The shaper's curve |
| 3 | FM 2-OP | Modulation index | Feedback: rough one way, chaotic the other | Frequency ratio |
| 4 | GRAIN | Formant frequency | Formant width and shape | Ratio between the two formants |
| 5 | ADDITIVE | Which harmonic is strongest | Shape of the bumps, wide to narrow | Number of bumps in the spectrum |
| 6 | WAVETABLE | Row in the table | Column | Bank: four smooth, then four stepped |
| 7 | CHORDS | Inversion and transposition | Waveform: string and organ, then a wavetable | Chord type |
| 8 | SPEECH | Species: the formants, from giant to chipmunk | Phoneme, or the part of the word | Vowels (formant, SAM, LPC), then banks of words |
| 9 | SWARM | Density of the grains | Grain length and overlap | Pitch randomness |
| 10 | NOISE | Clock frequency | Resonance | Filter: low-pass, band-pass, high-pass |
| 11 | PARTICLE | Density of the particles | Filter: an all-pass reverb, then resonant band-pass | Frequency randomness |
| 12 | STRING | Brightness of the excitation | Decay | Inharmonicity, the material |
| 13 | MODAL | Brightness of the excitation | Decay | Inharmonicity, the material |
| 14 | BASS DRUM | Brightness | Decay | Attack sharpness and overdrive |
| 15 | SNARE | Balance of the drum's modes | Decay | Tone against noise |
| 16 | HI-HAT | High-pass cutoff | Decay | Metal against noise |
| 17 | VA VCF | Filter cutoff | Waveform and sub level | Resonance and character, smooth to harsh |
| 18 | PHASE DIST | Amount of distortion | Its asymmetry | Its frequency |
| 19 | 6-OP FM 1 | Brightness (the modulators' level) | Envelopes and modulation, stretched | One of 32 patches: basses and synths |
| 20 | 6-OP FM 2 | Brightness | Envelopes and modulation, stretched | One of 32 patches: keys, plucks, percussion |
| 21 | 6-OP FM 3 | Brightness | Envelopes and modulation, stretched | One of 32 patches: organs, pads, strings, brass |
| 22 | TERRAIN | Radius of the path | Offset of the path | One of eight terrains, blended |
| 23 | STRING MCH | Chorus and filter | Waveform | Chord |
| 24 | CHIPTUNE | Arpeggio, or the chord's inversion | Pulse width and sync | Chord |

### How a note plays each engine

Plaits has a trigger input; in GRAV CODE a note is the trigger, in three ways:

- **Held:** most engines simply sound for as long as the note is held, and the envelope shapes them.
- **Struck, then sounding on:** STRING and MODAL are struck when a note begins and kept ringing while it is held
  (as on Plaits with nothing in its trigger input), then ring out with the envelope's release.
- **A gate:** BASS DRUM, SNARE and HI-HAT are struck by each note and decay on their own; the 6-OP FM banks play
  their patch's envelopes, which release when the key is let go; SPEECH speaks a word with each note when HARMONICS
  is among the word banks; PARTICLE starts afresh with each note.

The velocity is also Plaits' accent: the engines that strike strike harder and brighter. Plaits' second output
(AUX) is not used. The engines run as on Plaits, 12 samples at a time, at the FM-1's 44.1 kHz.

### Levels

The engines were brought to about the same loudness at their defaults (measured: the level while a note is held,
a drum by its peak). Some change level with a knob by nature: SWARM, PARTICLE and NOISE with their density or
clock, MODAL with TIMBRE (its exciter thins to a sparse crackle below the default), the 6-OP FM patches from one to
the next.

---

## 6. Voices and velocity

![A chord on CHORDS: three keys, three voices](img/screen-playing.png "Three keys held, three voices")

**VOICES** (GLO, K1): up to four notes at once, each its own engine (POLY), or one (MONO).

- **POLY:** each note takes a voice: a free one first, else the one let go longest ago, else the oldest held. The
  same note played again takes its own voice back.
- **MONO:** one voice, the last note wins; let go of it and the voice goes back to the note still held, without a
  new attack (legato); let go of the last one and it releases.

Changing VOICES lets go of every note. All the voices add up through a soft limiter, so a loud chord bends rather
than clips.

**VELOCITY** (GLO, K2): ON, the keys play as they land; OFF, every key plays at 100, and sends 100 over MIDI too.
MIDI coming in keeps its own velocity either way. The velocity sets a voice's level and how hard it strikes.

The keys' pressure goes out over MIDI as polyphonic aftertouch; it doesn't change the sound (yet).

---

## 7. Saving

SAVE stores the octave, the engine, the OSC, MOD and ENV values, VOICES and VELOCITY; GRAV CODE starts with them at
the next power-on. SAVE is lit while something has changed and isn't stored yet. A firmware update keeps them.

---

## 8. MIDI and USB audio

**MIDI in,** over USB and the TRS jack, any channel: notes play the voices, with their velocity. The last message
shows at the bottom of the screen.

**MIDI out,** over USB, channel 1: the keys' notes, their velocity and their pressure (polyphonic aftertouch).

**USB audio:** the FM-1 is also a USB sound card: a computer can record GRAV CODE, and play its own sound through
the FM-1's speaker and headphones, at MASTER.

GRAV CODE has no Bluetooth or WiFi.

---

## 9. CPU and its limits

CPU (bottom right) is the share of its time the sound takes. Four voices of the heaviest engines cost the most:
FM 2-OP, PARTICLE, then the drums, STRING and MODAL. Should the sound ever take over 90 % of its time, GRAV CODE
silences the oldest voice sounding rather than letting the sound break up (MONO keeps its one voice). A voice let go
and silent costs nothing.

If CPU turns red (a block came late): fewer VOICES, or a lighter engine.

---

## 10. Troubleshooting

**No sound:** turn MASTER up; some engines are quiet by their settings (MODAL and SWARM with TIMBRE low, NOISE and
PARTICLE thin); a drum or a 6-OP FM patch may simply have decayed.

**A crackle:** see section 9.

**The installer doesn't find the FM-1:** a data cable, other MIDI apps and browser tabs closed, Chrome or Edge.
Started but out of reach over USB: hold REC while switching on (update mode), then install.

**The knobs or keys read wrong:** hold OCT- and OCT+ while switching on, for the panel's setup.

---

## 11. Credits and licences

- GRAV CODE is built on **fm1-base** (Charles Vestal) and **felucca-bsp** (Leo Kuroshita, Hügelton Instruments),
  MPL-2.0.
- Its sound engines are **Mutable Instruments Plaits** and its library **stmlib**, by Émilie Gillet, under the MIT
  License; the notice is in the source (`grav-code/vendor/plaits/LICENSE`) and on the site's front page.
- The fonts are Barlow (SIL Open Font License).

M-VAVE and FM-1 are trademarks of their owners. GRAV CODE is not affiliated with M-VAVE or Mutable Instruments.
