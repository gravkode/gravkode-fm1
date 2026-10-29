# Terykons 0.14.0

Package: `terykons-0.14.0.fwsc`, identity `FM-1_73001400`.

- Plaits voice rendering uses both FM-1 cores by default, up to two voices per core.
- Beads provides granular processing, delay, four recording qualities and reverb, with FREEZE and SEED.
- REC opens DEBUG. K1 selects one/two-core processing; K2 resets peaks and counters. These choices are temporary.
- DEBUG shows average/peak load, synthesis/effect/wait times, worker jobs, active voices, audio xruns,
  protection cuts, voice reassignment and CPU1 status/stack use. USB-MIDI can read the same counters.
- Random state is isolated per core and per voice. Engine changes clear scratch memory for a fresh start.

Settings remain compatible with 0.12/0.13. Two-core processing improves capacity, but heavy Beads settings
can still cross the 90% protection threshold and cause immediate voice cuts. See the
[hardware results](DUAL_CORE_RESULTS.md) and [DEBUG guide](DUAL_CORE_DEBUG.md).

Validation: device build and memory/code checks, all five Terykons simulator scripts, 768 exact-output
DSP comparisons using real host threads, and the documented A/B measurements of the same DSP implementation in 0.13.0-DUAL on a USB-connected FM-1.
