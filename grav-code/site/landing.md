# GRAV CODE

GRAV CODE turns the M-VAVE FM-1 into a synthesizer on all of Mutable Instruments Plaits' 24 sound engines: virtual
analog, waveshaping, FM and 6-operator FM, grains, additive, wavetables, chords, speech, swarms, noise, particles,
strings and modal resonators, drums, wave terrains, a string machine and chiptune, up to four voices at once (or
one, MONO). The keys play it with velocity (or a fixed one); an envelope shapes it and an LFO moves its timbre,
morph or pitch; the settings save; MIDI goes out and comes in over USB and TRS.

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): the screen, the controls, saving, MIDI.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/gravkode/gravkode-fm1): MPL-2.0. Built on fm1-base by Charles Vestal and felucca-bsp,
  from Felucca by Leo Kuroshita, Hügelton Instruments.

## Credits and licences

The sound engines are Mutable Instruments Plaits' and its library stmlib, by Emilie Gillet
([source](https://github.com/pichenettes/eurorack)), used under the MIT License:

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
