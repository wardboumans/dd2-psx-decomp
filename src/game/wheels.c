#include "game/wheels.h"

#include "game/car.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"

Object wheel_objects[4];       /* 0x8008C75C */
Sprite wheel_sprites[3] = {   /* 0x80070264: still, turning, fast; raw texture */
    {0, 0, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 1},
};

#define FAST_WHEEL 300
#define TURNING_WHEEL 100
#define WHEEL_TEXELS 0x1f
#define BACK_FACE_OT 0x18      /* the far side of the overlay sorts behind the hub */

/* A wheel's outward side normal, for lighting its overlay. */
static const SVECTOR wheel_normals[4] = { /* 0x800702AC */
    {-4096, 0, 0, 0}, {4096, 0, 0, 0}, {-4096, 0, 0, 0}, {4096, 0, 0, 0},
};
static const CVECTOR wheel_colour = {0xc0, 0xc0, 0xc0, CODE_POLY_FT4}; /* 0x800794D4 */

/* Corner offsets round the wheel rim (y, z pairs) by spin angle. */
static const int16_t wheel_spin_table[512] = { /* 0x8006FE00 */
    97, 0, 97, 2, 97, 4, 97, 7, 97, 9, 96, 11, 96, 14, 96, 16,
    95, 18, 95, 21, 94, 23, 94, 25, 93, 28, 92, 30, 91, 32, 91, 35,
    90, 37, 89, 39, 88, 41, 87, 43, 86, 45, 84, 47, 83, 50, 82, 52,
    81, 54, 79, 56, 78, 57, 77, 59, 75, 61, 74, 63, 72, 65, 70, 67,
    69, 68, 67, 70, 65, 72, 63, 73, 62, 75, 60, 76, 58, 78, 56, 79,
    54, 80, 52, 82, 50, 83, 48, 84, 46, 85, 44, 87, 41, 88, 39, 89,
    37, 90, 35, 90, 33, 91, 30, 92, 28, 93, 26, 93, 24, 94, 21, 95,
    19, 95, 17, 96, 14, 96, 12, 96, 9, 97, 7, 97, 5, 97, 2, 97,
    0, 97, -2, 97, -5, 97, -7, 97, -10, 97, -12, 96, -14, 96, -17, 96,
    -19, 95, -21, 95, -24, 94, -26, 94, -28, 93, -31, 92, -33, 92, -35, 91,
    -37, 90, -40, 89, -42, 88, -44, 87, -46, 86, -48, 85, -50, 83, -52, 82,
    -54, 81, -56, 80, -58, 78, -60, 77, -62, 75, -64, 74, -66, 72, -67, 71,
    -69, 69, -71, 67, -72, 66, -74, 64, -76, 62, -77, 60, -78, 58, -80, 56,
    -81, 54, -83, 52, -84, 50, -85, 48, -86, 46, -87, 44, -88, 42, -89, 40,
    -90, 38, -91, 35, -92, 33, -93, 31, -94, 29, -94, 26, -95, 24, -96, 22,
    -96, 19, -97, 17, -97, 15, -97, 12, -98, 10, -98, 8, -98, 5, -98, 3,
    -98, 0, -98, -2, -98, -4, -98, -7, -98, -9, -97, -12, -97, -14, -97, -16,
    -96, -19, -96, -21, -95, -23, -95, -26, -94, -28, -93, -30, -93, -32, -92, -35,
    -91, -37, -90, -39, -89, -41, -88, -43, -87, -46, -86, -48, -85, -50, -83, -52,
    -82, -54, -81, -56, -79, -58, -78, -60, -77, -62, -75, -63, -74, -65, -72, -67,
    -70, -69, -69, -70, -67, -72, -65, -74, -63, -75, -61, -77, -60, -78, -58, -80,
    -56, -81, -54, -82, -52, -84, -50, -85, -48, -86, -45, -87, -43, -88, -41, -89,
    -39, -90, -37, -91, -34, -92, -32, -93, -30, -94, -28, -94, -25, -95, -23, -95,
    -21, -96, -18, -96, -16, -97, -14, -97, -11, -97, -9, -98, -7, -98, -4, -98,
    -2, -98, 1, -98, 3, -98, 5, -98, 8, -98, 10, -97, 13, -97, 15, -97,
    17, -96, 20, -96, 22, -95, 24, -95, 27, -94, 29, -94, 31, -93, 33, -92,
    36, -91, 38, -90, 40, -89, 42, -88, 44, -87, 46, -86, 48, -85, 50, -84,
    53, -82, 55, -81, 56, -80, 58, -78, 60, -77, 62, -75, 64, -74, 66, -72,
    67, -71, 69, -69, 71, -67, 72, -65, 74, -64, 75, -62, 77, -60, 78, -58,
    80, -56, 81, -54, 82, -52, 84, -50, 85, -48, 86, -46, 87, -44, 88, -41,
    89, -39, 90, -37, 91, -35, 92, -33, 92, -30, 93, -28, 94, -26, 94, -23,
    95, -21, 95, -19, 96, -16, 96, -14, 96, -12, 97, -9, 97, -7, 97, -5,};

static void set_wheel_uv(POLY_FT4 *p, const Sprite *s)
{
    p->u0 = s->u;
    p->v0 = s->v;
    p->u1 = (uint8_t)(s->u + WHEEL_TEXELS);
    p->v1 = s->v;
    p->u2 = s->u;
    p->v2 = (uint8_t)(s->v + WHEEL_TEXELS);
    p->u3 = (uint8_t)(s->u + WHEEL_TEXELS);
    p->v3 = (uint8_t)(s->v + WHEEL_TEXELS);
    p->tpage = s->tpage;
}

static int16_t spin_entry(int index)
{
    return wheel_spin_table[index & 0x1ff];
}

/* The hub cap drawn over a wheel: blurred more the faster the car goes, its corners turned by the
 * wheel's spin, lit from the side or black when seen from behind. The translation must already be
 * the wheel centre in view space. */
void ApplyWheelOverlay(int car, int wheel)
{
    CarState *cs = &car_states[car];
    int32_t speed = car_blocks[car].speed;
    if (speed < TURNING_WHEEL) {
        for (int w = 0; w < 4; w++)
            set_wheel_uv(&cs->wheel[w].packets[buffer_num], &wheel_sprites[0]);
    } else if (car_blocks[0].speed < FAST_WHEEL) {
        /* the original tests car 0's speed here */
        for (int w = 0; w < 4; w++)
            set_wheel_uv(&cs->wheel[w].packets[buffer_num], &wheel_sprites[1]);
    }
    if (speed >= FAST_WHEEL)
        for (int w = 0; w < 4; w++)
            set_wheel_uv(&cs->wheel[w].packets[buffer_num], &wheel_sprites[2]);

    CarWheelState *ws = &cs->wheel[wheel];
    int spin = ws->spin;
    CarWheelCorner *c = ws->corner;
    c[0].z = spin_entry(spin);
    c[0].y = spin_entry(spin + 1);
    if ((wheel & 1) == 0) {
        c[1].z = spin_entry(spin - 0x80);
        c[1].y = spin_entry(spin - 0x81);
        c[2].z = spin_entry(spin - 0x182);
        c[2].y = spin_entry(spin - 0x183);
    } else {
        c[1].z = spin_entry(spin + 0x80);
        c[1].y = spin_entry(spin + 0x81);
        c[2].z = spin_entry(spin - 0x82);
        c[2].y = spin_entry(spin - 0x83);
    }
    c[3].z = spin_entry(spin - 0x102);
    c[3].y = spin_entry(spin - 0x103);

    POLY_FT4 *p = &ws->packets[buffer_num];
    gte_ldv3((const SVECTOR *)&c[0], (const SVECTOR *)&c[1], (const SVECTOR *)&c[2]);
    gte_rtpt();
    gte_stsxy3_xy(p);
    int32_t flag;
    gte_stflg(&flag);
    if ((flag & GTE_CLIP_FLAGS) != 0)
        return;
    gte_nclip();
    int32_t opz;
    gte_stopz(&opz);
    gte_ldv0((const SVECTOR *)&c[3]);
    gte_rtps();
    p->x3 = gte.sx[2];
    p->y3 = gte.sy[2];
    gte_avsz4();
    int32_t otz;
    gte_stotz(&otz);
    if (opz < 0) {
        addPrim(&cdb->ot[otz + BACK_FACE_OT], p);
        p->r0 = 0;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        addPrim(&cdb->ot[otz], p);
        CVECTOR out;
        gte_ldrgb(&wheel_colour);
        gte_ldv0(&wheel_normals[wheel]);
        gte_ncds();
        gte_strgb(&out);
        p->r0 = out.r;
        p->g0 = out.g;
        p->b0 = out.b;
        p->code = out.cd;
    }
}

/* The player's wheels: each its own turned object, then its overlay. Both wheels of the pair use the
 * first one's rotation. */
void TransformWheels_(int car, int first, int second)
{
    CarState *cs = &car_states[car];
    MATRIX m;
    Calc_Object_MatrixYXZ(&m, cs->wheel[first].angle);
    MulMatrix2(&cs->matrix, &m);
    PushMatrix();
    const int wheels[2] = {first, second};
    for (int k = 0; k < 2; k++) {
        int w = wheels[k];
        if (car_blocks[car].wheel_lost[w] == 0) {
            SVECTOR centre = {cs->wheel[w].centre[0], cs->wheel[w].centre[1], cs->wheel[w].centre[2], 0};
            VECTOR pos;
            gte_ldv0(&centre);
            gte_rtv0();
            gte_stlvnl(&pos);
            pos.vx += cs->position[0];
            pos.vy += cs->position[1];
            pos.vz += cs->position[2];
            int32_t p[3] = {pos.vx, pos.vy, pos.vz};
            Set_Object_View(&m, p);
            Update_Object(&wheel_objects[w]);
            gte_SetTransMatrix(&object_matrix);
            ApplyWheelOverlay(car, w);
        }
        if (k == 0)
            PopMatrix();
    }
}

/* Other cars' wheels are part of their model; only the overlays are drawn, at the wheel centres. */
void TransformEnemyWheels(int car)
{
    CarState *cs = &car_states[car];
    VECTOR at[4];
    for (int w = 0; w < 4; w++) {
        SVECTOR centre = {cs->wheel[w].centre[0], cs->wheel[w].centre[1], cs->wheel[w].centre[2], 0};
        gte_ldv0(&centre);
        gte_rt();
        gte_stlvnl(&at[w]);
    }
    for (int w = 0; w < 4; w++) {
        if (car_blocks[car].wheel_lost[w] != 0)
            continue;
        gte.tr[0] = at[w].vx;
        gte.tr[1] = at[w].vy;
        gte.tr[2] = at[w].vz;
        ApplyWheelOverlay(car, w);
    }
}
