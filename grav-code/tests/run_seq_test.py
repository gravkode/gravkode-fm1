#!/usr/bin/env python3
"""Build and run the sequencer's timing test (tests/seq_test.c) against seq/seq.c, and the patterns' bank test
(tests/bank_test.c) against seq/bank.c on a flash of its own, on the host."""
import os
from pathlib import Path
import subprocess
import sys

app = Path(__file__).resolve().parents[1]
out = app / "build" / "host"
out.mkdir(parents=True, exist_ok=True)
exe = out / "seq_test"
cc = [os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall", "-Wextra", "-I" + str(app)]
subprocess.run([*cc, str(app / "tests" / "seq_test.c"), str(app / "seq" / "seq.c"), "-o", str(exe)], check=True)
bank = out / "bank_test"
subprocess.run([*cc, "-I" + str(app.parent / "include"), str(app / "tests" / "bank_test.c"),
                str(app / "seq" / "bank.c"), str(app / "seq" / "seq.c"), "-o", str(bank)], check=True)
sys.exit(subprocess.run([str(exe)]).returncode or subprocess.run([str(bank)]).returncode)
