# FM-1 hardware results — 25 October 2026

Firmware: `0.13.0-DUAL`, branch `codex/dual-core-debug`, installed on the USB-connected FM-1.
The pre-test and post-test crash logs both reported **no crash recorded**.
CPU1 remained on, with no rejected jobs or CPU1 errors. Its observed stack high water mark was 728 bytes
of the configured 8192 bytes for this PHASE DIST test.

Build checks passed: 328404 bytes of application flash, 68148 / 98304 bytes of ordinary RAM,
332776 / 344064 bytes of pool. The installation package is 611496 bytes (padded package size).
Package SHA-256: `9af42968042130f81dd62fe7eb31fafffb5bcfbbae0fd1e5370335e86b94d40f`.

The five app simulator scripts (`basic`, `voices`, `engines`, `fx`, `dual_core`) passed.
The real-thread DSP test passed 768 exact-output comparisons over all 24 engines, 1–4 voices,
FX off/on, engine changes, partial blocks, rejected/unavailable workers and live mode switching.
These software tests do not establish device-wide performance for every engine and parameter combination.

The generic template host test also passed when compiled with its intended BLE/central flags.
The normal template build currently omits those flags because of an existing commented-out portion
of `build_host` in `tools/build.py`; that invocation fails its Bluetooth expectations identically
with the baseline host source (17 failure messages). This unrelated baseline issue was not changed.

## Controlled device comparison

PHASE DIST, MIDI notes C4/E4/G4/B4 as appropriate, velocity 100, FREEZE off, Beads DIGITAL,
DRY/WET 0. The user left the controls untouched during these measurements. The test did not press SAVE.
Each row is a short trial: samples at approximately 350 ms and 500 ms after note-on, with debug
peaks/counters reset before the chord. These are measured cases, not guaranteed CPU ceilings.

With Beads off:

| Voices | One-core average | Two-core average | One-core peak | Two-core peak |
| --- | ---: | ---: | ---: | ---: |
| 2 | 32.5% | 17.0% | 33.8% | 17.9% |
| 3 | 48.0% | 32.5% | 49.2% | 33.3% |
| 4 | 63.5% | 33.0% | 64.6% | 33.5% |

All configured voices remained active. No overloads, shed voices or xruns occurred.

With REVERB 60 (the complete Beads chain runs):

| Requested voices | One-core peak | Two-core peak | Voices cut: one / two cores | Xruns: one / two cores |
| --- | ---: | ---: | ---: | ---: |
| 2 | 95.6% | 87.9% | 1 / 0 | 0 / 0 |
| 3 | 111.1% | 91.6% | 2 / 1 | 1 / 0 |
| 4 | 123.3% | 95.2% | 3 / 2 | 2 / 0 |

The smoothed average settled around 77–79% **after protection had already removed voices**.
It must not be treated as a measurement of four continuously active voices. This confirms how an
apparently sub-90% display can coexist with protection cuts and, on one core, missed audio deadlines.

Beads took 3384–3830 us in the sampled blocks, approximately 58–66% of the 5804 us budget by itself.
Splitting Plaits across the cores materially reduces rendering time and eliminated observed xruns
in these two-core trials. It did not sustain three/four voices with this reverb setting below the
90% shedding threshold. The remaining work is to reduce Beads' cost or overlap synthesis/effects
with a deliberately designed pipeline, and address the discontinuity from an immediate voice cut.

An earlier uncontrolled series was discarded because the engine and octave changed during it.
Two controlled runs were interrupted by the screen reader's timeout; the completed dry rows above
and the subsequent completed FX series were retained. The timeout affected the test tool's text query,
not CPU1 or its audio worker. The tool now refreshes HELLO and avoids redundant per-row screen reads.

Raw completed measurements are in the local build artifacts:
`build/debug-tests/hardware-controlled.json` (dry) and `hardware-controlled-fx.json` (reverb).
The general counters can be read again with `python tools/grav_debug.py`.

Listening for the user's original click and longer stress testing across other engines/settings are
still necessary. These tests measured deadlines, state and functional output; they did not record
or assess the device's physical audio output.
