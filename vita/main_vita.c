/* Orcs & Elves II — PS Vita host loop.
 * Preserves the original 40 ms BREW simulation timer while providing
 * responsive gamepad auto-repeat and clean Vita process shutdown. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "standalone_bridge.h"

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif

#define TICK_MS 40
#define KEY_REPEAT_START 320
#define KEY_REPEAT_RATE  80
#define KB_AUTOREPEAT    0x40

static const char *resolve_data_dir(void)
{
	static const char *const candidates[] = {
		"app0:gamedata",
		"ux0:data/oe2",
		"gamedata",
	};
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		char path[256];
		snprintf(path, sizeof(path), "%s/orcsandelves2.bar", candidates[i]);
		FILE *f = fopen(path, "rb");
		if (f) {
			fclose(f);
			return candidates[i];
		}
	}
	return "app0:gamedata";
}

int main(int argc, char **argv)
{
	const char *data = resolve_data_dir();
	int w = 352, h = 200;
	Applet *app;
	uint32_t nextTick;

	(void)argc;
	(void)argv;

	if (Brew_init(data, w, h) != 0) {
#ifdef __vita__
		sceKernelExitProcess(1);
#endif
		return 1;
	}
	if (Plat_init(w, h, "Orcs & Elves II") != 0) {
		Brew_shutdown();
#ifdef __vita__
		sceKernelExitProcess(1);
#endif
		return 1;
	}
	Audio_init();

	app = App_create(w, h);
	if (!app) {
		Plat_shutdown();
		Brew_shutdown();
#ifdef __vita__
		sceKernelExitProcess(1);
#endif
		return 1;
	}
	Applet_HandleEvent(app, EVT_APP_START, 0, 0);

	nextTick = Plat_uptimeMs();
	int heldKey = 0, suspended = 0;
	uint32_t nextRepeat = 0;
	for (;;) {
		PlatKeyEvent ev[32];
		int n;
		if (!Plat_pollEvents(ev, 32, &n))
			break;
		if (Plat_isMinimized() != suspended) {
			suspended = !suspended;
			heldKey = 0;
			Applet_HandleEvent(app, suspended ? EVT_APP_SUSPEND : EVT_APP_RESUME, 0, 0);
		}
		for (int i = 0; i < n && !suspended; i++) {
			if (ev[i].repeat)
				continue;
			if (ev[i].down) {
				Applet_HandleEvent(app, EVT_KEY, ev[i].avk, 0);
				heldKey = ev[i].avk;
				nextRepeat = Plat_uptimeMs() + KEY_REPEAT_START;
			} else if (ev[i].avk == heldKey) {
				heldKey = 0;
			}
		}
		if (heldKey && (int32_t)(Plat_uptimeMs() - nextRepeat) >= 0) {
			Applet_HandleEvent(app, EVT_KEY, heldKey, KB_AUTOREPEAT);
			nextRepeat += KEY_REPEAT_RATE;
			if ((int32_t)(Plat_uptimeMs() - nextRepeat) >= 0)
				nextRepeat = Plat_uptimeMs() + KEY_REPEAT_RATE;
		}
		if (Brew_quitRequested())
			break;
		uint32_t now = Plat_uptimeMs();
		if ((int32_t)(now - nextTick) >= 0) {
			uint32_t step = (uint32_t)OE2_getTickMs();
			nextTick += step;
			if ((int32_t)(now - nextTick) >= 0)
				nextTick = now + step;
			Applet_Tick(app);
		} else {
			Plat_delay(1);
		}
	}

	Applet_HandleEvent(app, EVT_APP_STOP, 0, 0);
	App_destroy(app);
	Plat_shutdown();
	Brew_shutdown();
#ifdef __vita__
	sceKernelExitProcess(0);
#endif
	return 0;
}

