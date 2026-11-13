# Dynamic Lighting & Remastered Presentation

This document details the architecture, mathematics, light selection algorithms, geometric ray-tracing, ambient occlusion, sprite lighting models, and performance optimizations implemented in `src/host/lighting.c`.

---

## 1. System Overview

The dynamic lighting engine is a screen-space / world-space hybrid. Rather than modifying the fixed-point software rasterizer, the rasterizer logs geometric metadata during span and column passes into a `Surface` buffer. The lighting system then performs ray-traced geometric shading, dynamic light evaluation, and tone mapping directly onto the output pixels.

```
Rasterizer Passes (render.c, render_span.c, render_sprite.c)
   │
   ├── BrewLight_floor()   ──> Writes Floor / Ceiling Surfaces (kind = 1)
   ├── BrewLight_span()    ──> Writes Wall Surfaces (kind = 2) or Sprites (kind = 3)
   └── BrewLight_weapon()  ──> Writes Weapon Overlay Surfaces (kind = 4)
                                    │
                                    ▼
                          Surface Buffer [W × H]
                                    │
                                    ▼
       BrewLight_render() / samplePointQ10() / incidentEnergy()
   ┌─────────────────────────────────────────────────────────────┐
   │ 1. Light Candidate Gathering & View-Frustum Scoring         │
   │ 2. Geometric Line-Casting Shadow Rays (visibilityAt)        │
   │ 3. Ambient Occlusion & Contact Shading (contactShade)       │
   │ 4. Half-Lambert Diffuse Wrap Shading for Billboards         │
   │ 5. Quadratic Distance Attenuation & Torch Flicker           │
   │ 6. Tone Mapping & Color Space LUTs (byteToLinear, toneLUT)  │
   └─────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                         Lit Framebuffer [W × H]
```

---

## 2. Surface Buffer & Receiver Geometry

Each pixel on screen stores geometric metadata in a `Surface` structure:

```c
typedef struct {
  float x, y, z;          // World space coordinate (units: 64 = 1 tile)
  signed char nx, ny, nz; // Normal vector components scaled to [-127, 127]
  unsigned char kind;     // Surface classification
} Surface;
```

### Surface Classifications (`kind`)

- **`0`**: Unlit / Background / Sky (no lighting applied).
- **`1`**: Floor or Ceiling plane (`nz = +127` for floor, `-127` for ceiling).
- **`2`**: Architectural Wall plane (normal calculated from the BSP line segment).
- **`3`**: Billboard Quad Sprite (monsters, NPCs, items, decorations; normal faces the camera: `nx = -fx, ny = -fy`).
- **`4`**: Player Weapon overlay (illuminated in weapon space, ignores world distance fog).
- **`5`**: Special floor/ceiling variants.

---

## 3. Light Source Selection & Prioritization

### 3.1. Emitter Registration (`sources[]`)

Static props and active sprites register light properties in `sources[]`:

```c
typedef struct {
  int tile;               // Sprite / tile ID
  float r, g, b;          // Color channels (0.0 .. 1.0)
  float radius;           // Falloff radius in world units
  float power;            // Intensity multiplier
  float height;           // Vertical Z offset from sprite base
  int flicker;            // 1 = animated torch/fire flicker, 0 = constant
} Source;
```

#### Key Emitter IDs:

- **Torches & Wall Sconces:** Tile `116`, `130`, `136`, `143`, `145`, `149`, `150`, `181`, `234`.
- **Healing Crystals:** Tile `162`, `165` (`r=0.15, g=0.45, b=1.0`, cyan glow).
- **Fire Portal Braziers & Barriers:** Tile `138`, `145` (braziers), `161` (fire barrier).
- **Dragon / Boss (Gaya):** Tile `20` (body) and `191` (head/flame) in OE1; Tile `185` in OE2.

#### Player & Held Weapon Lights:

- **Inherent Player Ambient Light:** Always active at low intensity (`radius = 105 * 1.2`, `power = 0.28`, `r=1.0, g=0.62, b=0.28`).
- **Fire Crossbow (`w == 5`), Diamond / Kinetic Crossbow (`w == 6`), Flaming Sword (`w == 7`):** Full 100% firelight (`radius = 210 * 1.2`, `power = 1.15`, `r=1.0, g=0.38, b=0.08`).
- **Wand Orbs (`w == 8..10`):** Tuned to 75% intensity (`radius = 210 * 1.2`, `power = 0.86`).
  - Shrink Orb (`w == 8`): Red light (`r=1.0, g=0.18, b=0.10`).
  - Mold Orb (`w == 9`): Green light (`r=0.25, g=1.0, b=0.35`).
  - Freeze Orb (`w == 10`): Ice blue light (`r=0.35, g=0.65, b=1.0`).
- **Emissive Weapon Sprite Self-Illumination:** Light-emitting held weapons (`w in {0, 5, 6, 7, 8, 9, 10}`) receive a 1.4x brightness scaling with a minimum floor of 1.25 to prevent dark silhouettes.

### 3.2. View-Frustum Scoring (`selectionScore`)

Because a map may contain dozens of light sources, the top 7 world lights must be chosen based on visual impact.

```c
static float selectionScore(int key, float lx, float ly, float distanceSquared) {
  float dx = lx - processingX, dy = ly - processingY;
  float forward = dx * fx + dy * fy;
  float dist = sqrtf(distanceSquared);
  float cosA = dist > .01f ? forward / dist : 1.f;
  float dirWeight;

  if (forward >= 0.f) {
    // Forward view cone: strong priority boost (up to 4x better score)
    dirWeight = 1.f - 0.75f * fmaxf(0.f, cosA);
  } else {
    // Behind camera: heavily penalized (up to 10x penalty)
    dirWeight = 1.f + 4.f * (-forward / (WALK_STEP * 2.f));
    if (dirWeight > 10.f) dirWeight = 10.f;
  }

  float score = dist * dirWeight;
  // Hysteresis: retain previously active lights to avoid frame-to-frame flicker
  for (int i = 0; i < retainedCount; i++) {
    if (retainedKeys[i] == key) return score * 0.75f;
  }
  return score;
}
```

---

## 4. Ray-Traced Geometric Shadows

Shadows are computed via geometric line intersection against the BSP world lines (`Render.lines`):

- **Shadow Grid (`visibilityAt`):** The world is partitioned into a 16-unit grid. Ray segments from each grid sample point `(x, y, z)` toward the light source `(lx, ly, lz)` are tested against non-transparent BSP walls (`lineOccludes`).
- **Self-Shadow Avoidance:** The receiver surface position is nudged along its surface normal by 2 world units:
  ```c
  float x = (sx + s->nx / 127.f * 2.f) / 16.f;
  float y = (sy + s->ny / 127.f * 2.f) / 16.f;
  ```
- **Performance Optimization (`OE2_NO_SPRITE_SHADOWS`):**
  On PS Vita and PC, `OE2_NO_SPRITE_SHADOWS` is enabled. Sprites do not cast volumetric shadows onto the shadow grid, removing expensive per-frame sprite occluder rebuilds while preserving 100% of wall, column, and door shadows.

---

## 5. Shading Models & Critical Pitfalls

### 5.1. Half-Lambert Wrap Shading for Billboard Sprites

- **Problem:** Billboard sprites have a planar normal facing the camera (`nx = -fx, ny = -fy`). Under standard Lambertian diffuse shading (`N · L`), as soon as the player walks past a torch or the torch is slightly to the side/rear, `N · L <= 0`, causing the character sprite to abruptly turn completely black.
- **Solution:** Apply Half-Lambert diffuse wrap to billboard sprites (`kind == 3`):
  ```c
  float ndotl = (s->nx * dx + s->ny * dy + snz * dz) / (127.f * d);
  float lambert = (kind == 3) ? clampf(ndotl * 0.5f + 0.5f, 0.f, 1.f)
                              : clampf(ndotl, 0.f, 1.f);
  ```
  This creates smooth, three-dimensional wrap-around lighting on 2D monster and NPC sprites.

### 5.2. Floor Contact Shading Pitfall on Billboards

- **Problem:** `contactShade()` computes ambient occlusion near wall-floor corners using distance to floor:
  `distance = fabsf(sz - renderer->floorZ);`
  For standing character billboards, the bottom pixels are at floor height (`distance ≈ 0`), which caused `contactShade` to evaluate to `0.35` (a **65% penalty** on ambient and direct lighting). This made characters standing on the ground look artificially crushed and dark.
- **Solution:** Billboard sprites must be exempted from floor crease AO:
  ```c
  int kind = surfKind(s);
  if (kind == 4 || kind == 3) return 1.0f;
  ```

### 5.3. Distance Culling Pitfall (`processingSteps` & `worldLightWeight`)

- **Problem:** `processingSteps` was originally initialized to `5` (5 tiles = 320 units).
  - In `lightInRegion`, lights further than `reach = 5 * 64 + radius` (around 500 units) from the player were completely discarded from selection.
  - In `incidentEnergy()`, `worldLightWeight(sx, sy)` computed distance from the _player_ to the surface. Beyond 5 tiles, it returned `0.0`.
  - When standing across a large cavern (e.g. Dragon Lair, 10–14 tiles away), Gaya and distant cavern walls received **zero dynamic light**, even when standing right next to a burning brazier!
- **Solution:**
  1. Set `processingSteps = 26` by default (covering up to 1664 world units).
  2. Allow `BrewLight_setRange` up to `32` steps (2048 units).
  3. Exempt sprites (`kind == 3`) from player distance culling:
     ```c
     float weight = (kind == 3 || kind == 4) ? 1.0f : worldLightWeight(sx, sy);
     if (weight <= 0.0f) return;
     ```

### 5.4. Distance Haze Red-Channel Crushing

- **Problem:** `samplePointQ10()` blends energy with a dark mist haze (`0.035, 0.065, 0.105`). At depths > 128 units, multiplying by `(1 - haze)` stripped warm red tones from distant sprites, turning Gaya into a dark silhouette.
- **Solution:** Restrict haze color crushing to non-sprite environmental geometry:
  ```c
  if (kind != 4 && kind != 3 && fadeLevel > 0) { ... }
  ```

---

## 6. Color Space & Tone Mapping

Lighting values are evaluated in linear radiometric units and mapped to display gamma through precomputed lookup tables:

- **`byteToLinear[256]`**: Converts sRGB color bytes `v` to linear intensity: `(v / 255)^2`.
- **`byteToLinearQ10[256]`**: 10-bit fixed-point linear representation (`* 1024.f`) for fast integer SIMD/multi-core shading.
- **`toneLUT[4096]`**: Reinhard-style soft shoulder curve:
  ```c
  float lin = i * (4.f / 4096.f);
  float mapped = lin > 1.f ? lin / (1.f + (lin - 1.f) * 0.7f) : lin;
  toneLUT[i] = (uint8_t)(clampf(sqrtf(mapped), 0.f, 1.f) * 255.f + 0.5f);
  ```
  Prevents blown-out color banding when multiple lights overlap while preserving bright specular highlights.

---

## 7. PS Vita Performance Optimizations

1. **Multi-threaded Slicing:** Column spans are sliced across a 4-thread worker pool (`LightWorkerCtx`).
2. **Q10 Fixed-Point Interpolation:** `samplePointQ10` processes low-resolution irradiance grids and bilinearly interpolates across wall spans in integer arithmetic.
3. **Atomic Cache Flags:** `wallCheckedAtomic` avoids redundant ray-casts across thread boundaries using lock-free acquire/release semantics.
