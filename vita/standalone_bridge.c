/* Standalone presentation adapter for PS Vita and native SDL2 hosts.
 * Preserves exact Classic simulation, RNG, and binary saves while adding
 * toggleable Remastered point lighting, ray shadows, and contact AO. */
#include "standalone_bridge.h"
#include "game.h"
#include "lighting.h"
#include "presentation.h"
#include <string.h>

static int remasteredMode = 1;
static int rangeSteps = 15; /* 5, 15, or 20 steps */
static uint32_t worldPixels[640 * 640];
static uint32_t litPixels[640 * 640];
static uint16_t lit565[640 * 640];
static int worldX, worldY, worldW, worldH, worldValid;
static int cineCallsThisFrame;
static int toastFrames;
static const char *toastText = "";

static uint32_t to_rgba(uint16_t c)
{
	return (((c >> 11) & 31) * 255 / 31) |
	       ((((c >> 5) & 63) * 255 / 63) << 8) |
	       (((c & 31) * 255 / 31) << 16) |
	       0xff000000u;
}

static uint16_t to_rgb565(uint32_t rgba)
{
	uint32_t r = ((rgba & 0xffu) * 31u + 127u) / 255u;
	uint32_t g = (((rgba >> 8) & 0xffu) * 63u + 127u) / 255u;
	uint32_t b = (((rgba >> 16) & 0xffu) * 31u + 127u) / 255u;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

static void apply_world_lighting(int x, int y, int w, int h)
{
	IBitmap *b = Brew_screen();
	if (!b || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > b->width || y + h > b->height)
		return;
	BrewLight_setRange(rangeSteps);
	const uint32_t *src = remasteredMode ? BrewLight_render(worldPixels, w, h)
	                                     : BrewLight_classic(worldPixels, w, h);
	if (!src)
		src = worldPixels;
	memcpy(litPixels, src, (size_t)w * (size_t)h * sizeof(uint32_t));
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			int idx = row * w + col;
			uint16_t c565 = to_rgb565(litPixels[idx]);
			lit565[idx] = c565;
			b->px[(y + row) * b->pitch + x + col] = c565;
		}
	}
	worldX = x;
	worldY = y;
	worldW = w;
	worldH = h;
	worldValid = 1;
}

void OE2_captureDetails(const String *stats)
{
	(void)stats;
}

void OE2_captureStory(int x, int y)
{
	(void)x;
	(void)y;
}

void OE2_captureStoryBase(int x, int y)
{
	(void)x;
	(void)y;
}

void OE2_captureTravel(int x, int y, const String *name)
{
	(void)x;
	(void)y;
	(void)name;
}

void OE2_projectileImpact(const GameSprite *missile, int flaming)
{
	BrewLight_impact(missile, flaming);
}

void OE2_captureWorld(const int16_t *rect)
{
	IBitmap *b = Brew_screen();
	int x = rect[0], y = rect[1], w = rect[2], h = rect[3];
	if (!b || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > b->width || y + h > b->height)
		return;
	for (int row = 0; row < h; row++)
		for (int col = 0; col < w; col++)
			worldPixels[row * w + col] = to_rgba(b->px[(y + row) * b->pitch + x + col]);
	apply_world_lighting(x, y, w, h);
	if (g_app && g_app->canvas && g_app->canvas->state == 0x13) {
		/* Draw the native padlock indicator over the lit 3D view. */
		Canvas *cv = g_app->canvas;
		Render_draw2DSprite(g_app->render, 0xd, cv->lockCorrectHits, cv->screenCenterX - 0x2c,
		                    cv->renderRect[1] + 0x14, 0, 0, 0x100, 0);
	}
}

void OE2_captureRenderedWorld(Render *r)
{
	if (!r || !g_app || !g_app->game || !g_app->canvas)
		return;
	if (g_app->canvas->snapView) {
		int16_t rect[4] = { (int16_t)r->fbOffsetX, (int16_t)r->fbOffsetY, (int16_t)r->width, (int16_t)r->height };
		OE2_captureWorld(rect);
		return;
	}
	if (!Game_isCinematicActive(g_app->game))
		return;
	/* In ST_CINEMATIC:
	 * Call 1 is at the end of Render_render (before Canvas_updateFade).
	 * Call 2 is at the end of Camera_apply (after Canvas_updateFade).
	 * Call 3 is after ParticleSystem_update (before letterbox/captions). */
	cineCallsThisFrame++;
	if (cineCallsThisFrame == 2) {
		int16_t rect[4] = { (int16_t)r->fbOffsetX, (int16_t)r->fbOffsetY, (int16_t)r->width, (int16_t)r->height };
		OE2_captureWorld(rect);
	}
}

void OE2_setRemastered(int enabled)
{
	remasteredMode = enabled ? 1 : 0;
}

int OE2_isRemastered(void)
{
	return remasteredMode;
}

int OE2_toggleRemastered(void)
{
	remasteredMode = !remasteredMode;
	toastText = remasteredMode ? "REMASTERED" : "CLASSIC";
	toastFrames = 30;
	if (worldValid) {
		IBitmap *b = Brew_screen();
		if (b && worldX >= 0 && worldY >= 0 && worldX + worldW <= b->width && worldY + worldH <= b->height) {
			BrewLight_setRange(rangeSteps);
			const uint32_t *src = remasteredMode ? BrewLight_render(worldPixels, worldW, worldH)
			                                     : BrewLight_classic(worldPixels, worldW, worldH);
			if (!src)
				src = worldPixels;
			for (int row = 0; row < worldH; row++) {
				for (int col = 0; col < worldW; col++) {
					int idx = row * worldW + col;
					uint16_t old565 = lit565[idx];
					uint16_t new565 = to_rgb565(src[idx]);
					litPixels[idx] = src[idx];
					lit565[idx] = new565;
					if (b->px[(worldY + row) * b->pitch + worldX + col] == old565)
						b->px[(worldY + row) * b->pitch + worldX + col] = new565;
				}
			}
		}
	}
	return remasteredMode;
}

void OE2_cycleLightRange(void)
{
	if (rangeSteps == 5) {
		rangeSteps = 15;
		toastText = "LIGHT: MED";
	} else if (rangeSteps == 15) {
		rangeSteps = 20;
		toastText = "LIGHT: HIGH";
	} else {
		rangeSteps = 5;
		toastText = "LIGHT: LOW";
	}
	toastFrames = 30;
	BrewLight_setRange(rangeSteps);
}

int OE2_getLightRangeSteps(void)
{
	return rangeSteps;
}

/* Minimal 4x5 uppercase glyph table for the mode toggle toast. */
static uint32_t glyph_bits(char c)
{
	switch (c) {
	case 'A': return 0x69f99u;
	case 'C': return 0x78887u;
	case 'D': return 0xe999eu;
	case 'E': return 0xf8e8fu;
	case 'G': return 0x78b97u;
	case 'H': return 0x99f99u;
	case 'I': return 0xe444eu;
	case 'L': return 0x8888fu;
	case 'M': return 0x9ff99u;
	case 'O': return 0x69996u;
	case 'R': return 0xe9e99u;
	case 'S': return 0x7861eu;
	case 'T': return 0xf4444u;
	case 'W': return 0x99ff9u;
	case ':': return 0x04040u;
	default:  return 0u;
	}
}

static void draw_toast(uint32_t *outRgba, int scrW, int scrH)
{
	if (toastFrames <= 0 || !toastText || !*toastText)
		return;
	toastFrames--;
	int len = (int)strlen(toastText);
	int boxW = len * 5 + 5;
	int boxH = 9;
	int x0 = scrW - boxW - 3;
	int y0 = 20;
	if (x0 < 0 || y0 + boxH > scrH)
		return;
	for (int y = y0; y < y0 + boxH; y++)
		for (int x = x0; x < x0 + boxW; x++)
			outRgba[y * scrW + x] = (x == x0 || x == x0 + boxW - 1 || y == y0 || y == y0 + boxH - 1)
			                        ? 0xff5088b8u : 0xff10141cu;
	for (int i = 0; i < len; i++) {
		uint32_t bits = glyph_bits(toastText[i]);
		int gx = x0 + 3 + i * 5;
		int gy = y0 + 2;
		for (int r = 0; r < 5; r++)
			for (int c = 0; c < 4; c++)
				if ((bits >> ((4 - r) * 4 + (3 - c))) & 1u)
					outRgba[(gy + r) * scrW + (gx + c)] = 0xffd8f0ffu;
	}
}

static int is_3d_state(int st)
{
	return st == 3 || st == 4 || st == 7 || st == 9 || st == 12 ||
	       st == 17 || st == 18 || st == 19 || st == 20 || st == 21;
}

void OE2_presentFrame(const uint16_t *fb, int pitchPixels, int scrW, int scrH, uint32_t *outRgba)
{
	cineCallsThisFrame = 0;
	for (int y = 0; y < scrH; y++) {
		const uint16_t *row = fb + y * pitchPixels;
		uint32_t *dst = outRgba + y * scrW;
		for (int x = 0; x < scrW; x++)
			dst[x] = to_rgba(row[x]);
	}
	if (worldValid && g_app && g_app->canvas && is_3d_state(g_app->canvas->state)) {
		if (worldX >= 0 && worldY >= 0 && worldX + worldW <= scrW && worldY + worldH <= scrH) {
			for (int row = 0; row < worldH; row++) {
				const uint16_t *fbRow = fb + (worldY + row) * pitchPixels + worldX;
				const uint16_t *lit565Row = lit565 + row * worldW;
				const uint32_t *litRow = litPixels + row * worldW;
				uint32_t *dstRow = outRgba + (worldY + row) * scrW + worldX;
				for (int col = 0; col < worldW; col++) {
					if (fbRow[col] == lit565Row[col])
						dstRow[col] = litRow[col];
				}
			}
		}
	}
	draw_toast(outRgba, scrW, scrH);
}

