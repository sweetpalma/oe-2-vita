/* PS Vita platform implementation (SDL2 GXM renderer + SceCtrl + SceAudio + SceIo). */
#include "platform.h"
#include "standalone_bridge.h"
#include "game.h"

#include <SDL.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

int _newlib_heap_size_user = 128 * 1024 * 1024;
#endif

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t *rgbaFrame;
static int scrW, scrH;
static int integerScale = 0;
static char dataDir[1024] = "app0:gamedata";
static char saveDir[1024] = "ux0:data/oe2";
static int virtClock;
static uint32_t virtNow;

int Plat_init(int screenW, int screenH, const char *title)
{
	scrW = screenW;
	scrH = screenH;

#ifdef __vita__
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir("ux0:data/oe2", 0777);
#endif

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return -1;
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#ifdef __vita__
	win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                       960, 544, SDL_WINDOW_SHOWN);
#else
	int scale = 3;
	SDL_DisplayMode dm;
	if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
		scale = (dm.h * 8 / 10) / screenH;
		if (scale < 1)
			scale = 1;
	}
	win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                       screenW * scale, screenH * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
	if (!win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return -1;
	}
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (!ren)
		ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	if (!ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		return -1;
	}
	SDL_RenderSetLogicalSize(ren, screenW, screenH);
	SDL_RenderSetIntegerScale(ren, SDL_FALSE);
	tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, screenW, screenH);
	rgbaFrame = calloc((size_t)screenW * (size_t)screenH, sizeof(uint32_t));
	if (!tex || !rgbaFrame) {
		fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
		return -1;
	}
	return 0;
}

void Plat_shutdown(void)
{
	Audio_shutdown();
	free(rgbaFrame);
	rgbaFrame = NULL;
	if (tex)
		SDL_DestroyTexture(tex);
	if (ren)
		SDL_DestroyRenderer(ren);
	if (win)
		SDL_DestroyWindow(win);
	tex = NULL;
	ren = NULL;
	win = NULL;
	SDL_Quit();
}

void Plat_setVirtualClock(int enabled, uint32_t nowMs) { virtClock = enabled; virtNow = nowMs; }
uint32_t Plat_uptimeMs(void) { return virtClock ? virtNow : SDL_GetTicks(); }
void Plat_delay(uint32_t ms) { SDL_Delay(ms); }
void Plat_vibrate(int ms) { (void)ms; }
void Plat_setWindowTitle(const char *title) { if (win) SDL_SetWindowTitle(win, title); }

void Plat_present(const uint16_t *fb, int pitchPixels)
{
	if (!tex || !rgbaFrame || virtClock)
		return;
	OE2_presentFrame(fb, pitchPixels, scrW, scrH, rgbaFrame);
	SDL_UpdateTexture(tex, NULL, rgbaFrame, scrW * (int)sizeof(uint32_t));
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	SDL_RenderCopy(ren, tex, NULL, NULL);
	SDL_RenderPresent(ren);
}

#ifdef __vita__
static uint32_t prevButtons;

enum {
	VBTN_UP       = 1u << 0,
	VBTN_DOWN     = 1u << 1,
	VBTN_LEFT     = 1u << 2,
	VBTN_RIGHT    = 1u << 3,
	VBTN_SLEFT    = 1u << 4,
	VBTN_SRIGHT   = 1u << 5,
	VBTN_CROSS    = 1u << 6,
	VBTN_CIRCLE   = 1u << 7,
	VBTN_TRIANGLE = 1u << 8,
	VBTN_SQUARE   = 1u << 9,
	VBTN_START    = 1u << 10,
	VBTN_SELECT   = 1u << 11,
};

static int adjust_merchant_offer(int delta)
{
	if (!g_app || !g_app->canvas || !g_app->menuSystem || !g_app->menu)
		return 0;
	if (g_app->canvas->state != 2 || g_app->menuSystem->menu != 0x135)
		return 0;
	MenuSystem *ms = g_app->menuSystem;
	MenuItem *item = &ms->items[ms->selectedIndex];
	int val = item->action + delta;
	if (val < 0)
		val = 0;
	if (val > 9999)
		val = 9999;
	item->action = val;
	g_app->menu->offerActive = 0;
	String_setLength(&item->text2, 0);
	String_appendInt(&item->text2, val);
	while (String_length(&item->text2) < ms->maxDigits) {
		String sp;
		String_ctorCStr(&sp, " ");
		String_insert(&item->text2, 0, &sp);
		String_dtor(&sp, 0);
	}
	return 1;
}

static int map_vita_button(uint32_t bit, uint32_t rawButtons)
{
	int st = (g_app && g_app->canvas) ? g_app->canvas->state : 0;
	int holdL = (rawButtons & SCE_CTRL_LTRIGGER) != 0;
	int holdR = (rawButtons & SCE_CTRL_RTRIGGER) != 0;

	switch (bit) {
	case VBTN_UP:    return AVK_UP;
	case VBTN_DOWN:  return AVK_DOWN;
	case VBTN_LEFT:  return AVK_LEFT;
	case VBTN_RIGHT: return AVK_RIGHT;
	case VBTN_SLEFT: return AVK_1;
	case VBTN_SRIGHT:return AVK_3;
	case VBTN_CROSS: return AVK_SELECT;
	case VBTN_START: return AVK_SOFT1;
	case VBTN_CIRCLE:
		if (st == 3) /* ST_PLAYING: pass turn (or previous weapon with L) */
			return holdL ? AVK_STAR : AVK_9;
		return AVK_CLR;
	case VBTN_TRIANGLE:
		if (st == 3) {
			if (holdL)
				return AVK_STAR; /* previous weapon */
			if (holdR)
				return AVK_0;    /* quick potion/item menu */
			return AVK_7;            /* next weapon */
		}
		return AVK_SOFT2;
	case VBTN_SQUARE:
		if (st == 3 || st == 5)
			return holdL ? AVK_5 : AVK_POUND; /* automap, or quick weapon list with L */
		return AVK_CLR;
	default:
		return 0;
	}
}
#endif

int Plat_pollEvents(PlatKeyEvent *ev, int max, int *count)
{
	SDL_Event e;
	int n = 0, keepGoing = 1;
	while (n < max && SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			keepGoing = 0;
		}
#ifndef __vita__
		else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
			SDL_Keycode k = e.key.keysym.sym;
			if (e.type == SDL_KEYDOWN && !e.key.repeat) {
				if (k == SDLK_F2) {
					OE2_toggleRemastered();
					break;
				}
				if (k == SDLK_F3) {
					OE2_cycleLightRange();
					break;
				}
				if (k == SDLK_F11) {
					Uint32 fl = SDL_GetWindowFlags(win);
					SDL_SetWindowFullscreen(win, (fl & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
					break;
				}
			}
			int avk = 0;
			switch (k) {
			case SDLK_UP: case SDLK_w: avk = AVK_UP; break;
			case SDLK_DOWN: case SDLK_s: avk = AVK_DOWN; break;
			case SDLK_LEFT: avk = AVK_LEFT; break;
			case SDLK_RIGHT: avk = AVK_RIGHT; break;
			case SDLK_a: avk = AVK_1; break;
			case SDLK_d: avk = AVK_3; break;
			case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: avk = AVK_SELECT; break;
			case SDLK_ESCAPE: case SDLK_q: avk = AVK_SOFT1; break;
			case SDLK_e: avk = AVK_SOFT2; break;
			case SDLK_BACKSPACE: avk = AVK_CLR; break;
			case SDLK_TAB: case SDLK_HASH: avk = AVK_POUND; break;
			case SDLK_z: avk = AVK_7; break;
			case SDLK_x: case SDLK_ASTERISK: case SDLK_KP_MULTIPLY: avk = AVK_STAR; break;
			case SDLK_c: avk = AVK_9; break;
			default:
				if (k >= SDLK_0 && k <= SDLK_9)
					avk = AVK_0 + (int)(k - SDLK_0);
				break;
			}
			if (avk) {
				ev[n].avk = avk;
				ev[n].down = e.type == SDL_KEYDOWN;
				ev[n].repeat = e.key.repeat;
				n++;
			}
		}
#endif
	}

#ifdef __vita__
	SceCtrlData pad;
	memset(&pad, 0, sizeof(pad));
	if (sceCtrlPeekBufferPositive(0, &pad, 1) >= 0) {
		uint32_t cur = 0;
		const int deadzone = 50;
		int lx = (int)pad.lx - 128;
		int ly = (int)pad.ly - 128;
		int rx = (int)pad.rx - 128;

		if ((pad.buttons & SCE_CTRL_UP) || ly < -deadzone)
			cur |= VBTN_UP;
		if ((pad.buttons & SCE_CTRL_DOWN) || ly > deadzone)
			cur |= VBTN_DOWN;
		if ((pad.buttons & SCE_CTRL_LEFT) || rx < -deadzone)
			cur |= VBTN_LEFT;
		if ((pad.buttons & SCE_CTRL_RIGHT) || rx > deadzone)
			cur |= VBTN_RIGHT;
		if ((pad.buttons & SCE_CTRL_LTRIGGER) || lx < -deadzone)
			cur |= VBTN_SLEFT;
		if ((pad.buttons & SCE_CTRL_RTRIGGER) || lx > deadzone)
			cur |= VBTN_SRIGHT;
		if (pad.buttons & SCE_CTRL_CROSS)
			cur |= VBTN_CROSS;
		if (pad.buttons & SCE_CTRL_CIRCLE)
			cur |= VBTN_CIRCLE;
		if (pad.buttons & SCE_CTRL_TRIANGLE)
			cur |= VBTN_TRIANGLE;
		if (pad.buttons & SCE_CTRL_SQUARE)
			cur |= VBTN_SQUARE;
		if (pad.buttons & SCE_CTRL_START)
			cur |= VBTN_START;
		if (pad.buttons & SCE_CTRL_SELECT)
			cur |= VBTN_SELECT;

		uint32_t changed = cur ^ prevButtons;
		if ((changed & VBTN_SELECT) && (cur & VBTN_SELECT)) {
			if (pad.buttons & SCE_CTRL_LTRIGGER) {
				OE2_cycleLightRange();
			} else if (pad.buttons & SCE_CTRL_RTRIGGER) {
				integerScale = !integerScale;
				SDL_RenderSetIntegerScale(ren, integerScale ? SDL_TRUE : SDL_FALSE);
			} else {
				OE2_toggleRemastered();
			}
		}

		static const uint32_t tracked[] = {
			VBTN_UP, VBTN_DOWN, VBTN_LEFT, VBTN_RIGHT,
			VBTN_SLEFT, VBTN_SRIGHT, VBTN_CROSS, VBTN_CIRCLE,
			VBTN_TRIANGLE, VBTN_SQUARE, VBTN_START
		};
		for (size_t i = 0; i < sizeof(tracked) / sizeof(tracked[0]) && n < max; i++) {
			uint32_t b = tracked[i];
			if (!(changed & b))
				continue;
			int down = (cur & b) != 0;
			if (down) {
				if (b == VBTN_UP && adjust_merchant_offer(1))
					continue;
				if (b == VBTN_DOWN && adjust_merchant_offer(-1))
					continue;
				if (b == VBTN_LEFT && adjust_merchant_offer(-10))
					continue;
				if (b == VBTN_RIGHT && adjust_merchant_offer(10))
					continue;
			}
			int avk = map_vita_button(b, pad.buttons);
			if (avk) {
				ev[n].avk = avk;
				ev[n].down = down;
				ev[n].repeat = 0;
				n++;
			}
		}
		prevButtons = cur;
	}
#endif

	*count = n;
	return keepGoing;
}

int Plat_isMinimized(void)
{
	return win && (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) != 0;
}

/* ---- files ---- */

void Plat_setDataDir(const char *dir)
{
	snprintf(dataDir, sizeof(dataDir), "%s", dir);
#ifdef __vita__
	if (strncmp(dir, "app0:", 5) == 0)
		snprintf(saveDir, sizeof(saveDir), "ux0:data/oe2");
	else
		snprintf(saveDir, sizeof(saveDir), "%s", dir);
#else
	snprintf(saveDir, sizeof(saveDir), "%s", dir);
#endif
}

const char *Plat_dataDir(void) { return dataDir; }

const char *Plat_dataPath(const char *name)
{
	static char path[2048];
	snprintf(path, sizeof(path), "%s/%s", saveDir, name);
	return path;
}

int Plat_fileRead(const char *name, uint8_t **data, size_t *len)
{
	FILE *f = fopen(Plat_dataPath(name), "rb");
	long n;
	*data = NULL;
	*len = 0;
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	*data = malloc((size_t)n + 1);
	if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) {
		free(*data);
		*data = NULL;
		fclose(f);
		return -1;
	}
	fclose(f);
	*len = (size_t)n;
	return 0;
}

int Plat_fileWrite(const char *name, const void *data, size_t len)
{
	char tmp[2100];
	FILE *f;
	snprintf(tmp, sizeof(tmp), "%s.tmp", Plat_dataPath(name));
	f = fopen(tmp, "wb");
	if (!f)
		return -1;
	if (fwrite(data, 1, len, f) != len) {
		fclose(f);
		remove(tmp);
		return -1;
	}
	if (fclose(f) != 0) {
		remove(tmp);
		return -1;
	}
	remove(Plat_dataPath(name));
	return rename(tmp, Plat_dataPath(name)) == 0 ? 0 : -1;
}

int Plat_fileRemove(const char *name) { return remove(Plat_dataPath(name)) == 0 ? 0 : -1; }

int Plat_fileExists(const char *name)
{
	struct stat st;
	return stat(Plat_dataPath(name), &st) == 0;
}

/* ---- audio: Kaiser-windowed sinc resampler ---- */

#define RS_HALF   16
#define RS_TAPS   (2 * RS_HALF)
#define RS_PHASES 256
#define RS_CUTOFF 0.45
#define RS_BETA   8.0

static float rsKernel[RS_PHASES + 1][RS_TAPS];

static double bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	for (int k = 1; k < 32; k++) {
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
	}
	return sum;
}

static void rs_build_kernel(void)
{
	for (int ph = 0; ph <= RS_PHASES; ph++) {
		double frac = (double)ph / RS_PHASES, sum = 0.0, row[RS_TAPS];
		for (int t = 0; t < RS_TAPS; t++) {
			double x = (double)(t - (RS_HALF - 1)) - frac;
			double w = x / RS_HALF, sinc;
			double arg = 2.0 * M_PI * RS_CUTOFF * x;
			sinc = fabs(x) < 1e-9 ? 1.0 : sin(arg) / arg;
			row[t] = fabs(w) >= 1.0 ? 0.0 : sinc * bessel_i0(RS_BETA * sqrt(1.0 - w * w)) / bessel_i0(RS_BETA);
			sum += row[t];
		}
		for (int t = 0; t < RS_TAPS; t++)
			rsKernel[ph][t] = (float)(row[t] / sum);
	}
}

typedef struct {
	const int16_t *pcm;
	int samples;
	uint32_t step;
	uint64_t pos;
	int loops;
	int volume;
	int playing;
} Chan;

static SDL_AudioDeviceID dev;
static SDL_AudioSpec have;
static Chan chans[AUDIO_CHANNELS];

static float rs_sample(const Chan *ch, uint64_t pos)
{
	int ip = (int)(pos >> 16), first = ip - (RS_HALF - 1);
	uint32_t fr = (uint32_t)(pos & 0xffff);
	int ph = (int)(fr >> 8);
	float mix = (float)(fr & 0xff) / 256.0f, acc = 0.0f;
	const float *k0 = rsKernel[ph], *k1 = rsKernel[ph + 1];
	int t0 = first < 0 ? -first : 0;
	int t1 = first + RS_TAPS > ch->samples ? ch->samples - first : RS_TAPS;
	for (int t = t0; t < t1; t++)
		acc += (float)ch->pcm[first + t] * (k0[t] + (k1[t] - k0[t]) * mix);
	return acc;
}

static void mix_cb(void *ud, Uint8 *stream, int len)
{
	int16_t *out = (int16_t *)stream;
	int frames = len / (int)(sizeof(int16_t) * have.channels);
	(void)ud;
	for (int i = 0; i < frames; i++) {
		float acc = 0.0f;
		int s;
		for (int c = 0; c < AUDIO_CHANNELS; c++) {
			Chan *ch = &chans[c];
			if (!ch->playing)
				continue;
			if ((uint32_t)(ch->pos >> 16) >= (uint32_t)ch->samples) {
				if (ch->loops != 0) {
					if (ch->loops > 0)
						ch->loops--;
					ch->pos = 0;
				} else if ((uint32_t)(ch->pos >> 16) >= (uint32_t)ch->samples + RS_HALF) {
					ch->playing = 0;
					continue;
				}
			}
			acc += rs_sample(ch, ch->pos) * (float)ch->volume / 100.0f;
			ch->pos += ch->step;
		}
		s = (int)lrintf(acc);
		if (s > 32767)
			s = 32767;
		if (s < -32768)
			s = -32768;
		for (int k = 0; k < have.channels; k++)
			*out++ = (int16_t)s;
	}
}

int Audio_init(void)
{
	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = mix_cb;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if (!dev || have.format != AUDIO_S16SYS) {
		fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
		if (dev)
			SDL_CloseAudioDevice(dev);
		dev = 0;
		return -1;
	}
	rs_build_kernel();
	for (int c = 0; c < AUDIO_CHANNELS; c++)
		chans[c].volume = 100;
	SDL_PauseAudioDevice(dev, 0);
	return 0;
}

void Audio_shutdown(void)
{
	if (dev)
		SDL_CloseAudioDevice(dev);
	dev = 0;
}

void Audio_play(int channel, const int16_t *pcm, int samples, int rate, int loops)
{
	Chan *ch;
	if (!dev || channel < 0 || channel >= AUDIO_CHANNELS || !pcm || samples <= 0)
		return;
	SDL_LockAudioDevice(dev);
	ch = &chans[channel];
	ch->pcm = pcm;
	ch->samples = samples;
	ch->step = (uint32_t)(((uint64_t)rate << 16) / (uint64_t)have.freq);
	ch->pos = 0;
	ch->loops = loops;
	ch->playing = 1;
	SDL_UnlockAudioDevice(dev);
}

void Audio_stop(int channel)
{
	if (!dev || channel < 0 || channel >= AUDIO_CHANNELS)
		return;
	SDL_LockAudioDevice(dev);
	chans[channel].playing = 0;
	SDL_UnlockAudioDevice(dev);
}

int Audio_isPlaying(int channel)
{
	int p;
	if (!dev || channel < 0 || channel >= AUDIO_CHANNELS)
		return 0;
	SDL_LockAudioDevice(dev);
	p = chans[channel].playing && (uint32_t)(chans[channel].pos >> 16) < (uint32_t)chans[channel].samples;
	SDL_UnlockAudioDevice(dev);
	return p;
}

void Audio_setVolume(int channel, int volume0to100)
{
	if (channel < 0 || channel >= AUDIO_CHANNELS)
		return;
	if (volume0to100 < 0)
		volume0to100 = 0;
	if (volume0to100 > 100)
		volume0to100 = 100;
	if (dev)
		SDL_LockAudioDevice(dev);
	chans[channel].volume = volume0to100;
	if (dev)
		SDL_UnlockAudioDevice(dev);
}

void Audio_pauseAll(int pause)
{
	if (dev)
		SDL_PauseAudioDevice(dev, pause);
}
