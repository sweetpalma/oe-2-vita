#!/usr/bin/env python3
"""
OE2Data - Comprehensive Data Inspection Tool for Orcs & Elves II (.bar archive)
Standard library only (Python 3.8+).

Parses:
  - BREW .bar resource archives (Resource types, gzip payload decompression)
  - strings.bin (Grouped string tables with hyphens/formatting markers)
  - entities.bin (190 entity definitions: tileIndex, subId, eType, eSubType, parm, names)
  - mappings.bin (Wall, Flat, Sprite mappings and Remap tables)
  - palettes.bin (RGB565 palettes converted to RGB888)
  - bitshapes.bin (Sprite dimensions, bounding boxes, stexels index)
  - Map BSPs (m_01..m_07, dev maps: header, nodes, lines, standing & placed sprites,
    tile grids, events, bytecodes, ASCII visualization)
"""

from __future__ import annotations

import argparse
import gzip
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# Default BAR path relative to repo root
DEFAULT_BAR_PATH = Path(__file__).resolve().parent.parent / "data" / "orcsandelves2.bar"

# Resource IDs
RES_ENTITIES = 1000
RES_SINTABLE = 1001
RES_STRINGS = 1002
RES_PALETTES = 1003
RES_MAPPINGS = 1004
RES_BITSHAPES = 1005
RES_STEXELS = 1006
RES_WTEXELS = 1007
RES_FTEXELS = 1008
RES_MAP_BASE = 1999  # map N is 1999 + N

MAP_NAMES_BY_INDEX = {
    1: "Caverns",
    2: "Dwarkar",
    3: "Tunnels",
    4: "El'Landra",
    5: "Dark Forest",
    6: "Sol",
    7: "Darkfel",
    8: "Ben's Map",
    9: "Brett",
    10: "Items",
    11: "Jah",
    12: "Karina",
    13: "Matt",
    14: "Monster Map",
}


def clean_text(text: str) -> str:
    """Clean game formatting characters (soft hyphens, bars)."""
    return text.replace("\xad", "").replace("-", "-").replace("|", " ").strip()


def normalize_for_search(text: str) -> str:
    """Normalize text for fuzzy search: lowercase, strip hyphens, spaces, punctuation."""
    return "".join(c for c in text.lower() if c.isalnum())


# -----------------------------------------------------------------------------
# BAR Archive
# -----------------------------------------------------------------------------
@dataclass
class BarEntry:
    id: int
    res_type: int
    offset: int
    size: int
    name: str = ""
    is_compressed: bool = False


class BarArchive:
    """Parser for Qualcomm BREW 7.x resource archive (.bar)."""

    def __init__(self, path: Optional[Path | str] = None):
        self.path = Path(path) if path else DEFAULT_BAR_PATH
        if not self.path.exists():
            raise FileNotFoundError(f"Archive not found: {self.path}")

        with open(self.path, "rb") as f:
            self.raw_data = f.read()

        self.entries: Dict[int, BarEntry] = {}
        self.name_to_id: Dict[str, int] = {}
        self._parse_header()

    def _parse_header(self) -> None:
        data = self.raw_data
        if len(data) < 0x20:
            raise ValueError("BAR file too small")

        ntypes = struct.unpack_from("<H", data, 6)[0]
        typedir = struct.unpack_from("<I", data, 8)[0]
        tbl = struct.unpack_from("<I", data, 16)[0]
        tblend = struct.unpack_from("<I", data, 24)[0]
        nent = (tblend - tbl) // 4

        for k in range(ntypes):
            t, first, span, idx = struct.unpack_from("<HHHH", data, typedir + k * 8)
            if t != 6:  # Type 6 = data resource
                continue
            for j in range(span + 1):
                if idx + j >= nent:
                    break
                o = struct.unpack_from("<I", data, tbl + 4 * (idx + j))[0]
                e = (
                    struct.unpack_from("<I", data, tbl + 4 * (idx + j + 1))[0]
                    if idx + j + 1 < nent
                    else len(data)
                )
                if o > len(data) - 2 or e > len(data) or e < o:
                    continue
                hl = struct.unpack_from("<H", data, o)[0]
                if hl < 2 or hl > e - o:
                    continue

                res_id = first + j
                payload_offset = o + hl
                payload_size = e - o - hl
                payload = data[payload_offset : payload_offset + payload_size]

                name = ""
                is_comp = len(payload) > 10 and payload[:2] == b"\x1f\x8b"
                if is_comp:
                    # Parse gzip FNAME if present
                    flags = payload[3]
                    p = 10
                    if flags & 4:  # FEXTRA
                        p += 2 + struct.unpack_from("<H", payload, p)[0]
                    if flags & 8 and p < len(payload):  # FNAME
                        end = payload.find(b"\x00", p)
                        if end != -1:
                            name = payload[p:end].decode("ascii", "replace")

                entry = BarEntry(
                    id=res_id,
                    res_type=t,
                    offset=payload_offset,
                    size=payload_size,
                    name=name,
                    is_compressed=is_comp,
                )
                self.entries[res_id] = entry
                if name:
                    self.name_to_id[name.lower()] = res_id
                    self.name_to_id[Path(name).stem.lower()] = res_id

    def get_raw(self, res_id_or_name: int | str) -> Optional[bytes]:
        res_id = self._resolve_id(res_id_or_name)
        if res_id is None or res_id not in self.entries:
            return None
        ent = self.entries[res_id]
        return self.raw_data[ent.offset : ent.offset + ent.size]

    def get(self, res_id_or_name: int | str) -> Optional[bytes]:
        """Get decompressed payload for a resource."""
        raw = self.get_raw(res_id_or_name)
        if raw is None:
            return None
        if len(raw) > 2 and raw[:2] == b"\x1f\x8b":
            try:
                return gzip.decompress(raw)
            except Exception:
                return raw
        return raw

    def _resolve_id(self, res_id_or_name: int | str) -> Optional[int]:
        if isinstance(res_id_or_name, int):
            return res_id_or_name
        s = str(res_id_or_name).strip().lower()
        if s.isdigit():
            return int(s)
        return self.name_to_id.get(s)


# -----------------------------------------------------------------------------
# Strings Table
# -----------------------------------------------------------------------------
class StringTable:
    """Parser for strings.bin (RES_STRINGS = 1002)."""

    def __init__(self, data: bytes):
        self.groups: List[List[str]] = []
        num_groups = struct.unpack_from("<I", data, 0)[0]
        offsets = struct.unpack_from(f"<{num_groups}I", data, 4)
        for g in range(num_groups):
            start = offsets[g]
            end = offsets[g + 1] if g + 1 < num_groups else len(data) - 4
            chunk = data[4 + start : 4 + end]
            parts = chunk.split(b"\x00")
            strs = [p.decode("latin1", "replace") for p in parts]
            self.groups.append(strs)

    def get(self, group: int, idx: int, clean: bool = False) -> str:
        if 0 <= group < len(self.groups) and 0 <= idx < len(self.groups[group]):
            val = self.groups[group][idx]
            return clean_text(val) if clean else val
        return ""

    def search(self, query: str) -> List[Tuple[int, int, str]]:
        q_norm = normalize_for_search(query)
        q_raw = query.lower()
        results = []
        for g_idx, grp in enumerate(self.groups):
            for s_idx, s in enumerate(grp):
                if q_raw in s.lower() or (
                    q_norm and q_norm in normalize_for_search(s)
                ):
                    results.append((g_idx, s_idx, s))
        return results


# -----------------------------------------------------------------------------
# Entity Definitions
# -----------------------------------------------------------------------------
@dataclass
class EntityDef:
    index: int
    tile_index: int
    sub_id: int
    etype: int
    esubtype: int
    parm: int
    name_id: int
    long_name_id: int
    desc_id: int
    name: str = ""
    long_name: str = ""
    description: str = ""


class EntityTable:
    """Parser for entities.bin (RES_ENTITIES = 1000)."""

    def __init__(self, data: bytes, strings: Optional[StringTable] = None):
        self.defs: List[EntityDef] = []
        num_defs = struct.unpack_from("<H", data, 0)[0]
        for i in range(num_defs):
            off = 2 + i * 13
            (
                tile,
                sub_id,
                etype,
                esubtype,
                parm,
                name_id,
                lname_id,
                desc_id,
            ) = struct.unpack_from("<hBBBhHHH", data, off)

            name = strings.get(1, name_id, clean=False) if strings else ""
            lname = strings.get(1, lname_id, clean=False) if strings else ""
            desc = strings.get(1, desc_id, clean=False) if strings else ""

            ed = EntityDef(
                index=i,
                tile_index=tile,
                sub_id=sub_id,
                etype=etype,
                esubtype=esubtype,
                parm=parm,
                name_id=name_id,
                long_name_id=lname_id,
                desc_id=desc_id,
                name=name,
                long_name=lname,
                description=desc,
            )
            self.defs.append(ed)

    def find_weapon(self, weapon_id: int) -> Optional[EntityDef]:
        for d in self.defs:
            if d.etype == 6 and d.esubtype == 1 and d.parm == weapon_id:
                return d
        return None

    def filter(
        self,
        tile: Optional[int] = None,
        etype: Optional[int] = None,
        query: Optional[str] = None,
    ) -> List[EntityDef]:
        res = self.defs
        if tile is not None:
            res = [d for d in res if d.tile_index == tile]
        if etype is not None:
            res = [d for d in res if d.etype == etype]
        if query:
            q_norm = normalize_for_search(query)
            q_raw = query.lower()
            res = [
                d
                for d in res
                if q_raw in d.name.lower()
                or q_raw in d.long_name.lower()
                or q_raw in d.description.lower()
                or (
                    q_norm
                    and (
                        q_norm in normalize_for_search(d.name)
                        or q_norm in normalize_for_search(d.long_name)
                        or q_norm in normalize_for_search(d.description)
                    )
                )
            ]
        return res


# -----------------------------------------------------------------------------
# Mappings, Palettes & Bitshapes
# -----------------------------------------------------------------------------
@dataclass
class ShapeMap:
    ofs: int
    palette: int


class Mappings:
    """Parser for mappings.bin (RES_MAPPINGS = 1004)."""

    def __init__(self, data: bytes):
        (
            self.n_walls,
            self.n_flats,
            self.n_sprites,
            self.n_wremap,
            self.n_fremap,
            self.n_sremap,
        ) = struct.unpack_from("<6I", data, 0)
        pos = 24

        # Wall maps {texelOfs: u32, palette: u32}
        self.wall_maps: List[ShapeMap] = []
        for _ in range(self.n_walls):
            ofs, pal = struct.unpack_from("<II", data, pos)
            pos += 8
            self.wall_maps.append(ShapeMap(ofs=ofs, palette=pal >> 8))

        # Flat maps {ofs: u32, palette: u32}
        self.flat_maps: List[ShapeMap] = []
        for _ in range(self.n_flats):
            ofs, pal = struct.unpack_from("<II", data, pos)
            pos += 8
            self.flat_maps.append(ShapeMap(ofs=ofs, palette=pal >> 8))

        # Sprite maps {ofs: u32, palette: u32}
        self.sprite_maps: List[ShapeMap] = []
        for _ in range(self.n_sprites):
            ofs, pal = struct.unpack_from("<II", data, pos)
            pos += 8
            self.sprite_maps.append(ShapeMap(ofs=ofs, palette=pal >> 8))

        # Remap tables (s16 arrays)
        self.wall_remap = list(
            struct.unpack_from(f"<{self.n_wremap}h", data, pos)
        )
        pos += self.n_wremap * 2
        self.flat_remap = list(
            struct.unpack_from(f"<{self.n_fremap}h", data, pos)
        )
        pos += self.n_fremap * 2
        self.sprite_remap = list(
            struct.unpack_from(f"<{self.n_sremap}h", data, pos)
        )
        pos += self.n_sremap * 2


class Palettes:
    """Parser for palettes.bin (RES_PALETTES = 1003)."""

    def __init__(self, data: bytes):
        self.num_palettes = struct.unpack_from("<H", data, 0)[0]
        pos = 2
        self.palettes: List[List[Tuple[int, int, int]]] = []
        for _ in range(self.num_palettes):
            count = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            raw_words = struct.unpack_from(f"<{count}H", data, pos)
            pos += count * 2
            rgb_list = []
            for w in raw_words:
                r = ((w >> 11) & 0x1F) << 3
                g = ((w >> 5) & 0x3F) << 2
                b = (w & 0x1F) << 3
                rgb_list.append((r, g, b))
            self.palettes.append(rgb_list)


class Bitshapes:
    """Parser for bitshapes.bin (RES_BITSHAPES = 1005)."""

    def __init__(self, data: bytes):
        self.size = struct.unpack_from("<I", data, 0)[0]
        self.payload = data[4:]

    def get_shape(
        self, ofs: int
    ) -> Optional[Tuple[int, int, int, int, int]]:
        """Returns (stex_idx, first_col, last_col, first_row, last_row)."""
        if ofs + 12 > len(self.payload):
            return None
        stex_idx = struct.unpack_from("<I", self.payload, ofs)[0]
        c1, c2 = self.payload[ofs + 8], self.payload[ofs + 9]
        r1, r2 = self.payload[ofs + 10], self.payload[ofs + 11]
        return (stex_idx, c1, c2, r1, r2)

    def render_silhouette(self, ofs: int, max_width: int = 60) -> str:
        """Render 1-bit ASCII silhouette of the shape mask."""
        sh = self.get_shape(ofs)
        if not sh:
            return ""
        stex, c1, c2, r1, r2 = sh
        w = c2 - c1 + 1
        h = r2 - r1 + 1
        col_bytes = (h + 7) // 8
        grid = [[" " for _ in range(w)] for _ in range(h)]
        raw = self.payload
        mask_start = ofs + 12
        for col in range(w):
            col_off = mask_start + col * col_bytes
            for row in range(h):
                if col_off + row // 8 < len(raw):
                    byte_val = raw[col_off + row // 8]
                    if (byte_val >> (row % 8)) & 1:
                        grid[row][col] = "#"
        lines = []
        step = 2 if h > 20 else 1
        for r in range(0, h, step):
            row_str = "".join(grid[r])
            if row_str.strip():
                lines.append(row_str[:max_width])
        return "\n".join(lines)


# -----------------------------------------------------------------------------
# Map BSP
# -----------------------------------------------------------------------------
@dataclass
class MapSprite:
    x: int
    y: int
    z: int
    tile: int
    info: int
    is_placed: bool
    is_wall_attached: bool = False
    wall_dir: Optional[str] = None


@dataclass
class MapLine:
    x1: int
    y1: int
    x2: int
    y2: int
    texture: int
    texture2: int
    flags: int


@dataclass
class MapData:
    map_id: int
    name: str
    version: int
    hash: int
    floor_color: Tuple[int, int, int]
    ceil_color: Tuple[int, int, int]
    color3: Tuple[int, int, int]
    player_tile: int
    spawn_dir: int
    spawn_tile: int
    floor_height: int
    ceil_height: int
    map_flags_byte: int
    nodes_count: int
    lines: List[MapLine] = field(default_factory=list)
    standing_sprites: List[MapSprite] = field(default_factory=list)
    placed_sprites: List[MapSprite] = field(default_factory=list)
    num_tile_events: int = 0
    num_bytecodes: int = 0
    tile_flags: List[int] = field(default_factory=list)  # 1024 tiles (32x32)
    floor_flats: List[int] = field(default_factory=list)  # 1024 tiles (32x32)
    ceil_flats: List[int] = field(default_factory=list)  # 1024 tiles (32x32)

    def render_ascii(self) -> str:
        """Render a 32x32 grid visualization of the map."""
        grid = [["." for _ in range(32)] for _ in range(32)]

        # Walls & solid tiles
        for idx, fl in enumerate(self.tile_flags):
            x, y = idx % 32, idx // 32
            if fl & 1:  # Blocking / solid
                grid[y][x] = "#"
            elif fl & 2:  # Door or interactive
                grid[y][x] = "+"

        # Standing and placed sprites
        for sp in self.standing_sprites + self.placed_sprites:
            tx = sp.x // 8
            ty = sp.y // 8
            if 0 <= tx < 32 and 0 <= ty < 32:
                if grid[ty][tx] not in ("#", "P"):
                    # Use S for sprites, T for torches/fire, M for monsters
                    if sp.tile in (130, 138, 143, 145, 150, 161, 181):
                        grid[ty][tx] = "*"  # Fire/Light emitter
                    elif 0x11 <= sp.tile <= 0x40:
                        grid[ty][tx] = "M"  # Monster
                    else:
                        grid[ty][tx] = "o"  # Other object

        # Player spawn
        px = self.spawn_tile % 32
        py = self.spawn_tile // 32
        if 0 <= px < 32 and 0 <= py < 32:
            grid[py][px] = "P"

        lines = [f"=== {self.name} (Map {self.map_id}) 32x32 Map Grid ==="]
        lines.append("   " + "".join(f"{x % 10}" for x in range(32)))
        for y in range(32):
            lines.append(f"{y:2d} " + "".join(grid[y]))
        lines.append(
            "Legend: '#' Solid wall, '+' Door/Event, '*' Light/Fire, 'M' Monster, 'o' Item/Sprite, 'P' Player spawn"
        )
        return "\n".join(lines)


def parse_map(data: bytes, map_id: int, strings: Optional[StringTable] = None) -> Optional[MapData]:
    if len(data) < 25:
        return None
    pos = 0
    ver = data[pos]
    pos += 1
    if ver != 10:
        return None
    mhash = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    name_id = struct.unpack_from("<h", data, pos)[0]
    pos += 2

    # Map name
    map_str_grp = map_id + 3
    map_name = (
        strings.get(map_str_grp, name_id, clean=True)
        if strings
        else MAP_NAMES_BY_INDEX.get(map_id, f"Map_{map_id}")
    )
    if not map_name:
        map_name = MAP_NAMES_BY_INDEX.get(map_id, f"Map_{map_id}")

    # Colors
    floor_c = (data[pos + 2], data[pos + 1], data[pos])
    pos += 3
    ceil_c = (data[pos + 2], data[pos + 1], data[pos])
    pos += 3
    color3 = (data[pos + 2], data[pos + 1], data[pos])
    pos += 3

    hdr17628 = data[pos]
    pos += 1
    player_tile = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    spawn_dir = data[pos]
    pos += 1
    spawn_tile = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    floor_h = data[pos]
    pos += 1
    ceil_h = data[pos]
    pos += 1
    flags = data[pos]
    pos += 1

    # Nodes
    num_nodes = struct.unpack_from("<h", data, pos)[0]
    pos += 2
    pos += num_nodes * 10

    # Lines
    num_lines = struct.unpack_from("<h", data, pos)[0]
    pos += 2
    lines = []
    for _ in range(num_lines):
        x1 = data[pos] << 3
        y1 = data[pos + 1] << 3
        x2 = data[pos + 2] << 3
        y2 = data[pos + 3] << 3
        tex1 = data[pos + 4]
        tex2 = data[pos + 5]
        lflags = struct.unpack_from("<I", data, pos + 6)[0]
        pos += 10
        lines.append(
            MapLine(
                x1=x1,
                y1=y1,
                x2=x2,
                y2=y2,
                texture=tex1,
                texture2=tex2,
                flags=lflags,
            )
        )

    # Sprites
    num_standing = struct.unpack_from("<h", data, pos)[0]
    pos += 2
    num_placed = struct.unpack_from("<h", data, pos)[0]
    pos += 2

    standing_sprites = []
    for _ in range(num_standing):
        sx = data[pos] << 3
        sy = data[pos + 1] << 3
        info = data[pos + 2] | (struct.unpack_from("<h", data, pos + 3)[0] << 16)
        pos += 5
        tile = info & 0xFF
        standing_sprites.append(
            MapSprite(
                x=sx,
                y=sy,
                z=0x20,
                tile=tile,
                info=info,
                is_placed=False,
            )
        )

    placed_sprites = []
    for _ in range(num_placed):
        sx = data[pos] << 3
        sy = data[pos + 1] << 3
        sz = data[pos + 2] - 0x20
        b0 = data[pos + 3]
        b1 = data[pos + 4]
        hi = struct.unpack_from("<h", data, pos + 5)[0]
        pos += 7
        info = b0 | (b1 << 8) | (hi << 16)

        tile = info & 0xFF
        if info & 0x40000:
            tile += 0x101

        # Check wall-attached nudge
        is_wall = bool(info & 0x800000)
        wall_dir = None
        if is_wall:
            if info & 0x100000:
                wall_dir = "S"
            elif info & 0x80000:
                wall_dir = "N"
            elif info & 0x200000:
                wall_dir = "E"
            elif info & 0x400000:
                wall_dir = "W"

        placed_sprites.append(
            MapSprite(
                x=sx,
                y=sy,
                z=sz,
                tile=tile,
                info=info,
                is_placed=True,
                is_wall_attached=is_wall,
                wall_dir=wall_dir,
            )
        )

    # Tile events
    num_tile_events = struct.unpack_from("<h", data, pos)[0]
    pos += 2
    if num_tile_events > 0:
        pos += num_tile_events * 2 + num_tile_events * 4

    # Bytecodes
    num_bytecodes = struct.unpack_from("<h", data, pos)[0]
    pos += 2
    if num_bytecodes > 0:
        pos += num_bytecodes * 9

    # Tile flags (512 bytes = 1024 nibbles)
    tile_flags = []
    if pos + 512 <= len(data):
        for _ in range(512):
            b = data[pos]
            pos += 1
            tile_flags.append(b & 0x0F)
            tile_flags.append((b >> 4) & 0x0F)

    # Floor and ceil flats (1024 bytes each)
    floor_flats = []
    if pos + 1024 <= len(data):
        floor_flats = list(data[pos : pos + 1024])
        pos += 1024

    ceil_flats = []
    if pos + 1024 <= len(data):
        ceil_flats = list(data[pos : pos + 1024])
        pos += 1024

    return MapData(
        map_id=map_id,
        name=map_name,
        version=ver,
        hash=mhash,
        floor_color=floor_c,
        ceil_color=ceil_c,
        color3=color3,
        player_tile=player_tile,
        spawn_dir=spawn_dir,
        spawn_tile=spawn_tile,
        floor_height=floor_h,
        ceil_height=ceil_h,
        map_flags_byte=flags,
        nodes_count=num_nodes,
        lines=lines,
        standing_sprites=standing_sprites,
        placed_sprites=placed_sprites,
        num_tile_events=num_tile_events,
        num_bytecodes=num_bytecodes,
        tile_flags=tile_flags,
        floor_flats=floor_flats,
        ceil_flats=ceil_flats,
    )


# -----------------------------------------------------------------------------
# High-Level OE2 Context / Session
# -----------------------------------------------------------------------------
class OE2Context:
    """Unified context holding parsed game assets."""

    def __init__(self, bar_path: Optional[Path | str] = None):
        self.bar = BarArchive(bar_path)
        self._strings: Optional[StringTable] = None
        self._entities: Optional[EntityTable] = None
        self._mappings: Optional[Mappings] = None
        self._palettes: Optional[Palettes] = None
        self._bitshapes: Optional[Bitshapes] = None
        self._maps: Dict[int, MapData] = {}

    @property
    def strings(self) -> StringTable:
        if self._strings is None:
            raw = self.bar.get(RES_STRINGS)
            if not raw:
                raise RuntimeError("Failed to load strings.bin")
            self._strings = StringTable(raw)
        return self._strings

    @property
    def entities(self) -> EntityTable:
        if self._entities is None:
            raw = self.bar.get(RES_ENTITIES)
            if not raw:
                raise RuntimeError("Failed to load entities.bin")
            self._entities = EntityTable(raw, self.strings)
        return self._entities

    @property
    def mappings(self) -> Mappings:
        if self._mappings is None:
            raw = self.bar.get(RES_MAPPINGS)
            if not raw:
                raise RuntimeError("Failed to load mappings.bin")
            self._mappings = Mappings(raw)
        return self._mappings

    @property
    def palettes(self) -> Palettes:
        if self._palettes is None:
            raw = self.bar.get(RES_PALETTES)
            if not raw:
                raise RuntimeError("Failed to load palettes.bin")
            self._palettes = Palettes(raw)
        return self._palettes

    @property
    def bitshapes(self) -> Bitshapes:
        if self._bitshapes is None:
            raw = self.bar.get(RES_BITSHAPES)
            if not raw:
                raise RuntimeError("Failed to load bitshapes.bin")
            self._bitshapes = Bitshapes(raw)
        return self._bitshapes

    def get_map(self, map_id: int) -> Optional[MapData]:
        if map_id in self._maps:
            return self._maps[map_id]
        res_id = RES_MAP_BASE + map_id
        raw = self.bar.get(res_id)
        if not raw:
            return None
        md = parse_map(raw, map_id, self.strings)
        if md:
            self._maps[map_id] = md
        return md


# -----------------------------------------------------------------------------
# CLI Interface
# -----------------------------------------------------------------------------
def cmd_list(ctx: OE2Context, args: argparse.Namespace) -> None:
    print(f"Archive: {ctx.bar.path} ({len(ctx.bar.raw_data)} bytes)")
    print(f"{'ID':>5}  {'Type':>4}  {'Size':>8}  {'Comp':>4}  {'Name'}")
    print("-" * 55)
    for res_id in sorted(ctx.bar.entries.keys()):
        ent = ctx.bar.entries[res_id]
        comp_str = "yes" if ent.is_compressed else "no"
        print(
            f"{ent.id:5d}  {ent.res_type:4d}  {ent.size:8d}  {comp_str:>4}  {ent.name}"
        )


def cmd_extract(ctx: OE2Context, args: argparse.Namespace) -> None:
    raw = ctx.bar.get(args.resource)
    if raw is None:
        print(f"Error: resource '{args.resource}' not found in archive.")
        sys.exit(1)
    out_path = Path(args.output) if args.output else Path(f"res_{args.resource}.bin")
    out_path.write_bytes(raw)
    print(f"Extracted {len(raw)} bytes to {out_path}")


def cmd_strings(ctx: OE2Context, args: argparse.Namespace) -> None:
    st = ctx.strings
    if args.search:
        results = st.search(args.search)
        print(f"Found {len(results)} matches for '{args.search}':")
        for g, idx, text in results:
            print(f"  [Group {g:2d}, Idx {idx:3d}]: {clean_text(text)}")
        return

    if args.group is not None:
        g = args.group
        if not (0 <= g < len(st.groups)):
            print(f"Invalid group {g} (valid: 0..{len(st.groups)-1})")
            return
        print(f"=== String Group {g} ({len(st.groups[g])} strings) ===")
        for idx, text in enumerate(st.groups[g]):
            if text:
                print(f"  [{idx:3d}]: {clean_text(text)}")
    else:
        print(f"String Groups (Total: {len(st.groups)}):")
        for g, grp in enumerate(st.groups):
            first = clean_text(grp[0]) if grp and grp[0] else "(empty)"
            if len(first) > 40:
                first = first[:37] + "..."
            print(f"  Group {g:2d}: {len(grp):3d} strings, sample: '{first}'")


def cmd_entities(ctx: OE2Context, args: argparse.Namespace) -> None:
    et = ctx.entities
    defs = et.filter(tile=args.tile, etype=args.type, query=args.search)
    print(f"Found {len(defs)} entities matching criteria:")
    print(f"{'Idx':>3} {'Tile':>4} {'Type':>7} {'Parm':>4} {'Name':<20} {'Long Name'}")
    print("-" * 65)
    for d in defs:
        t_str = f"{d.etype}:{d.esubtype}"
        name = clean_text(d.name)
        lname = clean_text(d.long_name)
        print(f"{d.index:3d} {d.tile_index:4d} {t_str:>7} {d.parm:4d} {name:<20} {lname}")


def cmd_weapons(ctx: OE2Context, args: argparse.Namespace) -> None:
    print("=== Player Weapons in Orcs & Elves II ===")
    print(f"{'Wpn#':>4} {'SubId':>5} {'Tile':>4} {'Name':<16} {'Long Name':<20} {'Description'}")
    print("-" * 75)
    for w in range(11):
        ed = ctx.entities.find_weapon(w)
        if ed:
            name = clean_text(ed.name)
            lname = clean_text(ed.long_name)
            desc = clean_text(ed.description)
            if len(desc) > 30:
                desc = desc[:27] + "..."
            print(
                f"{w:4d} {ed.sub_id:5d} {ed.tile_index:4d} {name:<16} {lname:<20} {desc}"
            )
        else:
            print(f"{w:4d}   (not defined in entities.bin)")


def cmd_map(ctx: OE2Context, args: argparse.Namespace) -> None:
    map_id = None
    if str(args.map).isdigit():
        map_id = int(args.map)
    else:
        # Match by name
        m_name = str(args.map).lower()
        for mid, name in MAP_NAMES_BY_INDEX.items():
            if m_name in name.lower():
                map_id = mid
                break

    if map_id is None:
        print(f"Could not identify map '{args.map}'")
        return

    md = ctx.get_map(map_id)
    if not md:
        print(f"Map {map_id} could not be parsed.")
        return

    print(f"=== Map {md.map_id}: {md.name} ===")
    print(f"  Version: {md.version}, Hash: 0x{md.hash:08X}")
    print(
        f"  Floor Color (RGB): {md.floor_color}, Ceil Color (RGB): {md.ceil_color}"
    )
    print(
        f"  Spawn Tile: {md.spawn_tile} (x={md.spawn_tile%32}, y={md.spawn_tile//32}), Dir: {md.spawn_dir}"
    )
    print(
        f"  Nodes: {md.nodes_count}, Lines: {len(md.lines)}, Events: {md.num_tile_events}, Bytecodes: {md.num_bytecodes}"
    )
    print(
        f"  Standing Sprites: {len(md.standing_sprites)}, Placed Sprites: {len(md.placed_sprites)}"
    )

    if args.ascii:
        print()
        print(md.render_ascii())

    if args.sprites:
        print("\nPlaced Sprites:")
        print(f"  {'Tile':>4} {'Grid(X,Y)':>10} {'Height(Z)':>9} {'Wall Attached':<14} {'Name'}")
        print("  " + "-" * 55)
        for sp in md.placed_sprites:
            gx, gy = sp.x // 8, sp.y // 8
            w_str = f"Yes ({sp.wall_dir})" if sp.is_wall_attached else "No"
            # find entity name if any
            ed_list = ctx.entities.filter(tile=sp.tile)
            name = clean_text(ed_list[0].name) if ed_list else ""
            print(
                f"  {sp.tile:4d} {f'({gx},{gy})':>10} {sp.z:9d} {w_str:<14} {name}"
            )


def cmd_emitters(ctx: OE2Context, args: argparse.Namespace) -> None:
    """Analyze light emitter candidate tiles and occurrences across all campaign maps."""
    print("=== Light Emitter Analysis across Campaign Maps (1..7) ===")

    # Known candidates based on entity names and tile IDs
    candidate_tiles = {
        130: "Fire",
        138: "Fire Portal Brazier (Sol / Darkfel)",
        143: "Torch / Sconce (Campaign)",
        145: "Fire Portal Brazier / Sconce",
        150: "Wall Sconce",
        161: "Flame Barrier (Sol / Darkfel)",
        181: "Wall Sconce",
    }

    tile_counts: Dict[int, Dict[int, int]] = {t: {} for t in candidate_tiles}

    for mid in range(1, 8):
        md = ctx.get_map(mid)
        if not md:
            continue
        all_sprites = md.standing_sprites + md.placed_sprites
        for sp in all_sprites:
            if sp.tile in candidate_tiles:
                tile_counts[sp.tile][mid] = tile_counts[sp.tile].get(mid, 0) + 1

    print(f"{'Tile':>4}  {'Description':<32}  {'Maps & Counts'}")
    print("-" * 65)
    for tile, desc in candidate_tiles.items():
        counts = tile_counts[tile]
        if counts:
            map_str = ", ".join(f"M{m}:{c}" for m, c in sorted(counts.items()))
        else:
            map_str = "(none placed directly in M1-M7)"
        print(f"{tile:4d}  {desc:<32}  {map_str}")


def cmd_tile(ctx: OE2Context, args: argparse.Namespace) -> None:
    tile = args.tile
    print(f"=== Tile {tile} Report ===")

    # 1. Entity definitions using this tile
    ed_list = ctx.entities.filter(tile=tile)
    if ed_list:
        print(f"\nAssociated Entities ({len(ed_list)}):")
        for ed in ed_list:
            print(
                f"  [{ed.index}] Type {ed.etype}:{ed.esubtype}, Parm {ed.parm}: {clean_text(ed.name)} / {clean_text(ed.long_name)}"
            )
            if ed.description:
                print(f"      Description: {clean_text(ed.description)}")
    else:
        print("\nNo entities directly specify this tileIndex.")

    # 2. Mappings and sprite frames
    if tile < len(ctx.mappings.sprite_remap):
        remap_base = ctx.mappings.sprite_remap[tile]
        remap_next = (
            ctx.mappings.sprite_remap[tile + 1]
            if tile + 1 < len(ctx.mappings.sprite_remap)
            else len(ctx.mappings.sprite_maps)
        )
        frame_count = max(0, remap_next - remap_base)
        print(
            f"\nSprite Frames (Remap base: {remap_base}, Count: {frame_count}):"
        )
        for f in range(remap_base, min(remap_next, remap_base + 16)):
            sm = ctx.mappings.sprite_maps[f]
            sh = ctx.bitshapes.get_shape(sm.ofs)
            if sh:
                stex, c1, c2, r1, r2 = sh
                w = c2 - c1 + 1
                h = r2 - r1 + 1
                print(
                    f"  Frame {f - remap_base}: ofs={sm.ofs}, pal={sm.palette}, stex={stex}, size={w}x{h} (cols {c1}..{c2}, rows {r1}..{r2})"
                )
            else:
                print(f"  Frame {f - remap_base}: ofs={sm.ofs}, pal={sm.palette}")

        if args.ascii and frame_count > 0:
            sm0 = ctx.mappings.sprite_maps[remap_base]
            print("\nFrame 0 Silhouette Preview:")
            print(ctx.bitshapes.render_silhouette(sm0.ofs))

    # 3. Placement in campaign maps
    print("\nOccurrences in Campaign Maps (1..7):")
    total_found = 0
    for mid in range(1, 8):
        md = ctx.get_map(mid)
        if not md:
            continue
        matches = [
            sp
            for sp in (md.standing_sprites + md.placed_sprites)
            if sp.tile == tile
        ]
        if matches:
            total_found += len(matches)
            coords = ", ".join(f"({sp.x//8},{sp.y//8})" for sp in matches[:6])
            if len(matches) > 6:
                coords += f" ... (+{len(matches)-6} more)"
            print(f"  Map {mid} ({md.name}): {len(matches)} placed at {coords}")
    if total_found == 0:
        print("  (not placed in maps 1..7)")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="OE2Data: Data Inspection Tool for Orcs & Elves II",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--bar",
        type=str,
        default=None,
        help="Path to orcsandelves2.bar (default: data/orcsandelves2.bar)",
    )

    subparsers = parser.add_subparsers(dest="command", help="Subcommand to run")

    # list
    subparsers.add_parser("list", help="List all resources in the archive")

    # extract
    p_ext = subparsers.add_parser("extract", help="Extract a resource to file")
    p_ext.add_argument("resource", help="Resource ID or file name")
    p_ext.add_argument("-o", "--output", help="Output file path")

    # strings
    p_str = subparsers.add_parser("strings", help="Inspect strings.bin")
    p_str.add_argument("group", nargs="?", type=int, help="String group (0..20)")
    p_str.add_argument(
        "-s", "--search", type=str, help="Search string across all groups"
    )

    # entities
    p_ent = subparsers.add_parser("entities", help="Inspect entities.bin")
    p_ent.add_argument("-t", "--tile", type=int, help="Filter by tileIndex")
    p_ent.add_argument("--type", type=int, help="Filter by eType")
    p_ent.add_argument(
        "-s", "--search", type=str, help="Search by name or description"
    )

    # weapons
    subparsers.add_parser("weapons", help="List all player weapons and stats")

    # map
    p_map = subparsers.add_parser("map", help="Inspect map BSP")
    p_map.add_argument("map", help="Map number (1..14) or name (e.g. Sol)")
    p_map.add_argument(
        "--ascii", action="store_true", help="Print 32x32 ASCII visualization"
    )
    p_map.add_argument(
        "--sprites", action="store_true", help="List all placed sprites"
    )

    # emitters
    subparsers.add_parser(
        "emitters", help="Analyze light emitter sources across maps"
    )

    # tile
    p_tile = subparsers.add_parser(
        "tile", help="Inspect a specific sprite/tile ID"
    )
    p_tile.add_argument("tile", type=int, help="Tile ID (e.g. 138, 143, 161)")
    p_tile.add_argument(
        "--ascii", action="store_true", help="Render silhouette of frame 0"
    )

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(0)

    try:
        ctx = OE2Context(args.bar)
    except Exception as e:
        print(f"Error initializing OE2Context: {e}")
        sys.exit(1)

    cmds = {
        "list": cmd_list,
        "extract": cmd_extract,
        "strings": cmd_strings,
        "entities": cmd_entities,
        "weapons": cmd_weapons,
        "map": cmd_map,
        "emitters": cmd_emitters,
        "tile": cmd_tile,
    }

    cmd_fn = cmds.get(args.command)
    if cmd_fn:
        cmd_fn(ctx, args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
