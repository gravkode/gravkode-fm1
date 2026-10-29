# Terykons 0.14.1

Package: `terykons-0.14.1.fwsc`, identity `FM-1_73001401`.

- Beads runs on the second core while it is on, one 24-sample chunk behind the voices: CPU0 renders the voices
  while CPU1 takes the previous chunk through Beads (and renders voices too when it has room, by the last blocks'
  timings). No added latency; with the effect off, the voices split across the cores as in 0.14.0.
- The overload protection fades a voice out over 128 samples (2.9 ms) instead of cutting it, and never takes the
  last voice sounding (nor MONO's one voice).
- `cpu2_fallback = true`: a failure of the second core gives it up (one silent block, then one core until
  power-off) instead of restarting the FM-1.
- From 0.14.0's later fixes: the app's ENV shapes the three 6-OP FM banks, repeated notes restart their attack,
  LFO DEST steps through named destinations.
- Beads keeps a random stream of its own, so its grains are the same on either core.
- DEBUG: FX0 / FX1 shows the core Beads ran on.

Settings remain compatible with 0.12–0.14.0.

Validation: device build and memory/code checks (RAM 71 KB of 96, pool 333 KB of 336), the six Terykons simulator
scripts, the real-thread test (768 exact-output comparisons with CPU1 taking chunk jobs, and the protection's fade).
Not yet measured on the FM-1: the load with Beads on CPU1, and the listening check.
