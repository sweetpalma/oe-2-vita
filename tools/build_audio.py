#!/usr/bin/env python3
"""Extract and decode BREW CMF/QCELP audio from orcsandelves2.bar into <crc>-<len>.pcm files."""
from pathlib import Path
import argparse
import hashlib
import os
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BAR_EXPECTED_SHA256 = "508aa62a368cc77c64113a68c5485f43871bf18fa819a58ae8e3aac085265c2f"


def default_data_dir() -> Path:
    return ROOT / "data"


def host_deps_flags() -> tuple[list[str], list[str]]:
    pkg_config = shutil.which("pkg-config")
    if pkg_config:
        try:
            cflags = subprocess.check_output([pkg_config, "--cflags", "zlib", "libavcodec", "libavutil"], text=True)
            libs = subprocess.check_output([pkg_config, "--libs", "zlib", "libavcodec", "libavutil"], text=True)
            return shlex.split(cflags), shlex.split(libs)
        except subprocess.CalledProcessError:
            pass

    cflags: list[str] = []
    lib_dirs: list[str] = []
    for prefix in (Path("/opt/homebrew"), Path("/usr/local"), Path("/usr")):
        inc = prefix / "include"
        if (inc / "libavcodec/avcodec.h").is_file():
            cflags.append(f"-I{inc}")
        lib = prefix / "lib"
        if lib.is_dir():
            lib_dirs.append(f"-L{lib}")
    if not cflags:
        raise RuntimeError("Could not find libavcodec/avcodec.h (install ffmpeg or libavcodec-dev)")
    return cflags, [*lib_dirs, "-lz", "-lavcodec", "-lavutil"]


def ensure_oe2tool() -> Path:
    sources = [
        ROOT / "engine/src/bar.c",
        ROOT / "engine/src/pmd.c",
        ROOT / "engine/src/moddata.c",
        ROOT / "engine/src/tools/oe2tool.c",
    ]
    headers = [
        ROOT / "engine/src/bar.h",
        ROOT / "engine/src/pmd.h",
        ROOT / "engine/src/moddata.h",
    ]
    tool_path = ROOT / "build/host/oe2tool"
    newest_src = max(p.stat().st_mtime for p in (*sources, *headers))
    if tool_path.is_file() and tool_path.stat().st_mtime >= newest_src:
        return tool_path

    cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        raise RuntimeError("No C compiler found to build host oe2tool")

    cflags, libs = host_deps_flags()
    tool_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        cc,
        "-std=c17",
        "-O2",
        f"-I{ROOT / 'engine/src'}",
        *cflags,
        *map(str, sources),
        *libs,
        "-o",
        str(tool_path),
    ]
    subprocess.run(cmd, check=True)
    return tool_path


def build_audio(bar_path: Path, out_dir: Path, force: bool = False) -> Path:
    if not bar_path.is_file() or hashlib.sha256(bar_path.read_bytes()).hexdigest() != BAR_EXPECTED_SHA256:
        raise RuntimeError(f"Missing or invalid {bar_path} (expected BREW 7.0.41 orcsandelves2.bar)")

    existing = list(out_dir.glob("*.pcm")) if out_dir.is_dir() else []
    if existing and not force:
        print(f"BREW audio: reusing {len(existing)} existing PCM files in {out_dir}")
        return out_dir

    out_dir.mkdir(parents=True, exist_ok=True)
    oe2tool = ensure_oe2tool()
    subprocess.run([str(oe2tool), "pcm", str(bar_path), str(out_dir)], check=True)
    return out_dir


def main():
    data_dir = default_data_dir()
    parser = argparse.ArgumentParser(description="Extract BREW CMF/QCELP audio from orcsandelves2.bar to PCM.")
    parser.add_argument("--bar", type=Path, default=data_dir / "orcsandelves2.bar", help="Path to orcsandelves2.bar")
    parser.add_argument("--out", type=Path, default=data_dir / "audio", help="Output directory for *.pcm files")
    parser.add_argument("--force", action="store_true", help="Re-decode even if *.pcm files already exist")
    args = parser.parse_args()
    build_audio(args.bar, args.out, force=args.force)


if __name__ == "__main__":
    main()

