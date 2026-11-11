#!/usr/bin/env python3
"""Build Orcs & Elves II for PS Vita (.vpk) with Classic + Remastered lighting."""
from pathlib import Path
import hashlib
import os
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
VITASDK = Path(os.environ.get("VITASDK", str(ROOT / "tools/vendor/vitasdk")))
BUILD = ROOT / "build/vita"
STAGE = BUILD / "vpk"
DIST = ROOT / "dist"


def verify_gamedata():
    expected = {
        "orcsandelves2.bar": "508aa62a368cc77c64113a68c5485f43871bf18fa819a58ae8e3aac085265c2f",
        "orcsandelves2.mod": "1b8664e937ef8c43c474dff9b82c398b46425c5cca43eff5919e4ce96fc3d750",
    }
    for name, sha in expected.items():
        path = ROOT / "gamedata" / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != sha:
            raise RuntimeError(f"Missing or invalid gamedata/{name} (expected BREW 7.0.41)")


def read_bmp8(data: bytes):
    bf_off = struct.unpack_from("<I", data, 10)[0]
    bi_size, w, h, _, bpp, comp, _, _, _, clr_used, _ = struct.unpack_from("<IiiHHIIIIII", data, 14)
    if bpp != 8 or comp != 0:
        raise RuntimeError(f"Expected 8-bit uncompressed BMP, got bpp={bpp} comp={comp}")
    pal_count = clr_used if clr_used else (bf_off - 14 - bi_size) // 4
    palette = []
    for i in range(pal_count):
        b, g, r, _ = struct.unpack_from("<BBBB", data, 14 + bi_size + i * 4)
        palette.append((r, g, b))
    abs_h = abs(h)
    stride = (w + 3) & ~3
    rows = []
    for y in range(abs_h):
        src_y = (abs_h - 1 - y) if h > 0 else y
        row_start = bf_off + src_y * stride
        rows.append([palette[idx] for idx in data[row_start : row_start + w]])
    return w, abs_h, rows


def write_png8(path: Path, w: int, h: int, rgb_rows):
    # Quantize to a 3-3-2 RGB palette (256 colors) so LiveArea always receives an 8-bit indexed PNG.
    plte = bytearray()
    for i in range(256):
        r = ((i >> 5) & 7) * 255 // 7
        g = ((i >> 2) & 7) * 255 // 7
        b = (i & 3) * 255 // 3
        plte.extend((r, g, b))
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # Filter type 0 (None)
        row = rgb_rows[y]
        for x in range(w):
            r, g, b = row[x]
            idx = ((r * 7 + 127) // 255 << 5) | ((g * 7 + 127) // 255 << 2) | ((b * 3 + 127) // 255)
            raw.append(idx)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"PLTE", bytes(plte)) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def build_livearea_assets():
    oe2tool = ROOT / "build/native/src/oe2tool"
    raw_dir = BUILD / "raw_bmp"
    raw_dir.mkdir(parents=True, exist_ok=True)
    for rid in (3034, 3037):
        subprocess.run(
            [str(oe2tool), "extract", str(ROOT / "gamedata/orcsandelves2.bar"), str(rid), str(raw_dir / f"{rid}.bmp")],
            check=True,
        )
    map_w, map_h, map_rows = read_bmp8((raw_dir / "3034.bmp").read_bytes())
    logo_w, logo_h, logo_rows = read_bmp8((raw_dir / "3037.bmp").read_bytes())

    def render_canvas(cw: int, ch: int, logo_scale: int):
        rows = []
        for y in range(ch):
            sy = (y * map_h // ch) % map_h
            row = []
            for x in range(cw):
                sx = (x * map_w // cw) % map_w
                r, g, b = map_rows[sy][sx]
                row.append((r * 3 // 5, g * 3 // 5, b * 3 // 5))
            rows.append(row)
        lw, lh = logo_w * logo_scale, logo_h * logo_scale
        ox = (cw - lw) // 2
        oy = (ch - lh) // 2
        for y in range(lh):
            dy = oy + y
            if not (0 <= dy < ch):
                continue
            ly = y // logo_scale
            for x in range(lw):
                dx = ox + x
                if not (0 <= dx < cw):
                    continue
                lr, lg, lb = logo_rows[ly][x // logo_scale]
                if lr >= 248 and lg == 0 and lb >= 248:
                    continue
                rows[dy][dx] = (lr, lg, lb)
        return rows

    write_png8(STAGE / "sce_sys/icon0.png", 128, 128, render_canvas(128, 128, 1))
    write_png8(STAGE / "sce_sys/livearea/contents/bg.png", 840, 500, render_canvas(840, 500, 4))
    write_png8(STAGE / "sce_sys/livearea/contents/startup.png", 280, 158, render_canvas(280, 158, 2))

    template_xml = """<?xml version="1.0" encoding="utf-8"?>
<livearea style="a1" format-ver="01.00" content-rev="1">
\t<livearea-background>
\t\t<image>bg.png</image>
\t</livearea-background>
\t<gate>
\t\t<startup-image>startup.png</startup-image>
\t</gate>
</livearea>
"""
    (STAGE / "sce_sys/livearea/contents/template.xml").write_text(template_xml, encoding="utf-8")


def build_vita_elf():
    gcc = VITASDK / "bin/arm-vita-eabi-gcc"
    elf_create = VITASDK / "bin/vita-elf-create"
    make_fself = VITASDK / "bin/vita-make-fself"
    mksfoex = VITASDK / "bin/vita-mksfoex"
    pack_vpk = VITASDK / "bin/vita-pack-vpk"

    for tool in (gcc, elf_create, make_fself, mksfoex, pack_vpk):
        if not tool.is_file():
            raise RuntimeError(f"Required VitaSDK tool not found: {tool}")

    game_sources = [
        ROOT / "engine/src/bar.c",
        ROOT / "engine/src/moddata.c",
        *sorted((ROOT / "engine/src/game").glob("*.c")),
        ROOT / "vita/pmd_vita.c",
        ROOT / "vita/platform_vita.c",
        ROOT / "vita/main_vita.c",
    ]
    fast_math_sources = [
        ROOT / "host/lighting.c",
        ROOT / "host/presentation.c",
        ROOT / "vita/standalone_bridge.c",
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
        "-DOE2_WEB_BRIDGE",
        "-DOE2_STANDALONE_BRIDGE",
        "-DOE2_NO_SPRITE_SHADOWS",
        "-I" + str(ROOT / "engine/src"),
        "-I" + str(ROOT / "engine/src/game"),
        "-I" + str(ROOT / "host"),
        "-I" + str(ROOT / "vita"),
        "-I" + str(VITASDK / "arm-vita-eabi/include/SDL2"),
    ]
    objects = []
    for src in game_sources:
        obj = obj_dir / (src.stem + ".o")
        subprocess.run([str(gcc), *base_cflags, "-c", str(src), "-o", str(obj)], check=True)
        objects.append(obj)
    for src in fast_math_sources:
        obj = obj_dir / (src.stem + ".o")
        subprocess.run(
            [str(gcc), *base_cflags, "-ffast-math", "-funroll-loops", "-c", str(src), "-o", str(obj)],
            check=True,
        )
        objects.append(obj)

    libs = [
        "-lSDL2",
        "-lz",
        "-lm",
        "-lpthread",
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
    ]

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

    stage_data = STAGE / "gamedata"
    stage_data.mkdir(parents=True, exist_ok=True)
    for name in ("orcsandelves2.bar", "orcsandelves2.mod"):
        shutil.copy2(ROOT / "gamedata" / name, stage_data / name)
    shutil.copytree(ROOT / "gamedata/audio", stage_data / "audio", dirs_exist_ok=True)

    DIST.mkdir(parents=True, exist_ok=True)
    vpk_path = DIST / "Orcs-Elves-II-Vita.vpk"
    if vpk_path.exists():
        vpk_path.unlink()
    subprocess.run([str(pack_vpk), "-s", str(sfo_path), "-b", str(eboot_path), str(vpk_path)], cwd=STAGE, check=True)
    # Re-pack with all staged assets (sce_sys/icon0.png, livearea, and gamedata/*).
    import zipfile

    with zipfile.ZipFile(vpk_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for file_path in sorted(STAGE.rglob("*")):
            if file_path.is_file():
                zf.write(file_path, file_path.relative_to(STAGE).as_posix())
    sha = hashlib.sha256(vpk_path.read_bytes()).hexdigest()
    print(f"Built {vpk_path} ({vpk_path.stat().st_size} bytes, SHA-256 {sha})")


def main():
    verify_gamedata()
    if STAGE.exists():
        shutil.rmtree(STAGE)
    (STAGE / "sce_sys/livearea/contents").mkdir(parents=True, exist_ok=True)
    build_livearea_assets()
    build_vita_elf()


if __name__ == "__main__":
    main()

