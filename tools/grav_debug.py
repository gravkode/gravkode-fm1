#!/usr/bin/env python3
"""Read Terykons' dual-core DEBUG counters over USB-MIDI, without changing settings."""
import json
from fm1_remote import FM1

HEADER = [0x7D, 0x47, 0x44]
FIELDS = (
    "dual_requested", "cpu1_on", "cpu1_error", "synth0_us", "synth1_us", "fx_us", "wait_us",
    "avg_pct", "last_us", "peak_us", "budget_us", "overloads", "xruns", "voices", "mask0", "mask1",
    "jobs", "steals", "shed", "queue_overflows", "bad_jobs", "stack_bytes",
)


def snapshot(fm):
    for _ in fm.inp.iter_pending():
        pass
    fm.sysex(HEADER)
    frames = fm.frames(HEADER, 2.0, until=lambda _: True)
    if not frames:
        raise RuntimeError("No DEBUG reply: install the dual-core debug firmware first")
    data = frames[0]
    if len(data) != 1 + 5 * len(FIELDS) or data[0] != 1:
        raise RuntimeError("Unsupported or incomplete DEBUG snapshot")
    values = [sum(data[1 + 5 * i + j] << (7 * j) for j in range(5)) for i in range(len(FIELDS))]
    return dict(zip(FIELDS, values))


if __name__ == "__main__":
    print(json.dumps(snapshot(FM1()), indent=2))
