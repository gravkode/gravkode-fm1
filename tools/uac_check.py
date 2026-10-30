#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check a recording of the FM-1's USB audio input in MULTI from a bench build (fm1.toml usb_audio_tracks, built
with FM1B_UAC_SAW=1: bsp/chip/usb.c uac_tap_tracks): the tracks' channels, 3.., carry sawtooths whose every frame has
a value of its own, so each frame lost or repeated on the way shows; channels 1 and 2 (the master) are only measured.

  tools/uac_check.py --record 30 [--device "Terykons FM-1"] [--out rec.wav]   record (macOS), then check
  tools/uac_check.py rec.wav                                                  check a recording (16-bit PCM WAV)

--record builds and runs tools/uac_record.swift (CoreAudio's HAL unit, the driver's frames as they are; ffmpeg's
AVFoundation input reorders and drops buffers). The recording must be the device's own samples: 44.1 kHz, 16 bits,
no level change, no resampling. Exit status 0 when every sawtooth frame follows the one before."""
import argparse
import array
import math
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

SAW_P = [147, 110, 88, 74, 63, 55, 49, 44]     # channel 3.. : period in frames (usb.c UA_SAW_P)
SAW_S = [55, 74, 93, 110, 130, 148, 167, 186]  # and the step per phase (UA_SAW_S): value = p * S - 4096
RATE = 44100


def record(seconds, device, out):
    """tools/uac_record.swift, built once (swiftc) into the temporary directory: the input named like `device`"""
    src = Path(__file__).with_name("uac_record.swift")
    exe = Path(tempfile.gettempdir()) / "fm1_uac_record"
    if not exe.exists() or exe.stat().st_mtime < src.stat().st_mtime:
        print("build   ", src.name, "(swiftc, once)")
        subprocess.run(["swiftc", "-O", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe), device, str(seconds), out], check=True)


def dbfs(x):
    return f"{20 * math.log10(x / 32768):.1f} dBFS" if x else "silent"


def check(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    print(f"file     {path}: {ch} channels, {8 * width} bits, {rate} Hz, {n} frames ({n / rate:.1f} s)")
    if width != 2:
        sys.exit("a 16-bit recording is needed (ffmpeg -c:a pcm_s16le)")
    if rate != RATE:
        print(f"  note: {rate} Hz, not {RATE}: resampled on the way, the sawtooths cannot be checked")
    s = array.array("h", raw)
    if sys.byteorder == "big":
        s.byteswap()
    if ch < 3:
        sys.exit(f"{ch} channels: the bench build has 10 (is the FM-1 running it? is the recorder taking them all?)")
    for c in range(min(ch, 2)):          # the master
        col = s[c::ch]
        peak = max((abs(v) for v in col), default=0)
        rms = (sum(v * v for v in col) / max(len(col), 1)) ** 0.5
        print(f"  ch {c + 1:2}  master   peak {dbfs(peak)}, rms {dbfs(rms)}")
    saws = list(range(2, min(ch, 2 + len(SAW_P))))
    # the recorder's own silence before the stream and after it: frames where every sawtooth is 0 (no phase is 0)
    first = next((i for i in range(n) if any(s[i * ch + c] for c in saws)), n)
    last = next((i for i in range(n - 1, -1, -1) if any(s[i * ch + c] for c in saws)), -1)
    if first >= last:
        sys.exit("the sawtooth channels are silent: not the bench build, or nothing recorded")
    if first or last < n - 1:
        print(f"  (recorder silence left out: {first} frames at the start, {n - 1 - last} at the end)")
    events = {}                          # frame -> what each channel saw there
    bad_total = 0
    for c in saws:
        k = c - 2
        P, S = SAW_P[k], SAW_S[k]
        prev, bad, jumps = None, 0, 0
        for i in range(first, last + 1):
            x = s[i * ch + c] + 4096
            if x % S or not 0 <= x // S < P:
                bad += 1
                events.setdefault(i, []).append(f"ch{c + 1} value {x - 4096}")
                prev = None
                continue
            p = x // S
            if prev is not None and p != (prev + 1) % P:
                gap = (p - prev - 1) % P
                jumps += 1
                events.setdefault(i, []).append(f"ch{c + 1} " + ("repeat" if gap == P - 1 else f"+{gap}"))
            prev = p
        bad_total += bad + jumps
        print(f"  ch {c + 1:2}  {RATE / P:6.1f} Hz  {'ok' if not bad and not jumps else 'FAIL'}"
              f"  ({jumps} jumps, {bad} wrong values)")
    if events:
        print(f"  {len(events)} frames with a break, the first ones:")
        for i in sorted(events)[:12]:
            print(f"    {i / rate:8.3f} s  frame {i}: {', '.join(events[i])}")
    print("result   " + ("every frame in order" if not bad_total else "frames lost, repeated or changed on the way"))
    return 0 if not bad_total else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav", nargs="?", help="a recording to check")
    ap.add_argument("--record", type=float, metavar="SECONDS", help="record this long first (ffmpeg, macOS)")
    ap.add_argument("--device", default="Terykons FM-1", help="the input's name, or part of it")
    ap.add_argument("--out", default="uac_check.wav", help="where --record writes")
    a = ap.parse_args()
    if a.record:
        record(a.record, a.device, a.out)
        a.wav = a.out
    if not a.wav:
        ap.error("a WAV file, or --record SECONDS")
    sys.exit(check(a.wav))


if __name__ == "__main__":
    main()
