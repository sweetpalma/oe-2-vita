# Orcs & Elves II - BREW Resources & Binary Formats

This document specifies the reverse-engineered binary structures, asset containers, and data models of *Orcs & Elves II* (BREW 7.0.41 build). All multi-byte integer values in these files are stored in **little-endian** byte order unless explicitly noted.

Tooling reference: All resources described below can be directly queried, extracted, and inspected using [tools/oe2data.py](file:///Users/palma/Documents/GitHub/personal/orcs-elves-ii/tools/oe2data.py).

---

## 1. BREW Archive Container (`orcsandelves2.bar`)

The main game archive is a Qualcomm BREW resource package containing 199 indexed resources.

### 1.1 Archive Header
Located at byte offset `0x00`:

| Offset | Type | Description | Value in OE2 |
|---|---|---|---|
| `0x00` | `uint16` | Archive format version | `0x0011` (17) |
| `0x02` | `uint16` | Reserved / flags | `0x0000` |
| `0x04` | `uint16` | Reserved | `0x0000` |
| `0x06` | `uint16` | Number of type-directory entries (`ntypes`) | `12` |
| `0x08` | `uint32` | Byte offset of type directory (`typedir`) | `0x00000020` |
| `0x0C` | `uint32` | Reserved | `0x00000000` |
| `0x10` | `uint32` | Byte offset of entry table (`tbl`) | `0x00000080` |
| `0x14` | `uint32` | Reserved | `0x00000000` |
| `0x18` | `uint32` | Offset of first resource / end of table (`tblend`) | Variable |

### 1.2 Type Directory
Contains `ntypes` entries (8 bytes each):
```
struct TypeDirEntry {
    uint16_t type;        // 6 = Data resource (payload), 1 = String resource
    uint16_t firstId;     // Starting resource ID for this range
    uint16_t span;        // (lastId - firstId); number of entries minus 1
    uint16_t firstIndex;  // Index in entry offset table
};
```
In *Orcs & Elves II*, all core assets are Type 6 (Data) resources grouped into:
- `1000..1008`: Core engine tables (`entities.bin`, `strings.bin`, `palettes.bin`, etc.)
- `2000..2013`: Map BSP bodies (`m_01.bsp`..`m_07.bsp`, dev/test maps)
- `3000..3050`: UI bitmaps (`.bmp`)
- `4000..4114`: PMD audio sound effects and MIDI compositions (`.pmd`)

### 1.3 Resource Records & Gzip Compression
Each entry in the table points to a raw record:
```
[uint16 header_len] [MIME string (header_len - 2 bytes)] [Payload bytes...]
```
If the payload begins with magic bytes `0x1F 0x8B`, it is compressed with **gzip**.
The uncompressed filename is stored in the gzip header (`flags & 0x08`, null-terminated ASCII starting at byte offset 10 + optional extra fields).

---

## 2. Entity Definition Table (`entities.bin` / Resource 1000)

Loaded in [src/engine/src/game/entity.c](file:///Users/palma/Documents/GitHub/personal/orcs-elves-ii/src/engine/src/game/entity.c#L133-L170).

### 2.1 File Header
- `uint16 count`: Total entity definitions (`190` in OE2).

### 2.2 Record Format (13 bytes each)
```
struct EntityDefRecord {
    int16_t  tileIndex;     // Tile number in graphics sheet (0 = abstract / weapon / inventory item)
    uint8_t  subId;         // Sub-identifier / inventory slot
    uint8_t  eType;         // High-level category (see below)
    uint8_t  eSubType;      // Sub-category (see below)
    int16_t  parm;          // Parameter / weapon slot / behavior value
    uint16_t nameId;        // String index in String Group 1 (short name)
    uint16_t longNameId;    // String index in String Group 1 (long/display name)
    uint16_t descId;        // String index in String Group 1 (quest/log description)
};
```

### 2.3 Entity Types & Hierarchy
- `eType 1`: Monsters & NPCs (subtype: 1 Orc, 2 Troll, 3 Skeleton, 4 Wererat, 5 Worg, 6 Wraith, etc.)
- `eType 2`: Barrels, chests, and breakable environmental containers
- `eType 6`: Items & Equipment:
  - `eSubType 1`: Weapons (`parm` = weapon slot `0..10`):
    - `parm 0`: Bare Hands / Sling ("Wand" before Bob upgrade)
    - `parm 1`: Staff (Heavy melee / boulder pusher)
    - `parm 2`: Dagger (Fast melee)
    - `parm 3`: Poison Dagger (Replaces Dagger upon upgrade)
    - `parm 4`: Familiar Wand ("Bob" - scouts and attacks)
    - `parm 5`: Fire Crossbow ("Fire Bow" - shoots flaming bolts)
    - `parm 6`: Kinetic Crossbow ("Ki-ne-tic Bow" - explosive area damage)
    - `parm 7`: Flaming Sword (High damage fire melee)
    - `parm 8`: Shrink Wand Orbs
    - `parm 9`: Mold Wand Orbs
    - `parm 10`: Freeze Wand Orbs
  - `eSubType 2`: Armor & Cloaks (Dragon armor, Diamond armor)
  - `eSubType 3`: Ammunition (Crossbow bolts)
  - `eSubType 4`: Potions & Consumables (Health, Mana, Ale, Antidote)
  - `eSubType 5`: Rings (Strength, Regeneration, Protection)
- `eType 7`: Wall Decorations, Sconces, Switches (`eSubType 9` = Wall sconces)
- `eType 8`: Standalone Environmental Objects (`eSubType 0` = Fire sources)
- `eType 9`: Spawners, Teleporters & Traps

---

## 3. String Tables (`strings.bin` / Resource 1002)

Loaded in [src/engine/src/game/core_text.c](file:///Users/palma/Documents/GitHub/personal/orcs-elves-ii/src/engine/src/game/core_text.c#L23-L72).

### 3.1 Structure
- `uint32 numGroups`: Number of string groups (`21`).
- `uint32 groupOffsets[numGroups]`: Byte offset for the start of each group (relative to the end of the offset table).
- Followed by sequential blocks of **null-terminated Latin-1 strings**.
- To look up string `(group, index)`:
  - Start offset = `groupOffsets[group]`
  - End offset = `groupOffsets[group + 1]`
  - Index `i` is the `i`-th null-terminated string within that group slice.

### 3.2 String Groups Map
| Group | Description | First String Sample |
|---|---|---|
| `0` | UI prompts & Tutorials | `TALK\|Press OK (key 5)...` |
| `1` | Entity Names, Items, Weapons | `Wand`, `Staff`, `Fire Cross-bow` |
| `2` | Game Credits | `CRE-DITS... JOHN CARMACK` |
| `3` | HUD strings & Menu items | `PLA-YER`, `Help`, `In-ven-to-ry` |
| `4` | Map 1: Caverns | Level dialogue and quest notes |
| `5` | Map 2: Dwarkar | Level dialogue and inscriptions |
| `6` | Map 3: Tunnels | Level dialogue |
| `7` | Map 4: El'Landra | Level dialogue |
| `8` | Map 5: Dark Forest | Level dialogue |
| `9` | Map 6: Sol | Level dialogue & final battle setup |
| `10` | Map 7: Darkfel | Level dialogue |
| `11..18` | Developer/Test Maps | `Ben's Map`, `Brett`, `Items`, `Monster Map` |
| `19` | Companion Barks | `Take that!`, `Nice hit!`, `Bril-liant!` |

### 3.3 Text Formatting & Special Characters
- `\xad` / `-`: Soft hyphen (syllable break for BREW screen wrapping).
- `|`: Hard line break (`\n`).
- `%1`..`%6`: Positional format specifiers replaced at runtime with player/monster names, stats, or counts.
- `\%`: Escaped literal percent sign.

---

## 4. Map BSP Format (`m_01.bsp`..`m_07.bsp` / Resources 2000..2013)

Loaded in [src/engine/src/game/render_load.c](file:///Users/palma/Documents/GitHub/personal/orcs-elves-ii/src/engine/src/game/render_load.c#L770-L1170).

### 4.1 Header (25 bytes)
```
struct MapHeader {
    uint8_t  version;         // Always 10 in OE2
    uint32_t mapHash;         // Verification hash
    int16_t  nameId;          // String index in Group (mapIdx + 3)
    uint8_t  floorColor[3];   // Ambient floor color (B, G, R)
    uint8_t  ceilColor[3];    // Ambient ceiling color (B, G, R)
    uint8_t  color3[3];       // Fog / accent color (B, G, R)
    uint8_t  hdr17628;        // Render flags
    uint16_t playerTile;      // Grid tile of player
    uint8_t  spawnDir;        // Initial player facing (0=E, 1=S, 2=W, 3=N)
    uint16_t spawnTile;       // Player spawn grid tile (x = tile % 32, y = tile / 32)
    uint8_t  floorHeight;     // Base floor height
    uint8_t  ceilHeight;      // Base ceiling height
    uint8_t  mapFlags;        // Global map behavior flags
};
```

### 4.2 BSP Nodes (10 bytes each)
- `int16 numNodes`: Total 2D BSP splitting nodes.
- Per node:
  - `uint8 x1, y1, x2, y2`: Bounding box coordinates (multiplied by 8 for world units).
  - `uint8 axis`: Splitting axis (0 = X split, 1 = Y split).
  - `uint8 splitCoord`: Coordinate value on the splitting axis (`val << 3`).
  - `int16 leftChild`: Child node index / leaf pointer.
  - `int16 rightChild`: Child node index / leaf pointer.

### 4.3 Wall Lines (10 bytes each)
- `int16 numLines`: Total line segments forming the 3D walls.
- Per line:
  - `uint8 x1, y1, x2, y2`: Endpoint grid units (`coord << 3`).
  - `uint8 texture`: Texture index on front face.
  - `uint8 texture2`: Texture index on back face.
  - `uint32 flags`: Line flags:
    - `0x0008`: Door line (triggers nudge 2 units off tile edge).
    - `0x0100` / `0x0200`: Orientation axes for sliding doors.
    - `0x8000`: Texture U coordinate inversion.

### 4.4 Sprites
- `int16 numStanding`: Number of floor-standing sprites.
- `int16 numPlaced`: Number of placed 3D sprites.

#### Standing Sprites (5 bytes each)
- `uint8 x, y`: Grid coordinates (`val << 3`). `z` is fixed at `0x20` (floor level).
- `uint8 infoLow`: Tile number `0..255`.
- `int16 infoHigh`: Packed flags (hidden `0x10000`, flip `0x1000000`).

#### Placed 3D Sprites (7 bytes each)
- `uint8 x, y`: Grid coordinates (`val << 3`).
- `uint8 zRaw`: World elevation (`z = zRaw - 0x20`).
- `uint8 info0, info1`: Packed tile and animation parameters.
- `int16 infoHigh`: Upper flags:
  - `0x40000`: Uses wall texture index instead of sprite sheet (`tile += 0x101`).
  - `0x800000`: Wall-attached decoration (torch, sconce, banner). Automatically nudged 1 world unit outward from the wall:
    - `0x100000`: South face (`y += 1`)
    - `0x080000`: North face (`y -= 1`)
    - `0x200000`: East face (`x += 1`)
    - `0x400000`: West face (`x -= 1`)

### 4.5 Tile Events & Bytecode
- `int16 numTileEvents`: Number of reactive tile event triggers.
  - `int16 eventTiles[numTileEvents]`: Grid tile index for each trigger.
  - `uint32 tileEvents[numTileEvents]`: Event flags and pointer into bytecode array.
- `int16 numByteCodes`: Total script instruction records.
  - Records are 9 bytes: `{uint8 opcode, int32 arg, int32 flags}`.
  - Key opcodes:
    - `0x19 ANIM`: Start animated sequence on sprite or tile.
    - `0x22 CHANGESPRITE`: Preload and swap a sprite frame or wall texture.
    - `0x53 AWARDSECRET`: Award secret discovery score to player.

### 4.6 32x32 Grid Layout Arrays (2,560 bytes)
Maps have a fixed grid resolution of **32 × 32 cells** (1,024 tiles total):
1. **Tile Flags (512 bytes)**:
   - 1,024 4-bit nibbles (2 tiles per byte):
     - `bit 0`: Solid / blocking collision.
     - `bit 1`: Door / passage trigger.
     - `bit 4`: Secret area flag.
2. **Floor Flat IDs (1,024 bytes)**:
   - 1 byte per tile pointing to the floor texture in `ftexels.bin`.
3. **Ceiling Flat IDs (1,024 bytes)**:
   - 1 byte per tile pointing to the ceiling texture in `ftexels.bin`.

---

## 5. Graphics Mappings, Palettes, Bitshapes & Texels

Loaded in [src/engine/src/game/render_load.c](file:///Users/palma/Documents/GitHub/personal/orcs-elves-ii/src/engine/src/game/render_load.c#L155-L740).

### 5.1 `mappings.bin` (Resource 1004)
Contains six 32-bit counts:
```
struct MappingsHeader {
    uint32_t numWalls;        // 220
    uint32_t numFlats;        // 24
    uint32_t numSprites;      // 1057
    uint32_t numWallRemap;    // 192
    uint32_t numFlatRemap;    // 218
    uint32_t numSpriteRemap;  // 256
};
```
Followed by:
- `numWalls` × 8 bytes: `{uint32 texelOfs, uint32 palette}`
- `numFlats` × 8 bytes: `{uint32 ofs, uint32 palette}`
- `numSprites` × 8 bytes: `{uint32 ofs, uint32 palette}`
- `numWallRemap` × `int16`
- `numFlatRemap` × `int16`
- `numSpriteRemap` × `int16`: Translates `tileIndex` into the starting frame index in `spriteMap`.

### 5.2 `palettes.bin` (Resource 1003)
- `uint16 numPalettes`: Total color palettes (`507`).
- Per palette:
  - `uint16 count`: Number of 16-bit color words.
  - `uint16 colors[count]`: Colors in **RGB565** format (`R:5, G:6, B:5`).
- Runtime conversion to RGB888:
  ```c
  r = ((color >> 11) & 0x1F) << 3;
  g = ((color >>  5) & 0x3F) << 2;
  b = ( color        & 0x1F) << 3;
  ```

### 5.3 `bitshapes.bin` (Resource 1005)
Stores the non-rectangular bounding box and 1-bit transparency mask for each sprite frame:
- `uint32 size`: Total bytes of shape data (`166,701`).
- Per shape:
  - `sh[0..3]`: 32-bit index into `stexels.bin`.
  - `sh[8], sh[9]`: First column (`c1`) and last column (`c2`). Width = `c2 - c1 + 1`.
  - `sh[10], sh[11]`: First row (`r1`) and last row (`r2`). Height = `r2 - r1 + 1`.
  - `sh[12...]`: 1-bit transparency mask column by column.
    - Each column takes `(height + 7) / 8` bytes.
    - Bit `y & 7` of byte `(y / 8)` indicates whether the texel is visible.

### 5.4 Texels (`stexels.bin`, `wtexels.bin`, `ftexels.bin`)
- Begins with a `uint32 count` followed by `count` 32-bit file offsets.
- Each texture entry starts with a format flag byte:
  - `4`: Packed 4-bit per pixel (2 texels per byte, low nibble first). Texels index into the frame's palette.
  - Other: Raw 8-bit uncompressed palette indices.

