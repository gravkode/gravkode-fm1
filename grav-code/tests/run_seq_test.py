#!/usr/bin/env python3
"""Build and run the sequencer's timing test (tests/seq_test.c) against seq/seq.c, on the host."""
import os
from pathlib import Path
import subprocess
import sys

app = Path(__file__).resolve().parents[1]
out = app / "build" / "host"
out.mkdir(parents=True, exist_ok=True)
exe = out / "seq_test"
subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall", "-Wextra", "-I" + str(app),
                str(app / "tests" / "seq_test.c"), str(app / "seq" / "seq.c"), "-o", str(exe)], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
