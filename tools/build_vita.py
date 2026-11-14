#!/usr/bin/env python3
"""Build Orcs & Elves II for PS Vita (.vpk) with Classic + Remastered lighting."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VITASDK = Path(os.environ.get("VITASDK", str(ROOT / "tools/vendor/vitasdk")))
BUILD_ROOT = ROOT / "build"
BUILD = BUILD_ROOT / "vita"
STAGE = BUILD / "vpk"

sys.path.insert(0, str(ROOT / "tools"))
from build_livearea import build_livearea_assets


def resolve_data_dir() -> Path:
    env = os.environ.get("OE2_DATA_DIR")
    if env:
        return Path(env)
    for p in (ROOT / "data", ROOT / "build/pc", ROOT / "gamedata"):
        if (p / "orcsandelves2.bar").is_file():
            return p
    return ROOT / "data"


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


def setup_livearea():
    assets_dir = ROOT / "src/vita/assets"
    sce_sys_dir = STAGE / "sce_sys"
    if assets_dir.is_dir() and (assets_dir / "icon0.png").is_file():
        shutil.copytree(assets_dir, sce_sys_dir, dirs_exist_ok=True)
    elif (DATA_DIR / "orcsandelves2.bar").is_file():
        build_livearea_assets(DATA_DIR / "orcsandelves2.bar", sce_sys_dir)
    else:
        raise RuntimeError(
            f"LiveArea assets not found in {assets_dir} and game data not found in {DATA_DIR}.\n"
            "Run 'python3 tools/build_livearea.py' with game data first."
        )


def sdl2_libs() -> list[str]:
    pc = VITASDK / "arm-vita-eabi/lib/pkgconfig/sdl2.pc"
    if pc.is_file():
        for line in pc.read_text(encoding="utf-8").splitlines():
            if line.startswith("Libs:"):
                return [tok for tok in line.split()[1:] if tok.startswith("-l")]
    return ["-lSDL2"]


def build_vita_elf(bundle_data: bool = False):
    gcc = VITASDK / "bin/arm-vita-eabi-gcc"
    elf_create = VITASDK / "bin/vita-elf-create"
    make_fself = VITASDK / "bin/vita-make-fself"
    mksfoex = VITASDK / "bin/vita-mksfoex"
    pack_vpk = VITASDK / "bin/vita-pack-vpk"

    for tool in (gcc, elf_create, make_fself, mksfoex, pack_vpk):
        if not tool.is_file():
            raise RuntimeError(f"Required VitaSDK tool not found: {tool}")

    game_sources = [
        ROOT / "src/engine/src/bar.c",
        ROOT / "src/engine/src/moddata.c",
        ROOT / "src/engine/src/pmd.c",
        ROOT / "src/engine/src/qcelp.c",
        *sorted((ROOT / "src/engine/src/game").glob("*.c")),
        ROOT / "src/vita/platform_vita.c",
        ROOT / "src/vita/main_vita.c",
    ]
    fast_math_sources = [
        ROOT / "src/host/lighting.c",
        ROOT / "src/host/presentation.c",
        ROOT / "src/host/standalone_bridge.c",
    ]

    elf_path = BUILD / "oe2.elf"
    velf_path = BUILD / "oe2.velf"
    eboot_path = STAGE / "eboot.bin"
    sfo_path = STAGE / "sce_sys/param.sfo"
    obj_dir = BUILD / "obj"
    obj_dir.mkdir(parents=True, exist_ok=True)

    base_cflags = [
        "-std=gnu17",
        "-O3",
        "-mcpu=cortex-a9",
        "-mfpu=neon",
        "-mfloat-abi=hard",
        "-ftree-vectorize",
        "-fwrapv",
        "-funsigned-char",
        "-DOE2_STANDALONE_BRIDGE",
        "-DOE2_NO_SPRITE_SHADOWS",
        "-I" + str(ROOT / "src/engine/src"),
        "-I" + str(ROOT / "src/engine/src/game"),
        "-I" + str(ROOT / "src/host"),
        "-I" + str(ROOT / "src/vita"),
        "-I" + str(VITASDK / "arm-vita-eabi/include/SDL2"),
    ]
    from concurrent.futures import ThreadPoolExecutor

    def compile_one(item):
        src, cflags = item
        obj = obj_dir / (src.stem + ".o")
        subprocess.run([str(gcc), *cflags, "-c", str(src), "-o", str(obj)], check=True)
        return obj

    tasks = [(s, base_cflags) for s in game_sources] + [
        (s, [*base_cflags, "-ffast-math", "-funroll-loops"]) for s in fast_math_sources
    ]
    with ThreadPoolExecutor() as ex:
        objects = list(ex.map(compile_one, tasks))

    libs = [*sdl2_libs(), "-lz", "-lm", "-lpthread"]
    if "-lvitaGL" in libs:
        libs.append("-lstdc++")
    for stub in (
        "-lSceDisplay_stub",
        "-lSceCtrl_stub",
        "-lSceAudio_stub",
        "-lSceAudioIn_stub",
        "-lSceSysmodule_stub",
        "-lSceGxm_stub",
        "-lSceCommonDialog_stub",
        "-lSceAppMgr_stub",
        "-lSceTouch_stub",
        "-lSceHid_stub",
        "-lSceMotion_stub",
        "-lScePower_stub",
        "-lSceProcessmgr_stub",
    ):
        if stub not in libs:
            libs.append(stub)

    subprocess.run([str(gcc), "-Wl,-q", *map(str, objects), *libs, "-o", str(elf_path)], check=True)
    subprocess.run([str(elf_create), str(elf_path), str(velf_path)], check=True)
    subprocess.run([str(make_fself), "-s", str(velf_path), str(eboot_path)], check=True)
    subprocess.run(
        [
            str(mksfoex),
            "-s",
            "TITLE_ID=ORCS00002",
            "-d",
            "ATTRIBUTE2=12",
            "Orcs & Elves II",
            str(sfo_path),
        ],
        check=True,
    )

    if bundle_data:
        stage_data = STAGE / "data"
        stage_data.mkdir(parents=True, exist_ok=True)
        for name in ("orcsandelves2.bar", "orcsandelves2.mod"):
            shutil.copy2(DATA_DIR / name, stage_data / name)

    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    vpk_path = BUILD_ROOT / "oe2.vpk"
    if vpk_path.exists():
        vpk_path.unlink()
    subprocess.run([str(pack_vpk), "-s", str(sfo_path), "-b", str(eboot_path), str(vpk_path)], cwd=STAGE, check=True)
    # Re-pack with all staged assets (sce_sys/icon0.png, livearea, and data/* if bundled).
    import zipfile

    with zipfile.ZipFile(vpk_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for file_path in sorted(STAGE.rglob("*")):
            if file_path.is_file():
                zf.write(file_path, file_path.relative_to(STAGE).as_posix())
    sha = hashlib.sha256(vpk_path.read_bytes()).hexdigest()
    print(f"Built {vpk_path} ({vpk_path.stat().st_size} bytes, SHA-256 {sha})")


def parse_args():
    parser = argparse.ArgumentParser(description="Build Orcs & Elves II for PS Vita (.vpk)")
    parser.add_argument(
        "--bundle-data",
        action="store_true",
        help="Bundle .bar and .mod inside the VPK (only for private/testing builds, NOT for release)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    if args.bundle_data:
        verify_data()
    if STAGE.exists():
        shutil.rmtree(STAGE)
    setup_livearea()
    build_vita_elf(bundle_data=args.bundle_data)
    if args.bundle_data:
        print("Note: Game data was bundled in the VPK. No additional install is required.")
    else:
        print("Note: Game data is NOT bundled in the VPK.")
        print("Copy 'orcsandelves2.bar' and 'orcsandelves2.mod' to 'ux0:data/oe2/' to play.")

if __name__ == "__main__":
    main()

