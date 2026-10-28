# Two-core processing and DEBUG

Included in `main` since **0.14.0**. Developed in `codex/dual-core-debug`, based on
`feature/grav-code-v0.13.0` (the version with Beads).
At creation, local `main` and `origin/main` were still at `1a64076`, without the effect.
See [the measured hardware results](DUAL_CORE_RESULTS.md) for the installed build's first A/B test.

CPU0 handles input, MIDI, the UI, its share of the active Plaits voices, MASTER and the limiter.
Releases still count as active until silent. Since **0.14.1** the split depends on the effect:

- **Beads off** (bypassed): CPU1 renders the other active voices for the whole block in one job (four voices:
  2 + 2; three: 2 + 1; one: CPU0 alone, no job). CPU0 joins CPU1, mixes and passes the mix through.
- **Beads on** (or fading out): CPU1 runs Beads, one 24-sample chunk behind the voices. Job c renders CPU1's
  voices' chunk c (if any) and takes chunk c-1 of the mix through Beads, while CPU0 renders its voices' chunk c;
  CPU0 joins each job before handing over the next (about 12 jobs a block). The voices go to the core with less
  work by the last blocks' timings (Beads' time counts on CPU1), so with a heavy Beads they often all stay on
  CPU0 (MASK1 0).

CPU0 joins CPU1 before mixing into the output, returning from the audio interrupt, changing engines or writing
flash. No extra audio block of latency is introduced.

Plaits has a random stream per voice and stmlib has a separate random context per executing core.
Beads keeps a random stream of its own too, whichever core runs it. This prevents random state races and keeps the output identical
between the one-core and two-core modes of this firmware. Stochastic textures may differ from 0.13.0.
Engine construction clears the slot and its scratch RAM, including the six-op FM's staggered buffers.

## Controls

- **GLO twice** opens DEBUG (since 0.15; REC opened it before, and now records); GLO again returns to GLO. HOME,
  ENV, LFO and FX keep their usual actions.
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
| FX0 / FX1 | Most recent total Beads processing time (its granular/delay/reverb chain) and the core it ran on |
| WAIT | Time CPU0 spent joining the dispatched worker after finishing its own voices |
| XRUN | Audio DMA deadlines missed since the last debug reset |
| SHED | Voices taken by overload protection since reset; each fades out over 128 samples (2.9 ms), and the last voice sounding is never taken |
| STEAL | Audible voices reassigned to a different note since reset; distinct from overload cuts |
| OVER | Audio blocks exceeding the 90% protection threshold since reset; may rise without XRUN |
| VOICES | Number of voices rendered in the most recent block, including releases |
| JOBS | Completed CPU1 jobs since reset: one a block with the effect off (none when CPU1 has no voices), about 12 a block with it on |
| MASK0 / MASK1 | Decimal voice bitmasks: bits 1, 2, 4, 8 represent voices 1, 2, 3, 4. With the effect off, normally 5 / 10 with four voices; with it on, by the cores' load |
| BAD / ERROR | Driver's rejected-job count / failure reason; these are not cleared by K2 |
| STACK | CPU1 stack high water mark in bytes since boot (8192-byte allocation, including the guard area) |
| QUEUE | Lifetime input event queue overflow count, in the footer |

The footer's usual red CPU indicator continues to mean an xrun occurred at some point since boot.
DEBUG's XRUN counter uses the most recent reset as its baseline. Last-block timings can fluctuate;
PEAK, OVER, SHED and XRUN retain short overloads that AVG can hide.

Since 0.14.1 the worker runs with the BSP's fallback (`cpu2_fallback = true`): a bad job, fault, startup
failure or 8 ms timeout gives CPU1 up instead of restarting the FM-1. The block in progress is silent (a partly
run job is never replayed: it has already changed its DSP state), and the render goes on on CPU0 alone until
power-off; DEBUG then shows CPU1 OFF and the reason under ERROR. K1 is an A/B scheduling switch after a successful
boot; it does not bring back a CPU1 that was given up.

## Build and automated tests

From the repository root, with Python/Pillow and the usual device toolchain configured:

```sh
.venv/bin/python tools/build.py grav-code --host --release 0.14.1
.venv/bin/python grav-code/tests/run_dual_core_test.py
grav-code/build/host/grav_code_host grav-code/tests/dual_core.txt grav-code/build/host
.venv/bin/python tools/build.py grav-code --release 0.14.1
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

Device output: `grav-code/build/grav_code-0.14.1.fwsc`. The original hardware comparison used
`grav_code-0.13.0-dual.fwsc`; its results are retained in the hardware report.

## Check on the FM-1

1. Open DEBUG with GLO twice. Confirm CORES 2 and CPU1 ON. Hold four notes with the effect off: VOICES should
   reach 4, MASK0 / MASK1 normally show 5 / 10, SYN1 becomes nonzero and JOBS increases. Turn REVERB up:
   FX1 shows Beads on CPU1, JOBS climbs about 12 a block, and the voices settle where there is room.
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
