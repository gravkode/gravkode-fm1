# Two-core debug firmware

Branch: `codex/dual-core-debug`, based on `feature/grav-code-v0.13.0` (the version with Beads).
At creation, local `main` and `origin/main` were still at `7b5fbd3`, without the effect.
See [the measured hardware results](DUAL_CORE_RESULTS.md) for the installed build's first A/B test.

CPU0 handles input, MIDI, the UI, half the active Plaits voices, mixing, Beads, MASTER and the limiter.
CPU1 handles the other active voices. With four active voices, the split is 2 + 2; with three, 2 + 1;
with one, CPU0 does the work and CPU1 receives no job. Releases still count as active until silent.
Each worker renders a full audio block into separate voice buffers. CPU0 joins CPU1 before mixing,
processing effects, returning from the audio interrupt, changing engines or writing flash.
No extra audio block of latency is introduced.

Plaits has a random stream per voice and stmlib has a separate random context per executing core.
Beads keeps its own CPU0 context. This prevents random state races and keeps the output identical
between the one-core and two-core modes of this firmware. Stochastic textures may differ from 0.13.0.
Engine construction clears the slot and its scratch RAM, including the six-op FM's staggered buffers.

## Controls

- **REC** opens DEBUG; REC again returns to OSC. HOME, GLO, ENV, LFO and FX keep their usual actions.
- **K1 left** selects one-core processing; **K1 right** selects two-core processing.
  CPU1 remains booted and idle in one-core mode, so it can be used again without restarting.
  The selection is temporary and is not written by SAVE. Every boot selects two-core mode.
- **K2**, either direction, resets the debug peak and counters. Reset after switching modes or engines
  to compare the same sound and playing pattern. It does not stop the notes or clear the reverb.
- PRESET can still change the engine while DEBUG is open; keys and MIDI remain playable.

## Display

All times are **microseconds**. An audio block is 256 frames at 44.1 kHz, with a deadline of about 5805 us.

| Field | Meaning |
| --- | --- |
| CORES / CPU1 | Requested processing mode and worker availability; IDLE means one-core mode with the worker still booted |
| AVG | Existing smoothed **whole audio render deadline usage**, not summed CPU utilization |
| PEAK | Maximum whole-render deadline usage since boot/reset; includes engine changes, USB and nested timer work |
| SYN0 / SYN1 | Most recent voice rendering duration on CPU0 / CPU1; the two spans overlap |
| FX | Most recent total Beads processing time, including its granular/delay/reverb chain |
| WAIT | Time CPU0 spent joining the dispatched worker after finishing its own voices |
| XRUN | Audio DMA deadlines missed since the last debug reset |
| SHED | Voices actually cut by overload protection since reset; cuts are still immediate in this diagnostic build |
| STEAL | Audible voices reassigned to a different note since reset; distinct from overload cuts |
| OVER | Audio blocks exceeding the 90% protection threshold since reset; may rise without XRUN |
| VOICES | Number of voices rendered in the most recent block, including releases |
| JOBS | Completed CPU1 jobs since reset; stops increasing when the worker has no voices |
| MASK0 / MASK1 | Decimal voice bitmasks: bits 1, 2, 4, 8 represent voices 1, 2, 3, 4. Normally 5 / 10 with four voices |
| BAD / ERROR | Driver's rejected-job count / failure reason; these are not cleared by K2 |
| STACK | CPU1 stack high water mark in bytes since boot (8192-byte allocation, including the guard area) |
| QUEUE | Lifetime input event queue overflow count, in the footer |

The footer's usual red CPU indicator continues to mean an xrun occurred at some point since boot.
DEBUG's XRUN counter uses the most recent reset as its baseline. Last-block timings can fluctuate;
PEAK, OVER, SHED and XRUN retain short overloads that AVG can hide.

The worker uses the BSP's normal fault handling: a bad job, fault, startup failure or 8 ms timeout
records the failure and restarts the device. Two failed starts lead to safe mode. Automatic replay on
CPU0 is disabled because a partially executed job has already changed its DSP state. K1 is an A/B
scheduling switch after a successful boot; it does not bypass a failure to boot CPU1.

## Build and automated tests

From the repository root, with Python/Pillow and the usual device toolchain configured:

```sh
.venv/bin/python tools/build.py grav-code --host --release 0.13.0-dual
.venv/bin/python grav-code/tests/run_dual_core_test.py
grav-code/build/host/grav_code_host grav-code/tests/dual_core.txt grav-code/build/host
.venv/bin/python tools/build.py grav-code --release 0.13.0-dual
```

The concurrent test runs actual host threads against the DSP objects, rather than the stock host
simulator's synchronous CPU1 stub. It compares exact samples for all 24 engines, one to four voices,
FX off/on, all four Beads media across the engine cases, note changes, releases, freeze, engine
changes, partial blocks, rejected jobs, an unavailable worker and live one/two-core switching.
It checks functional equivalence, not AC79 speed, memory-bus reliability or audible hardware output.

With `mido` and `python-rtmidi` installed, `python tools/grav_debug.py` reads the same counters from the
connected FM-1 as JSON. Its read-only request is `F0 7D 47 44 F7`; the reply has the same prefix,
schema byte 1, then 22 unsigned 32-bit values as five low-first 7-bit bytes each, followed by F7.
Field order is documented by `FIELDS` in the tool.

`python grav-code/tests/hardware_dual_core.py` plays a controlled A/B series on the connected unit.
Run it only while the player leaves the panel untouched: it temporarily selects PHASE DIST, disables
FREEZE, uses DIGITAL with DRY/WET 0 and REVERB 0/60, and selects 2/3/4 voices. It never presses SAVE.
`--reverb 0` or `--reverb 60` runs just one condition, and `--out PATH` selects the JSON report.

Device output: `grav-code/build/grav_code-0.13.0-dual.fwsc`. This is a diagnostic build of 0.13.0,
not a new release. Keep `grav_code-0.13.0.fwsc` for comparison/reinstallation.

## Check on the FM-1

1. Open DEBUG with REC. Confirm CORES 2 and CPU1 ON. Hold four notes: VOICES should reach 4,
   MASK0 / MASK1 normally show 5 / 10, SYN1 becomes nonzero and JOBS increases.
2. Select the engine, envelope/release and reverb settings that produced the clicks.
3. Select one core with K1 left, reset with K2, then repeat the same notes/chords. Record AVG, PEAK,
   FX, OVER, SHED and XRUN. Note whether STEAL rises at the audible click.
4. Select two cores with K1 right, reset with K2, and repeat. Compare overloads and missed deadlines,
   as well as average load. Do not add SYN0 and SYN1 to estimate render latency: they overlap.
5. Try three/four voices, fast note changes, long releases, each Beads QUALITY, FREEZE and PRESET changes.
   Watch STACK and any restart/crash record. Hardware testing and listening are still required even
   when the host tests pass.

If SHED/OVER rise at the click, protection is involved. XRUN proves missed audio deadlines.
If the click persists with none of those counters rising, investigate voice reassignment and transitions;
STEAL alone counts a normal allocation event and does not prove an audible discontinuity.
