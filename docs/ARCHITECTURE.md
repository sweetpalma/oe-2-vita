# Engine Architecture & Code Structure

This document details the architectural layout, core subsystems, execution lifecycle, asset formats, and platform abstractions in the project source tree.

---

## 1. Directory Layout

```
.
├── docs/                   # Technical documentation and findings
├── data/                   # Original game assets (.bar, .mod) - ignored by Git
├── tools/                  # Build scripts, asset decoders, font generators, vitasdk vendor
│   ├── build_pc.py         # Automated PC builds
│   └── build_vita.py       # Complete Vita VPK package pipeline
└── src/
    ├── engine/             # Faithful C reconstruction of BREW 7.0.41 game engine
    │   ├── src/
    │   │   ├── game/       # Simulation, entities, scripts, menus, software rasterizer
    │   │   ├── bar.c/.h    # BREW .bar archive reader & decompressor
    │   │   ├── moddata.c/.h# Static table parser from .mod data segment
    │   │   ├── pmd.c/.h    # QCELP audio decoder (PC/tools)
    │   │   └── main.c      # Standalone/PC entry point & test harness (OE2_PIPE)
    ├── host/               # Remastered presentation bridge (lighting, shaders, framebuffers)
    │   ├── lighting.c/.h   # Dynamic lighting, ray-cast shadows, ambient occlusion
    │   ├── lighting_gpu.inc# GPU/compute shader export helpers
    │   ├── presentation.c  # View and effect callbacks
    │   └── standalone_bridge.c # Classic/Remastered mode toggle, tick rate controller
    └── vita/               # PlayStation Vita platform backend
        ├── main_vita.c     # Vita main loop, controller input polling
        ├── platform_vita.c # SDL2 GXM display, SceCtrl handling, SceAudio, save redirection
        └── pmd_vita.c      # Fast streaming loader for pre-decoded PCM audio
```

---

## 2. Engine Subsystem (`src/engine/src/game/`)

The gameplay foundation is reconstructed directly from the disassembled binary of id Software / Fountainhead Entertainment's BREW release (version 7.0.41).

### 2.1. The 40 ms Simulation Step

- The native game operates on a fixed **40 ms step** (`25 Hz` simulation tick).
- `app.c`: `Applet_HandleEvent` and `Canvas_step` drive world updates.
- In `standalone_bridge.c`, `OE2_getTickMs()` allows switching between the standard `40 ms` step and an unlocked/fast mode (`16 ms` / `~60 Hz`).
- Simulation timing is strictly decoupled from presentation: turn animations, combat dice rolls, projectile trajectories, and tile triggers run synchronously per tick.

### 2.2. Coordinate Systems & Units

- **Grid Tiles:** The map consists of a 2D tile grid (typically 32×32 tiles).
- **World Units:** Each walking step / map tile is **`64 world units`** (`WALK_STEP = 64.f`).
  - Tile index `(tx, ty)` maps to world coordinate center `(tx * 64 + 32, ty * 64 + 32)`.
  - Tile elevation (`z`): standard floor level is typically `0` or `32`, ceiling height is typically `64` or `128` (or custom per level in large caverns).
- **View Angles:** Angles use a 10-bit integer ring: **`0 .. 1023`** (`0x3ff` mask).
  - `0` (`0x000`): Facing **East** (+X)
  - `256` (`0x100`): Facing **North** (-Y)
  - `512` (`0x200`): Facing **West** (-X)
  - `768` (`0x300`): Facing **South** (+Y)
- **Trigonometry:** Sine and cosine are looked up via `render->sinTable` (1024 entries, 16.16 fixed-point).

### 2.3. Canvas State Machine

The core UI and gameplay states are managed in `Canvas.state`:

- `0`: Logos / Splash screens
- `2`: Main menu / Options
- `3`: Active 3D gameplay (first-person navigation)
- `4`: Turn-based Combat sequence
- `5`: Automap viewer
- `6`: Loading level
- `7`: Dialogue / Conversation overlay
- `8`: Lockpicking minigame
- `9`: Story / Book scroll text
- `0xc`: Travel map (overworld navigation between levels)
- `0xd`: Player death / Game Over
- `0x11`: Credits
- `0x12`: Saving game
- `0x14`: Cinematic / Scripted scene
- `0x15`: Fire Portal transition

### 2.4. Software Renderer (`render.c`, `render_span.c`, `render_sprite.c`)

- **BSP Traversal:** Walls and sector leaves are stored in a Binary Space Partitioning tree loaded from `.bsp` files (`m01.bsp`..`m08.bsp`, `hub.bsp`).
- **Wall Projection:** `Render_clipSegmentNear` clips lines against the camera near plane; column top/bottom bounds are projected via fixed-point division (`render->maxX16 / depth`).
- **Span Rasterization:** Wall columns and floor/ceiling spans are rendered in software (`render_span.c`). In Remastered mode, hooks (`BrewLight_floor`, `BrewLight_span`, `BrewLight_weapon`) log pixel surface coordinates and surface normals into a surface buffer (`Surface surfaces[]`).
- **Billboard Sprites:** Sprites (monsters, NPCs, items, decorations) are projected and sorted by distance in `render_sprite.c`. Their surface kind is tagged as `kind = 3` (billboard quad).

---

## 3. Host Presentation Bridge (`src/host/`)

The bridge wraps the software-rasterized framebuffer and enhances it without mutating gameplay logic or save file formats.

```
       Game Engine Render
               │
               ▼
   BrewLight_span / floor / weapon   ──> Populate Surface Buffer (X, Y, Z, Normal, Kind)
               │
               ▼
       Software Framebuffer
               │
               ▼
       BrewLight_render()             ──> Dynamic Lighting, Ray Shadows & Ambient Occlusion
               │
               ▼
     Final Output to Display (SDL2 / Vita GXM)
```

- **`standalone_bridge.c`**:
  - Exposes `OE2_captureWorld()` and `OE2_presentFrame()`.
  - Maintains `remasteredMode` boolean flag (toggleable in real time).
  - Handles letterboxing / aspect ratio correction (e.g. 16:9 widescreen or original 4:3 / phone ratios).
- **`lighting.c`**:
  - Complete Remastered lighting pipeline:
    - Point light emitters with flicker animation.
    - View-frustum directional light scoring.
    - Geometric line occluders traced against BSP wall lines.
    - Ambient occlusion and contact shading.
    - Detailed breakdown in [LIGHTING.md](file:///Users/palma/Documents/GitHub/personal/orcs-elves-ii-remastered/docs/LIGHTING.md).

---

## 4. Asset Architecture (`data/`)

The game relies on two primary data files:

### 4.1. The Resource Archive (`.bar`)

- Standard BREW resource archive (`orcsandelves.bar`).
- Read by `src/engine/src/bar.c`.
- **Header:**
  - `0x06` (16-bit): Number of resource types (`ntypes`).
  - `0x08` (32-bit): Type directory offset.
  - `0x10` (32-bit): Resource table offset.
  - `0x18` (32-bit): Resource table end.
- Individual entries may be uncompressed or GZIP-compressed (identified by magic bytes `0x1f 0x8b`).
- Resources include textures (`7000..7512`), wall bitmaps (`7512..8000`), sprite graphics (`9000+`), maps (`6000..6008`), and palette/mapping metadata (`5005..5007`).

### 4.2. Bytecode & Static Data (`.mod`)

- Compiled ARM/BREW module file (`orcsandelves.mod`).
- `src/engine/src/moddata.c` maps directly into this file to read:
  - Game entity definitions, monster attributes, health, and armor tables.
  - Dialogue strings, quest logs, and script event command tables.
  - Overworld travel node paths (`MOD_TRAVEL_NODES`).

### 4.3. Audio Pipeline

- Mobile BREW audio uses Qualcomm QCELP-13k compression (`.pmd` CMF resources in `.bar`).
- Both PC and PS Vita embed a standalone, zero-dependency QCELP-13k audio decoder (`src/engine/src/qcelp.c`).
- Audio is decoded on demand directly from `.bar` in memory and cached on first play, requiring no offline preprocessing or external audio files.

---

## 5. PlayStation Vita Platform Adapter (`src/vita/`)

The Vita port directly binds the engine to Sony PlayStation Vita hardware libraries:

- **Graphics (`platform_vita.c`):**
  - Uses SDL2 configured for the Vita's hardware GXM backend.
  - Render target: Native OLED/LCD resolution (`960×544`).
  - Frame presentation stretches the 3D viewport while maintaining the original UI frame scaling.
- **Controls (`platform_vita.c`):**
- **Storage:**
  - Redirection of saves, user preferences, and lighting configuration to `ux0:data/oe2/saves/` (with automatic migration from legacy `ux0:data/oe2/`).
