# terykons: the firmware

Terykons' app, on fm1-base (the repository's root). What it is and does: the root [README.md](../README.md) and
the manual, [docs/MANUAL.md](docs/MANUAL.md).

| File | What |
| --- | --- |
| `app.h` | The app's settings (name, block size, the store's objects, APP_OVERLOAD) and its half of the contract with the platform |
| `app.c` | The app: the keys, buttons and knobs (`input`: a handler each), MIDI in and out; the pages (HOME's terricons, the sound's pages, SEQ, MOD, ENV, GLO, SCREEN, DEBUG, the mixer, the three FX pages: HALL, TAPE, ECHO, over a lake under the terricons, 0.33; ARP and ARP 2, the
arpeggiator's, 0.34; LOCK mode on EDIT, the parameter locks, 0.35), the presets' browser, recording; the lights; the screen (`draw`: header, page, bottom band; `app_frame` redraws the least that shows a change); the settings (`settings_load`: older records brought up to date) and SAVE |
| `synth/` | The voices (`synth.c`: four shared out among the synth tracks S1..S4, each MONO with legato, POLY or none, and one for each drum track D1..D4; the shedding), the envelope, the LFO, the event queue from the main loop to the audio interrupt |
| `sound/` | Engine control/page metadata, native FM byte mapping and validation; AUTO follows the original macros |
| `presets/` | Four factory starting points per engine; a 64-slot expanded preset log (184-byte sounds), indexed in flash with a bounded pending queue, CRC/commit and compaction tests |
| `seq/` | The sequencer (`seq.c`): 64 steps, length, chords, tempo, swing, MIDI clock out and in, the next pattern in at the pattern's end; each block's events at their sample offsets, on a 24-sample grid (`app.c` renders between them); the synth tracks' arpeggiators on the song's ticks (0.34); a pattern's parameter locks, a pool of 512, and each step's lock event before its notes (0.35). The patterns' bank in the flash (`bank.c`): 16 patterns in 32 slots of 8 KiB, saved a page at a time into erased slots, the older copies erased when quiet |
| `dsp/grav_engine.c` | The render: each voice through its engine and envelope, the mix through the effect, MASTER, the soft limiter, sleeping voices |
| `dsp/grav_plaits.cpp` | Plaits' 24 engines: their table (names, trigger modes, levels), each voice its own (its track's; a drum voice's, in a small slot, one of the three drums), rendered 12 samples at a time |
| `dsp/grav_drums.h` | The drum voices' drums (0.19): Plaits' drum models, the one chosen rendered, with the DRUM and SHAPE pages' knobs (the kick's PUNCH, SWEEP and DRIVE apart, where Plaits folds them onto HARMONICS) |
| `ui/icons.c` | The engines' icons (0.20), drawn by code from the time, the engine's knobs and its track's notes; one line on each engine (included by `app.c`) |
| `dsp/grav_fx.cpp` | The effect (0.32): ECHO, a delay in time with the tempo, and HALL, Beads' reverb (`dsp/grav_reverb.h`), coloured by TAPE's QUALITY (Beads' media); in mono, the sends in, the dry round it; off (and free) once nothing goes in and its tails have died. It measures what is sent in and HALL's tail for the FX pages' lake (0.33) |
| `dsp/grav_libc.c` | The C library functions the FM-1's link lacks (`fabsf`, `sqrtf`, `abs`, `memmove`) |
| `vendor/` | Plaits, Beads' reverb engine and tape, stmlib, libc++ and newlib headers: [vendor/README.md](vendor/README.md) |
| `fm1.toml` | Build and site settings: name, identity, the DSP units, their flags, the site's tagline and credits |
| `words.tsv` | The screen reader's words |
| `tests/` | The simulator's scripts: `basic.txt`, `engines.txt`, `voices.txt`, `fx.txt`, `arp.txt`, `locks.txt`, `envlfo.txt`, `tap.txt`, `midi.txt`, `copy.txt`, `dual_core.txt`, `six_op_controls.txt`, `seq.txt`, `rec.txt`, `theme.txt`, `mixer.txt`, `browser.txt`, `playback.txt`, `tracks.txt`, `drums.txt`, `patterns.txt`, `icons.txt`, `presets.txt`, `sound_pages.txt`, and `screens.txt` and `icon_sheet.txt` (the manual's and the README's pictures); `seq_test.c` (the sequencer's timing) and `bank_test.c` (the patterns' bank on a flash of its own: a save cut short, damage, wear), `run_seq_test.py`; `presets_test.c` (`run_presets_test.py`); `dual_core_test.cpp` (`run_dual_core_test.py`) |
| `docs/` | The manual and its pictures (`docs/img/compose.py` makes the README's from them) |
| `site/landing.md` | The site's front page |

## Build, test, install

Version 0.14 renders on both cores, with the DEBUG page (GLO three times since 0.25.1); since 0.14.1 the effect runs on the second core,
a chunk behind the voices.
K1 switches one/two-core processing; K2 clears peaks and counters. See
[the debug guide](docs/DUAL_CORE_DEBUG.md) for measurements, automated tests and the device checklist.

From the repository's root:

    export JIELI_TOOLCHAIN="$HOME/.jieli/toolchain" AC79_SDK="$HOME/fw-AC79_AIoT_SDK"
    python3 tools/build.py terykons                     # terykons/build/terykons.fwsc (no radio: no Bluetooth, WiFi)
    python3 tools/build.py terykons --host              # the simulator, then each script:
    terykons/build/host/terykons_host terykons/tests/basic.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/engines.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/voices.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/fx.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/seq.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/rec.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/theme.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/mixer.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/browser.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/playback.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/tracks.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/drums.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/patterns.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/icons.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/presets.txt terykons/build/host
    terykons/build/host/terykons_host terykons/tests/sound_pages.txt terykons/build/host
    python3 terykons/tests/run_presets_test.py
    python3 terykons/tests/run_dual_core_test.py
    python3 terykons/tests/run_seq_test.py
    python3 tools/build.py terykons --web               # the browser emulator: terykons/build/emu/
    python3 tools/fm1_install.py terykons/build/terykons.fwsc

The JieLi toolchain is Linux x86-64: on macOS each tool runs in Docker (Docker Desktop running). The build needs
Python with Pillow (the fonts).

## Release and site

    python3 tools/build.py terykons --release 0.40.0    # terykons/build/terykons-0.40.0.fwsc, identity FM-1_73004000
    python3 tools/build.py terykons --web
    python3 tools/make_site.py terykons terykons/build/terykons-0.40.0.fwsc 0.40.0 terykons/build/site

## The pictures

    python3 tools/build.py terykons --host --release 0.40.0   # (so GLO shows the version and identity)
    terykons/build/host/terykons_host terykons/tests/screens.txt terykons/docs/img
    python3 terykons/docs/img/compose.py

## Notes

- The FM-1 runs no C++ static constructors: a C++ object with a vtable can't be a static; `dsp/grav_plaits.cpp`
  builds the engines in place (placement new). Constant tables are fine.
- Plaits' engines run 12 samples at a time, always (the 6-op FM depends on it), through a FIFO per voice.
- The engines' RAM and each synth voice's slot are in the pool (`.pool`, PLAT_POOL): about 98 KB for four voices.
  The effect's echo (96 KiB) and reverb (64 KiB) are there too (0.32; Beads' delay and reverb before, its 25 KB
  processor in `.bss`): the pool is 333 KB of 336, the RAM about 67 KiB of 96 (including the 8 KiB CPU1 stack, the eight voices' block buffers and the four drum
  voices: their slots and 512 bytes of RAM each, in `.bss`).
- Expanded user presets (0.22) use a 32 KiB log at `0xE5000..0xED000`, indexed in place.
  A sound is 184 bytes; each 256-byte record commits its CRC header last. Fifteen records per sector leave the
  final page erased for loader compatibility. Eight RAM pending slots; compaction evacuates before erasing.
  The old object 2 bank at `0xDE000/DF000` stays intact for resumable import. Settings append eight control
  blocks to the compatible 92-byte prefix (1404 bytes total). See [SOUND_CONTROLS.md](docs/SOUND_CONTROLS.md).
- The patterns (0.18) are in the flash's data area, 0x97000..0xD7000 (`seq/bank.c`), read in place: the audio
  interrupt copies the next one in at the pattern's end, so RAM holds only the one playing. A save writes a page
  (256 bytes) at a time with the interrupts off for each, into a slot erased beforehand; erasing (tens of ms with
  the sound stopped) waits until nothing has sounded for 3 s.
- `-fdata-sections` (`fm1.toml`): each table its own section, so the link leaves out those nothing reads (Beads'
  synth mode's 100 KB of wavetables; 0.32 reads its sine alone).
