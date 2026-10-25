# Other people's code in GRAV CODE

| Directory | What | From | Licence | GRAV CODE's changes |
| --- | --- | --- | --- | --- |
| `plaits/` | Mutable Instruments Plaits: every engine (`dsp/`) and all its tables (`resources.cc`) | [pichenettes/eurorack](https://github.com/pichenettes/eurorack) | MIT (`plaits/LICENSE`) | `dsp/dsp.h`: the FM-1's 44.1 kHz sample rate, marked there |
| `stmlib/` | The parts of Mutable Instruments' stmlib that Plaits uses: 14 files of `dsp/` and `utils/` | [pichenettes/eurorack](https://github.com/pichenettes/eurorack) | MIT (`stmlib/LICENSE`) | none; the rest of stmlib (its STM32 libraries, linker scripts, boot loaders) is left out |
| `cxx/` | libc++ 12's headers, as the JieLi AC79 SDK ships them (the toolchain's C++ compiler has none) | the AC79 SDK (`include_lib/c++/include`), from the LLVM project | Apache-2.0 WITH LLVM-exception | `__config`: no threads (marked there) |
| `newlib/` | newlib's C library headers, as the AC79 SDK ships them | the AC79 SDK (`include_lib/newlib`) | newlib's (BSD-style, per file) | none |

The firmware is built from these headers and from `plaits/` and `stmlib/`'s `.cc` files (`fm1.toml`'s `o2`);
`tools/build.py` treats the whole directory as vendored (`fm1.toml` `vendored`).
