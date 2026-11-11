/* Standalone presentation adapter for PS Vita and native SDL2 hosts.
 * Preserves exact Classic simulation, RNG, and binary saves while adding
 * toggleable Remastered point lighting, ray shadows, and contact AO. */
#include "standalone_bridge.h"
#include "game.h"
#include "lighting.h"
#include "platform.h"
#include "presentation.h"
#include <stdlib.h>
#include <string.h>

static int remasteredMode = 1;
static int rangeSteps = 5; /* 5 (Low default), 15 (Med), or 20 (High) steps */
static int hostConfigLoaded = 0;
static int fastTickMode = 0;
static uint32_t worldPixels[640 * 640];
static uint32_t litPixels[640 * 640];
static uint16_t lit565[640 * 640];
static uint32_t lut565ToRgba[65536];
static int lut565Ready;
static int worldX, worldY, worldW, worldH, worldValid;
static uint32_t lastFrameSig;
static int lastViewX, lastViewY, lastViewZ, lastViewAngle, stationaryCount;
static int cineCallsThisFrame;
static int toastFrames;
static const char *toastText = "";

static void ensure_lut565(void)
{
	if (lut565Ready)
		return;
	for (uint32_t c = 0; c < 65536u; c++) {
		lut565ToRgba[c] = (((c >> 11) & 31u) * 255u / 31u) |
		                  ((((c >> 5) & 63u) * 255u / 63u) << 8) |
		                  (((c & 31u) * 255u / 31u) << 16) |
		                  0xff000000u;
	}
	lut565Ready = 1;
}

static inline uint32_t to_rgba(uint16_t c)
{
	return lut565ToRgba[c];
}

static inline uint16_t to_rgb565(uint32_t rgba)
{
	uint32_t r = ((rgba & 0xffu) * 249u + 1014u) >> 11;
	uint32_t g = (((rgba >> 8) & 0xffu) * 253u + 505u) >> 10;
	uint32_t b = (((rgba >> 16) & 0xffu) * 249u + 1014u) >> 11;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t hash_world_pixels(int count)
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < count; i += 4)
		h = (h ^ worldPixels[i]) * 16777619u;
	return h;
}

static void apply_world_lighting(int x, int y, int w, int h)
{
	IBitmap *b = Brew_screen();
	if (!b || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > b->width || y + h > b->height)
		return;
	if (!hostConfigLoaded)
		OE2_loadHostConfig();
	ensure_lut565();
	int count = w * h;
	uint32_t sig = hash_world_pixels(count);
	int vx = (g_app && g_app->render) ? g_app->render->viewX : 0;
	int vy = (g_app && g_app->render) ? g_app->render->viewY : 0;
	int vz = (g_app && g_app->render) ? g_app->render->viewZ : 0;
	int va = (g_app && g_app->canvas) ? g_app->canvas->viewAngle : 0;
	int samePose = worldValid && worldX == x && worldY == y && worldW == w && worldH == h &&
	               sig == lastFrameSig && vx == lastViewX && vy == lastViewY &&
	               vz == lastViewZ && va == lastViewAngle;
	if (samePose && (stationaryCount++ & 1) == 0) {
		for (int row = 0; row < h; row++)
			memcpy(&b->px[(y + row) * b->pitch + x], &lit565[row * w], (size_t)w * sizeof(uint16_t));
		return;
	}
	if (!samePose)
		stationaryCount = 0;
	lastFrameSig = sig;
	lastViewX = vx;
	lastViewY = vy;
	lastViewZ = vz;
	lastViewAngle = va;

	BrewLight_setRange(rangeSteps);
	const uint32_t *src = remasteredMode ? BrewLight_render(worldPixels, w, h)
	                                     : BrewLight_classic(worldPixels, w, h);
	if (!src)
		src = worldPixels;
	for (int row = 0; row < h; row++) {
		uint16_t *dst565 = &b->px[(y + row) * b->pitch + x];
		for (int col = 0; col < w; col++) {
			int idx = row * w + col;
			uint32_t rgba = src[idx];
			uint16_t c565 = to_rgb565(rgba);
			litPixels[idx] = rgba;
			lit565[idx] = c565;
			dst565[col] = c565;
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
	ensure_lut565();
	for (int row = 0; row < h; row++) {
		const uint16_t *srcRow = &b->px[(y + row) * b->pitch + x];
		uint32_t *dstRow = &worldPixels[row * w];
		for (int col = 0; col < w; col++)
			dstRow[col] = lut565ToRgba[srcRow[col]];
	}
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

void OE2_loadHostConfig(void)
{
	uint8_t *data = NULL;
	size_t len = 0;
	if (Plat_fileRead("LightingCfg", &data, &len) == 0) {
		if (len >= 8 && data[0] == 'O' && data[1] == 'E' && data[2] == 'L' && data[3] == 'C') {
			remasteredMode = data[4] ? 1 : 0;
			if (data[5] == 5 || data[5] == 15 || data[5] == 20)
				rangeSteps = data[5];
		}
		free(data);
	}
	const char *e = getenv("OE2_REMASTERED");
	if (e)
		remasteredMode = atoi(e) ? 1 : 0;
	BrewLight_setRange(rangeSteps);
	lastFrameSig = 0;
	hostConfigLoaded = 1;
}

void OE2_saveHostConfig(void)
{
	if (getenv("OE2_REMASTERED"))
		return;
	uint8_t buf[8] = {
		'O', 'E', 'L', 'C',
		(uint8_t)(remasteredMode ? 1 : 0),
		(uint8_t)rangeSteps,
		0, 0
	};
	Plat_fileWrite("LightingCfg", buf, sizeof(buf));
}

static void ensure_host_config(void)
{
	if (!hostConfigLoaded)
		OE2_loadHostConfig();
}

void OE2_setRemastered(int enabled)
{
	ensure_host_config();
	remasteredMode = enabled ? 1 : 0;
	lastFrameSig = 0;
	OE2_saveHostConfig();
}

int OE2_isRemastered(void)
{
	ensure_host_config();
	return remasteredMode;
}

int OE2_toggleRemastered(void)
{
	ensure_host_config();
	remasteredMode = !remasteredMode;
	lastFrameSig = 0;
	OE2_saveHostConfig();
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
	ensure_host_config();
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
	lastFrameSig = 0;
	toastFrames = 30;
	BrewLight_setRange(rangeSteps);
	OE2_saveHostConfig();
}

int OE2_getLightRangeSteps(void)
{
	ensure_host_config();
	return rangeSteps;
}

int OE2_toggleFastTick(void)
{
	fastTickMode = !fastTickMode;
	toastText = fastTickMode ? "SPEED: FAST" : "SPEED: ORIG";
	toastFrames = 30;
	return fastTickMode;
}

int OE2_getTickMs(void)
{
	return fastTickMode ? 16 : 40;
}

/* Minimal 4x5 uppercase glyph table for the mode toggle toast. */
static uint32_t glyph_bits(char c)
{
	switch (c) {
	case 'A': return 0x69f99u;
	case 'C': return 0x78887u;
	case 'D': return 0xe999eu;
	case 'E': return 0xf8e8fu;
	case 'F': return 0xf8e88u;
	case 'G': return 0x78b97u;
	case 'H': return 0x99f99u;
	case 'I': return 0xe444eu;
	case 'L': return 0x8888fu;
	case 'M': return 0x9ff99u;
	case 'O': return 0x69996u;
	case 'P': return 0xe9e88u;
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
	ensure_lut565();
	for (int y = 0; y < scrH; y++) {
		const uint16_t *row = fb + y * pitchPixels;
		uint32_t *dst = outRgba + y * scrW;
		for (int x = 0; x < scrW; x++)
			dst[x] = lut565ToRgba[row[x]];
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

