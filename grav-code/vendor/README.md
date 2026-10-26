# Other people's code in GRAV CODE

| Directory | What | From | Licence | GRAV CODE's changes |
| --- | --- | --- | --- | --- |
| `plaits/` | Mutable Instruments Plaits: every engine (`dsp/`) and all its tables (`resources.cc`) | [pichenettes/eurorack](https://github.com/pichenettes/eurorack) | MIT (`plaits/LICENSE`) | `dsp/dsp.h`: the FM-1's 44.1 kHz sample rate; `dsp/engine2/six_op_engine.cc`: reset held gates and scratch buffers after the app's outer release; changes marked there; 0.22: the engines' and physical models' Render paths take the sound editor's independent controls (`dsp/engine/engine.h` GravExtra: 255, AUTO, keeps Plaits' value; a sound left alone renders as on Plaits, checked sample for sample), and the 6-OP engine an edited patch; each file marked "GRAV CODE"; the controls' names and ranges in `../sound/controls.c` |
| `beads/` | Mutable Instruments Beads' DSP (`dsp/`: the granular processor, the delay, the reverb, the tape, the limiters) and its tables (`resources.cc`); not its synth mode, drivers or UI | [pichenettes/eurorack](https://github.com/pichenettes/eurorack) | MIT (`beads/LICENSE`) | `dsp/processor.h`, `dsp/processor.cc`: the FM-1's 44.1 kHz and a 96,000-byte delay; `dsp/delay_line.h`: one 96 KiB bank; each marked there |
| `stmlib/` | The parts of Mutable Instruments' stmlib that Plaits and Beads use: 18 files of `dsp/` and `utils/` | [pichenettes/eurorack](https://github.com/pichenettes/eurorack) | MIT (`stmlib/LICENSE`) | `utils/random.h`, `random.cc`: separate random state per FM-1 core for concurrent DSP; the rest of stmlib (its STM32 libraries, linker scripts, boot loaders) is left out |
| `cxx/` | libc++ 12's headers, as the JieLi AC79 SDK ships them (the toolchain's C++ compiler has none) | the AC79 SDK (`include_lib/c++/include`), from the LLVM project | Apache-2.0 WITH LLVM-exception | `__config`: no threads (marked there) |
| `newlib/` | newlib's C library headers, as the AC79 SDK ships them | the AC79 SDK (`include_lib/newlib`) | newlib's (BSD-style, per file) | none |

The firmware is built from these headers and from `plaits/`, `beads/` and `stmlib/`'s `.cc` files (`fm1.toml`'s `o2`);
`tools/build.py` treats the whole directory as vendored (`fm1.toml` `vendored`).
