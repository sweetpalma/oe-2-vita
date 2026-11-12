#!/usr/bin/env python3
"""Build Orcs & Elves II for PS Vita (.vpk) with Classic + Remastered lighting."""
from pathlib import Path
import gzip
import hashlib
import os
import shutil
import struct
import subprocess
import zlib

from build_audio import build_audio

ROOT = Path(__file__).resolve().parents[1]
VITASDK = Path(os.environ.get("VITASDK", str(ROOT / "tools/vendor/vitasdk")))
BUILD_ROOT = ROOT / "build"
BUILD = BUILD_ROOT / "vita"
STAGE = BUILD / "vpk"


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


def ensure_audio():
    audio_dir = DATA_DIR / "audio"
    if not audio_dir.is_dir() or not any(audio_dir.glob("*.pcm")):
        build_audio(DATA_DIR / "orcsandelves2.bar", audio_dir)
    if not audio_dir.is_dir() or not any(audio_dir.glob("*.pcm")):
        raise RuntimeError(f"No PCM audio files found in {audio_dir}")


def extract_bar_resource(bar_bytes: bytes, target_id: int) -> bytes:
    ntypes = struct.unpack_from("<H", bar_bytes, 0x06)[0]
    typedir, _, tbl, _, tblend = struct.unpack_from("<IIIII", bar_bytes, 0x08)
    nent = (tblend - tbl) // 4
    for k in range(ntypes):
        rtype, first, span, idx = struct.unpack_from("<HHHH", bar_bytes, typedir + k * 8)
        if rtype != 6:
            continue
        for j in range(span + 1):
            if idx + j >= nent:
                break
            if first + j != target_id:
                continue
            o = struct.unpack_from("<I", bar_bytes, tbl + 4 * (idx + j))[0]
            e = struct.unpack_from("<I", bar_bytes, tbl + 4 * (idx + j + 1))[0] if idx + j + 1 < nent else len(bar_bytes)
            hl = struct.unpack_from("<H", bar_bytes, o)[0]
            payload = bar_bytes[o + hl : e]
            if len(payload) >= 18 and payload[:2] == b"\x1f\x8b":
                return gzip.decompress(payload)
            return payload
    raise RuntimeError(f"Resource {target_id} not found in orcsandelves2.bar")


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
    # Build an exact 256-color palette when possible, falling back to median-cut quantization.
    counts = {}
    for row in rgb_rows:
        for px in row:
            counts[px] = counts.get(px, 0) + 1
    if len(counts) <= 256:
        palette = list(counts.keys())
        lut = {c: i for i, c in enumerate(palette)}
    else:
        buckets = [list(counts.keys())]
        while len(buckets) < 256:
            buckets.sort(key=lambda b: max(max(c[k] for c in b) - min(c[k] for c in b) for k in range(3)) if len(b) > 1 else -1, reverse=True)
            top = buckets[0]
            if len(top) <= 1:
                break
            ch = max(range(3), key=lambda k: max(c[k] for c in top) - min(c[k] for c in top))
            top.sort(key=lambda c: c[ch])
            mid = len(top) // 2
            buckets = buckets[1:] + [top[:mid], top[mid:]]
        palette = []
        for b in buckets:
            tot = sum(counts[c] for c in b)
            palette.append((
                sum(c[0] * counts[c] for c in b) // tot,
                sum(c[1] * counts[c] for c in b) // tot,
                sum(c[2] * counts[c] for c in b) // tot,
            ))
        lut = {}
        for c in counts:
            r, g, b = c
            lut[c] = min(range(len(palette)), key=lambda i: (palette[i][0] - r) ** 2 + (palette[i][1] - g) ** 2 + (palette[i][2] - b) ** 2)

    plte = bytearray()
    for r, g, b in palette:
        plte.extend((r, g, b))
    if len(palette) < 256:
        plte.extend(b"\x00\x00\x00" * (256 - len(palette)))

    raw = bytearray()
    for y in range(h):
        raw.append(0)  # Filter type 0 (None)
        row = rgb_rows[y]
        for x in range(w):
            raw.append(lut[row[x]])

    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"PLTE", bytes(plte)) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def build_livearea_assets():
    bar_bytes = (DATA_DIR / "orcsandelves2.bar").read_bytes()
    raw_dir = BUILD / "raw_bmp"
    raw_dir.mkdir(parents=True, exist_ok=True)
    bmp_3034 = extract_bar_resource(bar_bytes, 3034)
    bmp_3037 = extract_bar_resource(bar_bytes, 3037)
    (raw_dir / "3034.bmp").write_bytes(bmp_3034)
    (raw_dir / "3037.bmp").write_bytes(bmp_3037)
    map_w, map_h, map_rows = read_bmp8(bmp_3034)
    logo_w, logo_h, logo_rows = read_bmp8(bmp_3037)

    def render_canvas(cw: int, ch: int, logo_scale: int, draw_logo: bool = True):
        crop_h = min(map_h, max(1, round(map_w * ch / cw)))
        crop_y = max(0, min(map_h - crop_h, 255 - crop_h // 2))
        rows = []
        for y in range(ch):
            sy = crop_y + (y * crop_h // ch)
            row = []
            for x in range(cw):
                sx = x * map_w // cw
                r, g, b = map_rows[sy][sx]
                if draw_logo:
                    row.append((r * 3 // 5, g * 3 // 5, b * 3 // 5))
                else:
                    row.append((r, g, b))
            rows.append(row)
        if not draw_logo:
            return rows
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

    def draw_label(rows, cw: int, ch: int, text: str, scale: int = 2):
        font = {
            "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
            "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
            "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
            "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
            "a": ["00000", "00000", "01110", "00001", "01111", "10001", "01111"],
            "b": ["10000", "10000", "10110", "11001", "10001", "10001", "11110"],
            "d": ["00001", "00001", "01101", "10011", "10001", "10001", "01111"],
            "e": ["00000", "00000", "01110", "10001", "11111", "10000", "01110"],
            "f": ["00110", "01001", "01000", "11100", "01000", "01000", "01000"],
            "g": ["00000", "00000", "01111", "10001", "01111", "00001", "01110"],
            "h": ["10000", "10000", "10110", "11001", "10001", "10001", "10001"],
            "i": ["00100", "00000", "01100", "00100", "00100", "00100", "01110"],
            "k": ["10000", "10000", "10010", "10100", "11000", "10100", "10010"],
            "l": ["01100", "00100", "00100", "00100", "00100", "00100", "01110"],
            "m": ["00000", "00000", "11010", "10101", "10101", "10001", "10001"],
            "n": ["00000", "00000", "10110", "11001", "10001", "10001", "10001"],
            "o": ["00000", "00000", "01110", "10001", "10001", "10001", "01110"],
            "r": ["00000", "00000", "10110", "11001", "10000", "10000", "10000"],
            "s": ["00000", "00000", "01111", "10000", "01110", "00001", "11110"],
            "t": ["01000", "01000", "11100", "01000", "01000", "01001", "00110"],
            "u": ["00000", "00000", "10001", "10001", "10001", "10011", "01101"],
            "w": ["00000", "00000", "10001", "10001", "10101", "10101", "01010"],
            "y": ["00000", "00000", "10001", "10001", "01111", "00001", "01110"],
            ",": ["00000", "00000", "00000", "00000", "00000", "00100", "01000"],
            " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
        }
        adv = 6 * scale
        tw = len(text) * adv - scale
        ox = (cw - tw) // 2
        oy = ch - 7 * scale - 22
        for pass_idx, (dx_off, dy_off, col) in enumerate(((scale, scale, (16, 14, 12)), (0, 0, (245, 240, 225)))):
            for idx, ch_c in enumerate(text):
                glyph = font.get(ch_c, font[" "])
                gx = ox + idx * adv + dx_off
                for gy_i, row_bits in enumerate(glyph):
                    for gx_i, bit in enumerate(row_bits):
                        if bit != "1":
                            continue
                        for sy_i in range(scale):
                            py = oy + dy_off + gy_i * scale + sy_i
                            if not (0 <= py < ch):
                                continue
                            for sx_i in range(scale):
                                px = gx + gx_i * scale + sx_i
                                if 0 <= px < cw:
                                    rows[py][px] = col

    bg_rows = render_canvas(840, 500, 4, draw_logo=False)
    draw_label(bg_rows, 840, 480, "Ported by SweetPalma, lighting by PilotOfAsuka", scale=2)

    write_png8(STAGE / "sce_sys/icon0.png", 128, 128, render_canvas(128, 128, 1))
    write_png8(STAGE / "sce_sys/livearea/contents/bg.png", 840, 500, bg_rows)
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


def sdl2_libs() -> list[str]:
    pc = VITASDK / "arm-vita-eabi/lib/pkgconfig/sdl2.pc"
    if pc.is_file():
        for line in pc.read_text(encoding="utf-8").splitlines():
            if line.startswith("Libs:"):
                return [tok for tok in line.split()[1:] if tok.startswith("-l")]
    return ["-lSDL2"]


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
        ROOT / "src/engine/src/bar.c",
        ROOT / "src/engine/src/moddata.c",
        *sorted((ROOT / "src/engine/src/game").glob("*.c")),
        ROOT / "src/vita/pmd_vita.c",
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

    stage_data = STAGE / "data"
    stage_data.mkdir(parents=True, exist_ok=True)
    for name in ("orcsandelves2.bar", "orcsandelves2.mod"):
        shutil.copy2(DATA_DIR / name, stage_data / name)
    shutil.copytree(DATA_DIR / "audio", stage_data / "audio", dirs_exist_ok=True)

    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    vpk_path = BUILD_ROOT / "Orcs and Elves II.vpk"
    if vpk_path.exists():
        vpk_path.unlink()
    subprocess.run([str(pack_vpk), "-s", str(sfo_path), "-b", str(eboot_path), str(vpk_path)], cwd=STAGE, check=True)
    # Re-pack with all staged assets (sce_sys/icon0.png, livearea, and data/*).
    import zipfile

    with zipfile.ZipFile(vpk_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for file_path in sorted(STAGE.rglob("*")):
            if file_path.is_file():
                zf.write(file_path, file_path.relative_to(STAGE).as_posix())
    sha = hashlib.sha256(vpk_path.read_bytes()).hexdigest()
    print(f"Built {vpk_path} ({vpk_path.stat().st_size} bytes, SHA-256 {sha})")


def main():
    verify_data()
    ensure_audio()
    if STAGE.exists():
        shutil.rmtree(STAGE)
    (STAGE / "sce_sys/livearea/contents").mkdir(parents=True, exist_ok=True)
    build_livearea_assets()
    build_vita_elf()


if __name__ == "__main__":
    main()

