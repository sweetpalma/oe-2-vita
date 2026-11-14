# Orcs & Elves II Source

This directory contains the full C source code for **Orcs & Elves II**, organized into three distinct layers:

```
src/
├── engine/     # BREW 7.0.41 game engine (simulation, gameplay, software rasterizer)
├── host/       # Remastered presentation bridge (lighting, ray shadows, contact AO)
└── vita/       # PlayStation Vita platform adapter (SDL2 GXM, SceCtrl, SceAudio)
```

## Engine

The pure reconstructed game engine, faithfully preserving the original BREW 7.0.41 implementation:

- **`src/engine/src/game/`**:
  - **Simulation & Timing:** Preserves the original 40 ms (`25 Hz`) simulation loop (`app.c`, `main.c`), fixed-point math, and deterministic RNG.
  - **Game Logic:** Turn-based entity AI (`entity_ai.c`, `game_monsters.c`), player progression and inventory (`player.c`, `player_items.c`), spells and weapons (`combat_weapon.c`, `combat_attack.c`), scripting system (`script.c`, `script_run.c`), and automap (`game_world.c`).
  - **Software Renderer:** Original fixed-point column raycaster and span rasterizer (`render.c`, `render_span.c`, `render_sprite.c`, `canvas.c`).
- **Data & Archive Loaders:**
  - `bar.c` / `bar.h`: BREW Binary App Resource (`.bar`) directory and resource extractor.
  - `moddata.c` / `moddata.h`: Reads original compiled static tables from `data/orcsandelves2.mod`.
  - `pmd.c` / `pmd.h`: Qualcomm CMF wave track extractor.
  - `qcelp.c` / `qcelp.h`: Embedded standalone QCELP-13k audio decoder (shared between PC and Vita).

## Host Bridge

The presentation layer that adds modern graphical enhancements without affecting game simulation or binary saves:

- **`lighting.c` / `lighting.h`:** Real-time point lighting, dynamic candle/torch flicker, geometric ray-cast shadows, and contact ambient occlusion.
- **`presentation.c` / `presentation.h`:** Screen buffers and post-processing adapters.
- **`standalone_bridge.c` / `standalone_bridge.h`:** Decouples game logic from the display platform:
  - Intercepts 3D frames (`OE2_captureWorld`, `OE2_presentFrame`).
  - Switches between Classic and Remastered modes in real time.
  - Manages configuration persistence (`LightingCfg`) and tick rate toggling (`OE2_getTickMs`).

## Vita Port

Platform-specific backend tailored for the PS Vita hardware:

- **`main_vita.c`:** Main application loop, controller auto-repeat timing, and process lifecycle (`sceKernelExitProcess`).
- **`platform_vita.c`:**
  - **Graphics:** SDL2 hardware-accelerated GXM renderer (`960×544`).
  - **Controls:** Direct `SceCtrl` sampling with analog stick deadzones, face buttons, shoulder triggers, and merchant offer adjustments.
  - **Audio:** SceAudio / SDL2 audio output with Kaiser-windowed sinc resampler.
  - **Storage:** Redirects saves and configuration to `ux0:data/oe2/`.
