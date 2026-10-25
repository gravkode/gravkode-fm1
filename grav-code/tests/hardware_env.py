#!/usr/bin/env python3
"""Capture only the FM-1 USB audio input while exercising ENV. No SAVE; no microphone capture.

Uses 6-OP FM 2 with TIMBRE 65, MORPH 50, HARMONICS 75. Sets FX/LFO off,
MONO, master 50%, and changes ENV temporarily. Keep the hardware controls untouched.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mido
from fm1_remote import FM1, SR


def capture(path, device):
    fm = FM1()
    events = []
    process = None
    started = 0.0

    def messages():
        return [f[2:].decode("ascii", "replace") for f in fm.frames(SR, 0.2) if f and f[0] == 16]

    def command(*args):
        fm.run(list(args))
        return messages()

    def note(on):
        fm.out.send(mido.Message("note_on" if on else "note_off", note=60, velocity=100 if on else 0))
        events.append({"t": time.monotonic() - started, "note": "on" if on else "off"})

    def scenario(name, attack, release, hold, gap, repeats=1):
        command("turn", "K1", "-100", "turn", "K1", str(attack),
                "turn", "K3", "-100", "turn", "K3", str(release))
        events.append({"t": time.monotonic() - started, "scenario": name,
                       "attack": attack, "release": release})
        for _ in range(repeats):
            note(True); time.sleep(hold)
            note(False); time.sleep(gap)

    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        fm.sysex(SR + [1, 1])
        hello = fm.frames(SR, 0.3)
        name = next((frame[2:].split(b"\0")[0].decode("ascii", "replace") for frame in hello
                     if frame and frame[0] == 1), "unknown")
        events.append({"firmware": name})
        print("firmware:", name, flush=True)
        fm.sysex(SR + [3])
        description = messages()
        engine = next((re.search(r"\. WAVE ([^.]+)", t).group(1).strip() for t in description
                       if re.search(r"\. WAVE ([^.]+)", t)), None)
        if engine != "6-OP FM 2":
            order = ["ANALOG", "WAVESHAPE", "FM 2-OP", "GRAIN", "ADDITIVE", "WAVETABLE", "CHORDS", "SPEECH",
                     "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT", "VA VCF",
                     "PHASE DIST", "6-OP FM 1", "6-OP FM 2", "6-OP FM 3", "TERRAIN", "STRING MCH", "CHIPTUNE"]
            if engine not in order: raise RuntimeError(f"No engine name in reply: {description!r}")
            command("turn", "PRESET", str(order.index("6-OP FM 2") - order.index(engine)))
        state = command("tap", "EDIT")
        if "FREEZE ON" in state:
            command("tap", "EDIT")
        elif "FREEZE OFF" not in state:
            raise RuntimeError("No FREEZE state announcement")
        command("tap", "HOME", "turn", "K1", "-100", "turn", "K1", "65",
                "turn", "K2", "-100", "turn", "K2", "50", "turn", "K3", "-100", "turn", "K3", "75")
        for _ in range(3):
            text = command("tap", "FX")
            if "PAGE FX MIX" in text: break
        else: raise RuntimeError("No FX MIX page announcement")
        command("turn", "K1", "-100", "turn", "K2", "-100")
        command("tap", "LFO", "turn", "K2", "-100")
        command("tap", "GLO", "turn", "K1", "-4", "master", "2048")
        command("tap", "ENV", "turn", "K1", "-100", "turn", "K2", "-100", "turn", "K3", "-100")
        time.sleep(0.3)
        started = time.monotonic()
        log = path.with_suffix(".ffmpeg.log").open("w")
        process = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "warning", "-f", "avfoundation",
                                    "-i", device, "-t", "16", "-ar", "44100", "-ac", "2",
                                    "-c:a", "pcm_s16le", "-y", str(path)], stdout=log, stderr=log)
        time.sleep(1.0)
        if process.poll() is not None: raise RuntimeError("USB capture did not start; see ffmpeg log")
        scenario("fast", 0, 0, 0.3, 0.25)
        scenario("slow_attack", 100, 0, 1.2, 0.25)
        scenario("long_release", 0, 100, 0.3, 1.0)
        scenario("retrigger_slow", 100, 100, 0.25, 0.08, repeats=3)
        time.sleep(4.3)
        process.wait(timeout=12)
        if process.returncode: raise RuntimeError("USB capture failed; see ffmpeg log")
        print(path, flush=True)
    finally:
        try:
            fm.out.send(mido.Message("note_off", note=60, velocity=0))
            fm.run(["release-all"])
        finally:
            if process and process.poll() is None:
                process.terminate(); process.wait(timeout=5)
            path.with_suffix(".json").write_text(json.dumps(events, indent=2) + "\n")
            fm.inp.close(); fm.out.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--device", default=":GRAV CODE FM-1")
    args = ap.parse_args()
    capture(args.out.resolve(), args.device)
