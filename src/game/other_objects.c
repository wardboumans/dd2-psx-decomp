#include "game/other_objects.h"

#include "game/anim.h"
#include "game/car_init.h"
#include "game/main_init.h"
#include "game/scene.h"
#include "graphics/object.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"
#include "system/maths.h"

#include <string.h>

static void Draw_Dynamic_Objects(Object *obj, const int16_t angles[3], const int32_t pos[3]) /* 0x8001F8C8 */
{
    MATRIX m, light;
    Calc_Object_MatrixYXZ(&m, angles);
    MulMatrix0(&scene_light_matrix, &m, &light);
    SetLightMatrix(&light);
    SetColorMatrix(&scene_colour_matrix);
    Set_Object_View(&m, pos);
    VVDraw_Object(obj);
}

/* The next frame of a picture animation over its sprite. */
static void next_picture(Animation *a)
{
    if (a->sprite[++a->frame] == NULL)
        a->frame = 1;
    const SpriteInfo *f = a->sprite[a->frame];
    RECT r = {f->vram_x, f->vram_y, (int16_t)(f->width / 4), f->height};
    MoveImage(&r, a->sprite[0]->vram_x, a->sprite[0]->vram_y);
}

/* The next colour table of a CLUT animation over its sprite's. */
static void next_clut(Animation *a)
{
    if (a->sprite[++a->frame] == NULL)
        a->frame = 1;
    const SpriteInfo *f = a->sprite[a->frame];
    RECT r = {f->clut_x, f->clut_y, 16, 1};
    MoveImage(&r, a->sprite[0]->clut_x, a->sprite[0]->clut_y);
}

/* The rollercoaster's track (0x8006EC88): its points in two runs, each ended by a marker (-1 the end
 * of the first, after which a car waits 0x100 frames and takes the second; -2 the end of the second,
 * then 0x200 frames and back to the start). */
static const int32_t roller_track[26][4] = {
    {-64368, 670, 19967, 0}, {-62894, 1487, 18607, 0}, {-61416, 2174, 17318, 0}, {-59942, 2803, 16016, 0},
    {-58464, 3231, 14726, 0}, {-56983, 3553, 13449, 0}, {-55500, 3772, 12178, 0}, {-54017, 3883, 10906, 0},
    {-52534, 3880, 9633, 0}, {-51052, 3750, 8359, 0}, {-49570, 3471, 7082, 0}, {-48091, 2975, 5798, 0},
    {-46616, 2168, 4501, 0}, {-45146, 1100, 3186, 0}, {-1, 0, 0, 0}, {-30481, -6382, -10353, 0},
    {-29483, -4619, -14152, 0}, {-28985, -3665, -16052, 0}, {-28487, -2723, -17952, 0}, {-27988, -1853, -19851, 0},
    {-27488, -1100, -21751, 0}, {-26987, -631, -23650, 0}, {-26481, -757, -25549, 0}, {-25978, -1672, -27444, 0},
    {-25476, -2906, -29343, 0}, {-2, 0, 0, 0},
};

#define ROLLER_STEPS 6 /* frames from one track point to the next */

/* Where between its two points a car is, by its step. */
static void roller_place(RollerCar *r)
{
    r->t = r->step * 0x2aa;
    for (int k = 0; k < 3; k++)
        r->position[k] = (r->point[1][k] - r->point[0][k]) * r->t / 4096 + r->point[0][k];
    r->step++;
}

static void Update_Rollercoaster(void)
{
    for (int i = 0; i < ROLLER_CARS; i++) {
        RollerCar *r = &roller_cars[i];
        if (r->ready == 1) {
            memcpy(r->point[0], roller_track[r->point_index], sizeof(r->point[0]));
            int32_t marker = r->point[0][0];
            if (marker == -1 || marker == -2) {
                r->delay = marker == -1 ? 0x100 : 0x200;
                r->point_index = marker == -1 ? 15 : 0;
                r->restart_point = r->point_index;
                r->ready = 0;
                r->angle[1] = marker == -1 ? 0xf58 : 0xdda;
                r->step = 0;
                r->t = 0;
            } else {
                memcpy(r->point[1], roller_track[r->point_index + 1], sizeof(r->point[1]));
                if (r->step == ROLLER_STEPS) {
                    r->point_index++;
                    r->step = 0;
                    r->t = 0;
                    memcpy(r->point[0], roller_track[r->point_index], sizeof(r->point[0]));
                    memcpy(r->point[1], roller_track[r->point_index + 1], sizeof(r->point[1]));
                    int32_t dx = r->point[1][0] - r->point[0][0];
                    int32_t dz = r->point[1][2] - r->point[0][2];
                    r->angle[0] = (int16_t)Calc_Angle(r->point[1][1] - r->point[0][1], SquareRoot0(dx * dx + dz * dz));
                }
                roller_place(r);
                Draw_Dynamic_Objects(&roller_objects[i], r->angle, r->position);
            }
        }
        if (r->delay < 1) {
            r->ready = 1;
        } else {
            r->delay--;
            r->ready = 0;
        }
    }
}

/* Wild Bill's two figures rock from side to side. */
static void draw_wild_bill(int i)
{
    static int16_t rock[2][4] = {{512, 0, 0, 0}, {512, 0, 0, 0}};      /* 0x800791D8, 0x800791E0 */
    static const int32_t place[2][4] = {{55504, 3240, 9956, 0}, {55504, 3240, 9956, 0}}; /* 0x8006EC78, 0x8006EC68 */
    rock[i][2] = (int16_t)(rock[i][2] + 0x40);
    int16_t angles[4] = {rock[i][0], rock[i][1], (int16_t)(rsin(rock[i][2]) / 16), rock[i][3]};
    Draw_Dynamic_Objects(&wild_bill_objects[i], angles, place[i]);
}

void Draw_Other_Objects(void)
{
    switch (current_level) {
    case 1:
        draw_wild_bill(0);
        draw_wild_bill(1);
        if (frame_count % 2 == 0)
            next_picture(&video_anim);
        break;
    case 4:
        Update_Rollercoaster();
        break;
    case 5:
        if (frame_count % 3 == 0)
            next_picture(&flag_anims[0]);
        if (frame_count % 5 == 0)
            next_picture(&flag_anims[1]);
        break;
    case 6:
        if (frame_count % 8 == 0)
            next_clut(&dollar_cluts);
        if (frame_count % 12 == 0)
            next_clut(&safe_cluts);
        if (frame_count % 12 == 0)
            next_clut(&casino_cluts);
        if (frame_count % 32 == 0)
            next_picture(&driver_anim);
        if (frame_count % 24 == 0)
            next_clut(&j_cluts);
        if (frame_count % 32 == 0)
            next_clut(&twist_cluts[0]);
        if (frame_count % 24 == 0)
            next_clut(&twist_cluts[1]);
        if (frame_count % 2 == 0) {
            next_picture(&goose_anim);
            for (int i = 0; i < 3; i++)
                next_picture(&flash_anims[i]);
        }
        break;
    case 11:
        if (frame_count % 3 == 0)
            next_picture(&flame_anim);
        break;
    }
}
