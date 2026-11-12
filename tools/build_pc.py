#!/usr/bin/env python3
"""Build Orcs & Elves II for native PC (SDL2) with Classic + Remastered lighting."""
from pathlib import Path
import hashlib
import os
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = ROOT / "build"


def resolve_data_dir() -> Path:
    env = os.environ.get("OE2_DATA_DIR")
    return Path(env) if env else ROOT / "data"


DATA_DIR = resolve_data_dir()


def verify_data():
    expected = {
        "orcsandelves2.bar": "508aa62a368cc77c64113a68c5485f43871bf18fa819a58ae8e3aac085265c2f",
        "orcsandelves2.mod": "1b8664e937ef8c43c474dff9b82c398b46425c5cca43eff5919e4ce96fc3d750",
    }
    for name, sha in expected.items():
        path = DATA_DIR / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != sha:
            raise RuntimeError(f"Missing or invalid {path} (expected BREW 7.0.41)")


def main():
    verify_data()
    cmake = shutil.which("cmake")
    if not cmake:
        raise RuntimeError("cmake not found in PATH")

    arch = platform.machine().lower() or "unknown"
    build_dir = BUILD_ROOT / "pc" / f"{sys.platform}-{arch}"
    subprocess.run([cmake, "-S", str(ROOT / "engine"), "-B", str(build_dir)], check=True)
    subprocess.run([cmake, "--build", str(build_dir), "--parallel"], check=True)

    exe_name = "oe2.exe" if sys.platform == "win32" else "oe2"
    src_bin = build_dir / "src" / exe_name
    if not src_bin.is_file():
        raise RuntimeError(f"Expected built executable at {src_bin}")

    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    dst_bin = BUILD_ROOT / exe_name
    shutil.copy2(src_bin, dst_bin)
    sha = hashlib.sha256(dst_bin.read_bytes()).hexdigest()
    print(f"Built {dst_bin} ({dst_bin.stat().st_size} bytes, SHA-256 {sha})")


if __name__ == "__main__":
    main()

