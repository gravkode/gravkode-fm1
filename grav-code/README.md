# grav-code: the firmware

GRAV CODE's app, on fm1-base (the repository's root). What it is and does: the root [README.md](../README.md) and
the manual, [docs/MANUAL.md](docs/MANUAL.md).

| File | What |
| --- | --- |
| `app.h` | The app's settings (name, block size, the store's objects, APP_OVERLOAD) and its half of the contract with the platform |
| `app.c` | The app: the keys and MIDI into the voices, the pages (OSC, MOD, ENV, GLO, the three FX pages), FREEZE and SEED, the screen, the settings and SAVE |
| `synth/` | The voices (`synth.c`: POLY up to four, MONO with legato, the shedding), the envelope, the LFO, the event queue from the main loop to the audio interrupt |
| `dsp/grav_engine.c` | The render: each voice through its engine and envelope, the mix through the effect, MASTER, the soft limiter, sleeping voices |
| `dsp/grav_plaits.cpp` | Plaits' 24 engines: their table (names, trigger modes, levels), built in each voice, rendered 12 samples at a time |
| `dsp/grav_beads.cpp` | Beads' processor, the effect: built in place, run in mono 12 samples at a time, off (and free) at DRY/WET and REVERB 0, faded in and out |
| `dsp/grav_libc.c` | The C library functions the FM-1's link lacks (`fabsf`, `sqrtf`, `abs`, `memmove`) |
| `vendor/` | Plaits, Beads, stmlib, libc++ and newlib headers: [vendor/README.md](vendor/README.md) |
| `fm1.toml` | Build and site settings: name, identity, the DSP units, their flags, the site's tagline and credits |
| `words.tsv` | The screen reader's words |
| `tests/` | The simulator's scripts: `basic.txt`, `engines.txt`, `voices.txt`, `fx.txt`, and `screens.txt` (the manual's pictures) |
| `docs/` | The manual and its pictures (`docs/img/compose.py` makes the README's from them) |
| `site/landing.md` | The site's front page |

## Build, test, install

This branch enables a second-core audio worker and a temporary DEBUG page (REC).
K1 switches one/two-core processing; K2 clears peaks and counters. See
[the debug guide](docs/DUAL_CORE_DEBUG.md) for measurements, automated tests and the device checklist.

From the repository's root:

    export JIELI_TOOLCHAIN="$HOME/.jieli/toolchain" AC79_SDK="$HOME/fw-AC79_AIoT_SDK"
    python3 tools/build.py grav-code                     # grav-code/build/grav_code.fwsc (no radio: no Bluetooth, WiFi)
    python3 tools/build.py grav-code --host              # the simulator, then each script:
    grav-code/build/host/grav_code_host grav-code/tests/basic.txt grav-code/build/host
    grav-code/build/host/grav_code_host grav-code/tests/engines.txt grav-code/build/host
    grav-code/build/host/grav_code_host grav-code/tests/voices.txt grav-code/build/host
    grav-code/build/host/grav_code_host grav-code/tests/fx.txt grav-code/build/host
    python3 tools/build.py grav-code --web               # the browser emulator: grav-code/build/emu/
    python3 tools/fm1_install.py grav-code/build/grav_code.fwsc

The JieLi toolchain is Linux x86-64: on macOS each tool runs in Docker (Docker Desktop running). The build needs
Python with Pillow (the fonts).

## Release and site

    python3 tools/build.py grav-code --release 0.13.0    # grav-code/build/grav_code-0.13.0.fwsc, identity FM-1_73001300
    python3 tools/build.py grav-code --web
    python3 tools/make_site.py grav-code grav-code/build/grav_code-0.13.0.fwsc 0.13.0 grav-code/build/site

## The pictures

    python3 tools/build.py grav-code --host --release 0.13.0   # (so GLO shows the version and identity)
    grav-code/build/host/grav_code_host grav-code/tests/screens.txt grav-code/docs/img
    python3 grav-code/docs/img/compose.py

## Notes

- The FM-1 runs no C++ static constructors: a C++ object with a vtable can't be a static; `dsp/grav_plaits.cpp`
  builds the engines in place (placement new). Constant tables are fine.
- Plaits' engines run 12 samples at a time, always (the 6-op FM depends on it), through a FIFO per voice.
- The engines' RAM and each voice's slot are in the pool (`.pool`, PLAT_POOL): about 98 KB for four voices. Beads'
  delay (96 KiB) and reverb (64 KiB) are there too, its processor (25 KB) in `.bss`: the pool is 333 KB of 336,
  the RAM 55 KB of 96.
- `-fdata-sections` (`fm1.toml`): each table its own section, so the link leaves out those nothing reads (Beads'
  synth mode's 100 KB of wavetables).
