#!/usr/bin/env python3
"""Validate the preset bank's persisted format and browsing without platform or DSP state."""
import os
from pathlib import Path
import subprocess

app = Path(__file__).resolve().parents[1]
out = app / "build" / "host"
out.mkdir(parents=True, exist_ok=True)
exe = out / "presets_test"
subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I" + str(app), str(app / "tests/presets_test.c"), str(app / "presets/presets.c"),
                "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
