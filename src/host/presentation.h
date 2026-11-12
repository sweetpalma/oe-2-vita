#ifndef BREW_PRESENTATION_H
#define BREW_PRESENTATION_H
#include "game.h"
void BrewView_begin(Render *r);
void BrewView_line(Render *r,const Line *line);
void BrewView_sprite(Render *r,int sprite,int frame,int x,int y,int a,int blend,int scale,int flip);
void BrewView_effect(Render *r,int type,int value);
void BrewView_setQuality(int level);
const uint32_t *BrewView_lit(const uint32_t *original,int width,int height);
const uint32_t *BrewView_gpu(const uint32_t *original,int width,int height);
#endif
