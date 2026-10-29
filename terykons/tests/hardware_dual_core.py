#!/usr/bin/env python3
"""A/B processing on a connected FM-1. Plays notes and changes temporary settings; never presses SAVE.

Run with hands off the panel/keyboard. Leaves four voices, DIGITAL reverb 60, two cores and DEBUG open.
"""
import argparse
import json
import re
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mido
from fm1_remote import FM1
from grav_debug import snapshot


def run(output, reverbs=(0, 60)):
    fm = FM1()
    chord = [60, 64, 67, 71]
    rows = []
    sr = [0x7d, 0x53, 0x52]
    order = ["ANALOG", "WAVESHAPE", "FM 2-OP", "GRAIN", "ADDITIVE", "WAVETABLE", "CHORDS", "SPEECH",
             "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "KICK", "SNARE", "HI-HAT", "VA VCF",
             "PHASE DIST", "6-OP FM 1", "6-OP FM 2", "6-OP FM 3", "TERRAIN", "STRING MCH", "CHIPTUNE"]

    def announcements():
        return [frame[2:].decode("ascii", "replace") for frame in fm.frames(sr, 0.18)
                if frame and frame[0] == 16]

    def current_engine():
        # Announce expires its listener after five seconds without HELLO.
        fm.sysex(sr + [1, 1])
        time.sleep(0.2)
        fm.sysex(sr + [3])
        texts = announcements()
        for text in reversed(texts):
            match = re.search(r"\. WAVE ([^.]+)", text)
            if match:
                return match.group(1).strip()
        raise RuntimeError("No engine name in the screen reader reply")

    def stop():
        for note in chord:
            fm.out.send(mido.Message("note_off", note=note, velocity=0))
        time.sleep(0.03)

    try:
        fm.run(["listen"])
        announcements()
        engine = current_engine()
        fm.run(["turn", "ALGO", str(order.index("PHASE DIST") - order.index(engine))])
        assert current_engine() == "PHASE DIST"
        for reverb in reverbs:
            fm.run(["listen", "tap", "HOME"])
            # Walk to HALL using the page announcements; independent of the remembered FX page.
            for _ in range(3):
                fm.run(["tap", "FX"])
                frames = fm.frames([0x7d, 0x53, 0x52], 0.15)
                if any(b"PAGE HALL" in frame for frame in frames):
                    break
            else:
                raise RuntimeError("Could not confirm HALL page")
            # HALL's AMOUNT the reverb; TAPE's QUALITY DIGITAL; ECHO's AMOUNT 0 (0.32)
            fm.run(["turn", "K1", "-100", "turn", "K1", str(reverb), "tap", "FX", "turn", "K1", "-4",
                    "tap", "FX", "turn", "K1", "-100"])
            for voices in (2, 3, 4):
                fm.run(["tap", "GLO", "turn", "K1", "-4", "turn", "K1", str(voices - 1), "tap", "REC"])
                for mode in (1, 2):
                    stop()
                    fm.run(["turn", "K1", "-1" if mode == 1 else "1", "turn", "K2", "1"])
                    for note in chord[:voices]:
                        fm.out.send(mido.Message("note_on", note=note, velocity=100))
                    time.sleep(0.35)
                    samples = [snapshot(fm)]
                    time.sleep(0.15)
                    samples.append(snapshot(fm))
                    row = {"engine": "PHASE DIST", "configured_voices": voices, "cores": mode, "reverb": reverb,
                           "avg_pct": round(sum(s["avg_pct"] for s in samples) / len(samples), 1),
                           "peak_pct": round(max(s["peak_us"] for s in samples) * 100 / samples[-1]["budget_us"], 1),
                           "snapshot": samples[-1], "samples": samples}
                    rows.append(row)
                    d = samples[-1]
                    print(f"rev={reverb:2} voices={voices} cores={mode}: avg={row['avg_pct']}% "
                          f"peak={row['peak_pct']}% active={d['voices']} shed={d['shed']} "
                          f"over={d['overloads']} xrun={d['xruns']} stack={d['stack_bytes']}", flush=True)
                    if not d["cpu1_on"] or d["cpu1_error"] or d["bad_jobs"]:
                        raise RuntimeError("CPU1 failed during the hardware test")
                    stop()
            if reverb == 0:
                fm.run(["tap", "HOME"])
    finally:
        try:
            stop()
            fm.run(["release-all"])
        finally:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(rows, indent=2) + "\n")
            fm.inp.close()
            fm.out.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "terykons/build/debug-tests/hardware.json")
    parser.add_argument("--reverb", type=int, choices=(0, 60), help="run only one FX condition")
    args = parser.parse_args()
    run(args.out, (args.reverb,) if args.reverb is not None else (0, 60))
