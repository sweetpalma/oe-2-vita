#!/usr/bin/env python3
"""Generate PlayStation Vita LiveArea and system assets into src/vita/assets."""
from pathlib import Path
import argparse
import gzip
import os
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = ROOT / "src/vita/assets"


def resolve_data_dir() -> Path:
    env = os.environ.get("OE2_DATA_DIR")
    if env:
        return Path(env)
    for p in (ROOT / "data", ROOT / "build/pc", ROOT / "gamedata"):
        if (p / "orcsandelves2.bar").is_file():
            return p
    return ROOT / "data"


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
            buckets.sort(
                key=lambda b: max(max(c[k] for c in b) - min(c[k] for c in b) for k in range(3)) if len(b) > 1 else -1,
                reverse=True,
            )
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
            lut[c] = min(
                range(len(palette)),
                key=lambda i: (palette[i][0] - r) ** 2 + (palette[i][1] - g) ** 2 + (palette[i][2] - b) ** 2,
            )

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
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"PLTE", bytes(plte))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def build_livearea_assets(bar_path: Path, output_dir: Path):
    if not bar_path.is_file():
        raise RuntimeError(f"Cannot find orcsandelves2.bar at {bar_path}")

    bar_bytes = bar_path.read_bytes()
    bmp_3034 = extract_bar_resource(bar_bytes, 3034)
    bmp_3037 = extract_bar_resource(bar_bytes, 3037)
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

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "livearea/contents").mkdir(parents=True, exist_ok=True)

    write_png8(output_dir / "icon0.png", 128, 128, render_canvas(128, 128, 1))
    write_png8(output_dir / "livearea/contents/bg.png", 840, 500, bg_rows)
    write_png8(output_dir / "livearea/contents/startup.png", 280, 158, render_canvas(280, 158, 2))

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
    (output_dir / "livearea/contents/template.xml").write_text(template_xml, encoding="utf-8")
    print(f"Generated LiveArea assets in {output_dir}")


def parse_args():
    parser = argparse.ArgumentParser(description="Generate PS Vita LiveArea assets into src/vita/assets")
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="Directory containing orcsandelves2.bar (default: auto-detected from data/ or OE2_DATA_DIR)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Target output directory (default: src/vita/assets)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    data_dir = args.data_dir or resolve_data_dir()
    bar_path = data_dir / "orcsandelves2.bar"
    if not bar_path.is_file():
        raise RuntimeError(
            f"Could not find orcsandelves2.bar in {data_dir}.\n"
            "Please provide a valid data directory with --data-dir <dir> or OE2_DATA_DIR."
        )
    build_livearea_assets(bar_path, args.output_dir)


if __name__ == "__main__":
    main()
