#ifndef OE2_GAME_H
#define OE2_GAME_H

/* Single include for every engine source file. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "brew_shim.h"
#include "moddata.h"
#include "platform.h"
#include "protos.h"
#include "types.h"

/* The original reached every subsystem through GETAPPINSTANCE(); the port keeps
 * one global applet instead. */
extern Applet *g_app;

#ifdef OE2_CINEMATIC_CAPTURE
/* Offline camera precision; absent from normal native/browser builds. */
void Film_adjustView(Render *render);
void Film_offsetFloor(int *x, int *y);
float Film_offsetX(void);
float Film_offsetY(void);
#endif

/* Presentation-only arrival flash; never changes combat or sprite lifetime. */
void OE2_projectileImpact(const GameSprite *missile, int flaming);
/* Host presentation hook: reads the finished world before the original HUD. */
void OE2_captureWorld(const int16_t *rect);
void OE2_captureRenderedWorld(Render *render);
void BrewLight_begin(Render *r);
void BrewView_begin(Render *r);
void BrewView_line(Render *r, const Line *line);
void BrewView_sprite(Render *r, int sprite, int frame, int x, int y, int a,
                     int blend, int scale, int flip);
void BrewView_effect(Render *r, int type, int value);
void BrewLight_floor(Render *r, int x, int y, int u, int v, int du, int dv,
                     int count, int ceiling);
void BrewLight_span(Render *r, int x, int y, int count, int projection,
                    int sprite);
void BrewLight_weapon(Render *r, int x, int y, int count, int blend);
void BrewLight_tagCaster(Render *r, Sprite *sprite);
void BrewLight_poseCaster(Render *r, int x, int y, int z, int tile, int frame,
                          int flags, int mode, int scale);

/* Engine lifecycle used by main.c (implemented in app.c). */
Applet *App_create(int screenW, int screenH);
void App_destroy(Applet *app);

/* Report something the port does not handle yet, once per call site. */
#define OE2_UNIMPLEMENTED(what)                                                \
  do {                                                                         \
    static int once_;                                                          \
    if (!once_) {                                                              \
      once_ = 1;                                                               \
      Oe2_warn("unimplemented: %s (%s:%d)", what, __FILE__, __LINE__);         \
    }                                                                          \
  } while (0)
void Oe2_warn(const char *fmt, ...);
void Oe2_log(const char *fmt, ...);

/* 1 << n as the ARM computed it (LSL by register): only the low byte of n
 * counts, and shifts of 32..255 give 0. In C a shift >= 32 is undefined and x86
 * would wrap it to n & 31. */
static inline uint32_t arm_bit(int n) {
  uint32_t s = (uint32_t)n & 0xff;
  return s < 32 ? 1u << s : 0u;
}

#endif
