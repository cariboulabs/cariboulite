#!/usr/bin/env python3
"""Exercise pure application radio rules with normal and production math flags."""
from pathlib import Path
import subprocess
import tempfile

src = Path(__file__).resolve().parents[1] / "src"
with tempfile.TemporaryDirectory(prefix="radio-profile-") as directory:
    for name, flags in (("normal", []), ("fast-math", ["-ffast-math"])):
        binary = Path(directory) / name
        subprocess.run([
            "cc", "-D_GNU_SOURCE", "-std=c11", "-O3", "-Wall", "-Wextra", "-Werror", *flags,
            "-I" + str(src), str(Path(__file__).with_suffix(".c")),
            str(src / "radio_profile.c"), "-lm", "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)
