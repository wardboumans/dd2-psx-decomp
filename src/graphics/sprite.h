#ifndef GRAPHICS_SPRITE_H
#define GRAPHICS_SPRITE_H

#include "psx/libgpu.h"

#include <stdint.h>

/* One sprite of LEVEL.SPR (on-disk layout): where its texels and CLUT are in VRAM. */
typedef struct {
    int16_t vram_x, vram_y; /* in VRAM words */
    int16_t width, height;  /* in texels */
    int16_t clut_x, clut_y;
    int16_t depth;          /* bits per texel: 4, 8 or 16 */
    char name[10];
} SpriteInfo;

_Static_assert(sizeof(SpriteInfo) == 0x18, "SpriteInfo must match LEVEL.SPR");

/* A sprite instance: where and how to draw it, filled in from its SpriteInfo. */
typedef struct {
    int16_t x, y;
    uint8_t r, g, b;
    uint8_t pad7;
    uint8_t semi_trans; /* +0x08 */
    uint8_t blend;      /* +0x09, semi-transparency mode */
    uint8_t raw;        /* +0x0A, no colour modulation */
    uint8_t depth;      /* +0x0B */
    uint16_t tpage;     /* +0x0C */
    uint16_t clut;      /* +0x0E */
    uint8_t w, h;       /* +0x10 */
    uint8_t u, v;       /* +0x12 */
    int16_t clut_x, clut_y; /* +0x14 */
} Sprite;

_Static_assert(sizeof(Sprite) == 0x18, "Sprite keeps the original offsets");

extern const SpriteInfo *sprite_table; /* 0x80079BBC */
extern int32_t sprite_count;           /* 0x80079C38 */

void Load_Sprite_Info(const uint8_t *blob);                              /* 0x8005B5C8 */
const SpriteInfo *Search_For_Sprite(const char *name);                   /* 0x8005B5E0 */
void Setup_Sprite_Quad(POLY_FT4 *quad, const char *name, Sprite *sprite); /* 0x8005B6BC */
void Modify_Sprite(POLY_FT4 *quad, const Sprite *sprite, int du, int dv, int w, int h); /* 0x8005BA80 */

#include "graphics/object.h"

int32_t Subdivide_Object(Object *obj); /* 0x8005D370 */

#endif
