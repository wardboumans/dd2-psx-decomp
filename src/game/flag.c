#include "game/flag.h"

#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"
#include "system/maths.h"

#define FLAG_COLUMNS 10
#define FLAG_ROWS 6
#define FLAG_POINTS ((FLAG_COLUMNS + 1) * (FLAG_ROWS + 1))

/* The chequered flag at the end of a race: a grid of shaded squares rippling in front of the camera,
 * with RACE OVER on it. */
static POLY_G4 flag_squares[FLAG_ROWS * FLAG_COLUMNS][2]; /* 0x800A1CF8 */
static SVECTOR flag_points[FLAG_POINTS];                   /* 0x80088AB8 */
static POLY_FT3 flag_mode;     /* 0x800A67D0: an empty triangle that sets the texture page (blending) */
static POLY_FT4 race_quad[2];  /* 0x80090CBC */
static POLY_FT4 over_quad[2];  /* 0x8009563C */
static int32_t flag_phase;     /* 0x80079398 */
static int32_t flag_arriving = 1; /* 0x8007939C */
static int16_t flag_distance = 4000; /* 0x800793A0: how far behind the screen the flag still is */

static void shade_square(POLY_G4 *p, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3)
{
    setRGB0(p, c0, c0, c0);
    setRGB1(p, c1, c1, c1);
    setRGB2(p, c2, c2, c2);
    setRGB3(p, c3, c3, c3);
}

void Init_Flag(void)
{
    flag_distance = 4000;
    flag_phase = 0;
    flag_arriving = 1;
    for (int r = 0; r < FLAG_ROWS; r++) {
        for (int c = 0; c < FLAG_COLUMNS; c++) {
            uint8_t shade = ((r * FLAG_COLUMNS + c + r) & 1) == 0 ? 0x40 : 0x32;
            int16_t x = (int16_t)(c * 0x20), y = (int16_t)(r * 0x28);
            for (int b = 0; b < 2; b++) {
                POLY_G4 *p = &flag_squares[r * FLAG_COLUMNS + c][b];
                setPolyG4(p);
                setSemiTrans(p, 1);
                shade_square(p, shade, shade, shade, shade);
                setXY4(p, x, y, x + 0x20, y, x, y + 0x28, x + 0x20, y + 0x28);
            }
        }
    }
    for (int r = 0; r <= FLAG_ROWS; r++) {
        for (int c = 0; c <= FLAG_COLUMNS; c++) {
            SVECTOR *v = &flag_points[r * (FLAG_COLUMNS + 1) + c];
            v->vx = (int16_t)(-0xa0 + c * 0x20);
            v->vy = (int16_t)(-0x78 + r * 0x28);
            v->vz = 0;
        }
    }
    setXY3(&flag_mode, 0, 0, 0, 0, 0, 0);
    flag_mode.tpage = GetTPage(0, 0, 0, 0);
    setPolyFT3(&flag_mode);
    /* the templates at 0x8006EFF8 and 0x8006F010 */
    Sprite race = {.x = 114, .y = 120, .semi_trans = 1, .raw = 1, .depth = 4};
    Sprite over = {.x = 170, .y = 120, .semi_trans = 1, .raw = 1, .depth = 4};
    Setup_Sprite_Quad(&race_quad[0], "RACE", &race);
    Setup_Sprite_Quad(&over_quad[0], "OVER", &over);
    Setup_Sprite_Quad(&race_quad[1], "RACE", &race);
    Setup_Sprite_Quad(&over_quad[1], "OVER", &over);
}

/* The flag comes in from behind the screen, a fifteenth of the way at a time. Returns 0 once it is
 * there. */
static int UpdateFlag(void) /* 0x800204B0 */
{
    for (int i = 0; i < FLAG_POINTS; i++)
        flag_points[i].vz = (int16_t)(flag_points[i].vz + flag_distance);
    if (flag_distance != 0)
        flag_distance = (int16_t)(flag_distance - flag_distance / 15);
    return flag_distance != 0;
}

/* A square's corner lit by the ripple: white squares darken where the cloth bends away. */
static uint8_t flag_shade(int base, int i)
{
    int16_t v = (int16_t)(base - flag_points[i].vz * 4);
    if (v > 0x80)
        v = 0x80;
    if (v < 0)
        v = 0;
    return (uint8_t)v;
}

static void draw_point(int i, int16_t *x, int16_t *y)
{
    int32_t sxy, p, flag;
    RotTransPers(&flag_points[i], &sxy, &p, &flag);
    *x = (int16_t)sxy;
    *y = (int16_t)(sxy >> 16);
}

void DrawFlagObject(void)
{
    static const MATRIX flag_matrix = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 320}}; /* 0x8006EFD8 */
    SetTransMatrix(&flag_matrix);
    SetRotMatrix(&flag_matrix);
    int row = 0;
    for (int i = 0; i < FLAG_POINTS; i++) {
        if (i % (FLAG_COLUMNS + 1) == 0)
            row++;
        int32_t s = rsin((i % (FLAG_COLUMNS + 1) + row) * 0x200 + flag_phase) * 0x100 / 4096;
        flag_points[i].vz = (int16_t)(s * 0xf >> 7);
    }
    for (int r = 0; r < FLAG_ROWS; r++) {
        for (int c = 0; c < FLAG_COLUMNS; c++) {
            int i = r * (FLAG_COLUMNS + 1) + c;
            int base = ((r * FLAG_COLUMNS + c + r) & 1) == 0 ? 0x80 : 0;
            shade_square(&flag_squares[r * FLAG_COLUMNS + c][buffer_num], flag_shade(base, i),
                         flag_shade(base, i + 1), flag_shade(base, i + FLAG_COLUMNS + 1),
                         flag_shade(base, i + FLAG_COLUMNS + 2));
        }
    }
    if (flag_arriving != 0)
        flag_arriving = UpdateFlag();
    for (int r = 0; r < FLAG_ROWS; r++) {
        for (int c = 0; c < FLAG_COLUMNS; c++) {
            int i = r * (FLAG_COLUMNS + 1) + c;
            POLY_G4 *p = &flag_squares[r * FLAG_COLUMNS + c][buffer_num];
            draw_point(i, &p->x0, &p->y0);
            draw_point(i + 1, &p->x1, &p->y1);
            draw_point(i + FLAG_COLUMNS + 1, &p->x2, &p->y2);
            draw_point(i + FLAG_COLUMNS + 2, &p->x3, &p->y3);
        }
    }
    addPrim(&cdb->ot[0], &race_quad[buffer_num]);
    addPrim(&cdb->ot[0], &over_quad[buffer_num]);
    for (int i = 0; i < FLAG_ROWS * FLAG_COLUMNS; i++)
        addPrim(&cdb->ot[0], &flag_squares[i][buffer_num]);
    addPrim(&cdb->ot[0], &flag_mode);
    flag_phase = (flag_phase + 0x60) & ANGLE_MASK;
}
