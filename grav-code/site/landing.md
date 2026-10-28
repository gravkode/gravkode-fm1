# GRAV CODE

GRAV CODE turns the M-VAVE FM-1 into a synthesizer on all of Mutable Instruments Plaits' 24 sound engines: virtual
analog, waveshaping, FM and 6-operator FM, grains, additive, wavetables, chords, speech, swarms, noise, particles,
strings and modal resonators, drums, wave terrains, a string machine and chiptune, up to four voices at once (or
one, MONO). The keys play it with velocity (or a fixed one); an envelope shapes it and an LFO moves its timbre,
morph or pitch; Mutable Instruments Beads, its granular processor, delay, tape and reverb, is its effect; a sequencer
of eight tracks, four synth tracks each its own engine and four drum tracks, 64 steps on the white keys and sixteen
patterns, plays it, with swing and MIDI clock; the settings and the pattern save; MIDI goes out
and comes in over USB and TRS. ALGO chooses the engine, PRESET its sound: four factory starting points per
engine and 64 user slots, saved with held SAVE and a white key. The sound editor (0.22) adds independent engine
controls, all six FM operators, small animated knobs and extended preset storage; HOME advances pages, held
HOME with SELECT jumps between them. The sequencer's screen (0.23) shows the selected track as a piano roll on
HOME, and all eight tracks as a grid in step mode, both moving as they play. Live recording (0.24): REC while
playing records the keys (and a MIDI keyboard) into the selected track, quantized, with a metronome and a count-in;
each track has its own swing (0.25). SEL opens a mixer: each track's level, mute, solo and send to the effect (0.26). PRESET browses every sound, engine after engine (0.27). HOME shows every track as a terricon on a ridge, moving with its sound (0.28), under a sky by day or by night (0.29).

![GRAV CODE on the FM-1's screen: a chord on CHORDS, its sound page with its knobs](manual/img/screen-playing.png "A chord on CHORDS")

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): the screen, the controls, the pages, the sequencer, all 24 engines, the effect, voices, MIDI.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/gravkode/gravkode-fm1): MPL-2.0. Built on fm1-base by Charles Vestal and felucca-bsp,
  from Felucca by Leo Kuroshita, Hügelton Instruments.

## Credits and licences

The sound engines are Mutable Instruments Plaits', the effect is Mutable Instruments Beads', and their library is
stmlib, all by Emilie Gillet ([source](https://github.com/pichenettes/eurorack)), used under the MIT License:

```
Copyright 2012-2021 Emilie Gillet.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

M-VAVE and FM-1 are trademarks of their owners. GRAV CODE is not affiliated with M-VAVE or Mutable Instruments.
