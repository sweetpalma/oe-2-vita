#ifndef OE2_STANDALONE_BRIDGE_H
#define OE2_STANDALONE_BRIDGE_H

#include <stdint.h>

void OE2_setRemastered(int enabled);
int OE2_isRemastered(void);
int OE2_toggleRemastered(void);
void OE2_cycleLightRange(void);
int OE2_getLightRangeSteps(void);
int OE2_toggleFastTick(void);
int OE2_getTickMs(void);
void OE2_presentFrame(const uint16_t *fb, int pitchPixels, int scrW, int scrH, uint32_t *outRgba);

#endif

