#include "game/lens_flare.h"

#include "game/main_init.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"

#define FLARE_PARTS 8

/* Which levels have a sun (by level; 0x8006F03C). */
static const int32_t flare_levels[12] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1};

static POLY_FT4 flare_quads[FLARE_PARTS][2]; /* 0x8008C48C */
static POLY_FT4 sun_quad[2];                 /* 0x8007FD6C */

/* The flare's parts along the line from the sun through the centre of the screen: sprite, how far
 * back along the line (/128) and size (0x8006F028). */
static const struct {
    uint8_t sprite, offset, size;
} flare_parts[FLARE_PARTS] = {
    {2, 0x40, 0x30}, {4, 0x20, 0x20}, {1, 0x40, 0x20}, {2, 0x60, 0x40},
    {3, 0x80, 0x10}, {4, 0x60, 0x30}, {0, 0x80, 0x50}, {2, 0x40, 0x10},
};

/* Where the sun is, seen from the camera (0x800793B4). */
static const SVECTOR sun_direction = {2890, 2300, -500, 0};

void Init_LensFlare(void)
{
    static const char *const names[5] = {"FLARE_0", "FLARE_1", "FLARE_2", "FLARE_3", "FLARE_4"};
    if (flare_levels[current_level] == 0)
        return;
    for (int b = 0; b < 2; b++) {
        for (int k = 0; k < FLARE_PARTS; k++) {
            /* additive, from the templates at 0x80010480 */
            Sprite s = {.x = 0x80, .y = 0x32, .semi_trans = 1, .blend = 1};
            Setup_Sprite_Quad(&flare_quads[k][b], names[flare_parts[k].sprite], &s);
        }
        Sprite sun = {.x = 0x80, .y = 0x32, .semi_trans = 1, .blend = 1};
        Setup_Sprite_Quad(&sun_quad[b], "SUN", &sun);
    }
}

/* The sun, and a flare when it is near the middle of the screen, brighter the nearer. The sun's
 * projection uses whatever translation the GTE holds from the last drawing, as in the original. */
void DrawLensFlare(void)
{
    if (flare_levels[current_level] == 0)
        return;
    SetRotMatrix(&world_matrix);
    gte_ldv0(&sun_direction);
    gte_rtps();
    int32_t sz;
    gte_stsz(&sz);
    int16_t sx = (int16_t)(gte.sx[2] - 0xa0);
    int16_t sy = (int16_t)(gte.sy[2] - 0x78);
    int32_t dist = SquareRoot0(sx * sx + sy * sy);
    if (sz > 0 && dist < 500) {
        int32_t hx = sx / 8, hy = sy / 8;
        uint8_t bright = (uint8_t)(0x28 - (dist * 0x28) / 500);
        for (int k = 0; k < FLARE_PARTS; k++) {
            int16_t size = flare_parts[k].size;
            int32_t ox = -((hx * flare_parts[k].offset) / 128) / 2 + hx * (k - 4);
            int32_t oy = -((hy * flare_parts[k].offset) / 128) / 2 + hy * (k - 4);
            if (ox < -0x11f || ox > 0x11f || oy < -0xf7 || oy > 0xf7)
                continue;
            int16_t x = (int16_t)(ox + 0xa0 - (size >> 1));
            int16_t y = (int16_t)(oy + 0x78 - (size >> 1));
            POLY_FT4 *p = &flare_quads[k][buffer_num];
            int16_t x1 = (int16_t)(x + size), y1 = (int16_t)(y + size);
            setXY4(p, x, y, x1, y, x, y1, x1, y1);
            setRGB0(p, bright, bright, bright);
            addPrim(&cdb->ot[0], p);
        }
    }
    if (sx >= -0xf7 && sx <= 0x147 && sy >= -0xcf && sy <= 0x11f) {
        POLY_FT4 *p = &sun_quad[buffer_num];
        setXY4(p, sx + 0x78, sy + 0x50, sx + 200, sy + 0x50, sx + 0x78, sy + 0xa0, sx + 200, sy + 0xa0);
        setRGB0(p, 0x40, 0x40, 0x40);
        addPrim(&cdb->ot[otsize - 3], p);
    }
}
