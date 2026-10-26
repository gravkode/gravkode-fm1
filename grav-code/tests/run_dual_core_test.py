#!/usr/bin/env python3
"""Build/run the concurrent DSP regression against objects from tools/build.py grav-code --host."""
import os
from pathlib import Path
import subprocess
import sys

app = Path(__file__).resolve().parents[1]
root = app.parent
out = app / "build" / "host"
objects = sorted((out / "o2").glob("*.o"))
if not objects:
    raise SystemExit("Run tools/build.py grav-code --host first")
exe = out / "dual_core_test"
subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-O2", "-ffp-contract=off", "-pthread",
                "-DPLAT_HOST", "-I" + str(app), "-I" + str(app / "vendor"), "-I" + str(root / "include"),
                str(app / "tests" / "dual_core_test.cpp"), *map(str, objects), "-lm", "-o", str(exe)], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
