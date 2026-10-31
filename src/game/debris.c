#include "game/debris.h"

#include "game/car.h"
#include "game/car_cluts.h"
#include "game/car_motion.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/scene.h"
#include "game/track.h"
#include "graphics/sprite.h"
#include "psx/libc.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"

#include <stdio.h>
#include <string.h>

#define DEBRIS_COUNT 128
#define DEBRIS_FRAMES 128 /* spin frames of each shape */

/* A spinning triangle of debris (0x7C bytes in the original). */
typedef struct {
    POLY_FT3 packet[2];   /* +0x00 */
    SVECTOR vertices[3];  /* +0x40: the shape at its current spin frame */
    int32_t position[4];  /* +0x58 */
    int32_t velocity[4];  /* +0x68: 20.12 */
    uint8_t life;         /* +0x78: frames left */
    uint8_t shape;        /* +0x79 */
    uint8_t frame;        /* +0x7A: spin frame */
    uint8_t spin;         /* +0x7B: frames to step each frame */
} Debris;

static Debris debris[DEBRIS_COUNT];  /* 0x80080454 */
static uint8_t debris_next;          /* 0x80079C24: the next record to throw (records before it fly) */
static uint16_t debris_cluts[20];    /* 0x80090BFC: each car's paint, for its debris */
static VECTOR debris_frames[2][DEBRIS_FRAMES][3]; /* 0x8009EC94: each shape turned to each spin frame */

/* The two debris triangles (the vertices of the templates at 0x80095524). */
static const SVECTOR debris_shapes[2][3] = {
    {{0, -0x1e, -0x32, 0}, {0x32, 0x3c, -0x50, 0}, {-0x32, 0x32, 0x3c, 0}},
    {{-0x14, 0x1e, -0x32, 0}, {0x32, -0x3c, 0x50, 0}, {-0x32, 10, -0x3c, 0}},
};

static const CVECTOR debris_colour = {0x80, 0x80, 0x80, CODE_POLY_FT3}; /* 0x80079150 */

#define DEBRIS_CAR_NO_PAINT 18 /* as in Init_Car_Cluts: this car's debris takes car 1's paint */

/* Each car's paint for its debris; the debris packets on the two DEBR88 sprites; the spin frames. */
void Init_Debris(void)
{
    static MATRIX spin_matrix; /* 0x8006DC54 */
    static SVECTOR spin_angles[2]; /* 0x800A721C */
    Sprite sprites[2] = {{0}}, s = {0};
    char name[24];
    debris_next = 0;
    Setup_Sprite_Quad(NULL, "DEBR88A", &sprites[0]);
    Setup_Sprite_Quad(NULL, "DEBR88B", &sprites[1]);
    for (int i = 0; i < num_cars; i++) {
        if (i == DEBRIS_CAR_NO_PAINT) {
            Setup_Sprite_Quad(NULL, "CLUT01B", &s);
            debris_cluts[i] = s.clut;
            continue;
        }
        if (i == 0) {
            if (race_car == 0)
                sprintf(name, "CLUT%02dB", car_textures[0]);
            if (race_car == 1)
                sprintf(name, "CLT%02dB2", car_textures[0]);
            if (race_car == 2)
                sprintf(name, "CLT%02dB3", car_textures[0]);
        } else {
            sprintf(name, "CLUT%02dB", car_textures[i]);
        }
        Setup_Sprite_Quad(NULL, name, &s);
        debris_cluts[i] = s.clut;
    }
    for (int i = 0; i < 2; i++)
        spin_angles[i].vx = spin_angles[i].vy = spin_angles[i].vz = 0;
    for (int i = 0; i < DEBRIS_COUNT; i++) {
        Debris *d = &debris[i];
        const Sprite *sp = &sprites[i & 1];
        for (int b = 0; b < 2; b++) {
            POLY_FT3 *p = &d->packet[b];
            setPolyFT3(p);
            p->r0 = p->g0 = p->b0 = 0x1e;
            setSemiTrans(p, 0);
            setShadeTex(p, 1);
            p->u0 = sp->u;
            p->v0 = sp->v;
            p->u1 = (uint8_t)(sp->u + sp->w - 1);
            p->v1 = sp->v;
            p->u2 = sp->u;
            p->v2 = (uint8_t)(sp->v + sp->h - 1);
            p->tpage = sp->tpage;
            p->clut = sp->clut;
        }
        d->life = 0;
        d->shape = (uint8_t)(rand() & 1);
        d->frame = (uint8_t)(rand() & 0x7f);
        d->spin = (uint8_t)((rand() & 3) + 1);
    }
    for (int f = 0; f < DEBRIS_FRAMES; f++) {
        spin_angles[0].vx += 0x20;
        spin_angles[0].vy += 0x20;
        spin_angles[0].vz -= 0x20;
        spin_angles[1].vx -= 0x20;
        spin_angles[1].vy += 0x20;
        spin_angles[1].vz -= 0x20;
        for (int shape = 0; shape < 2; shape++) {
            Calc_Object_MatrixYXZ(&spin_matrix, &spin_angles[shape].vx);
            SetRotMatrix(&spin_matrix);
            SetTransMatrix(&spin_matrix);
            int32_t flag;
            for (int k = 0; k < 3; k++)
                RotTrans(&debris_shapes[shape][k], &debris_frames[shape][f][k], &flag);
        }
    }
}

/* Throws `count` (0..7) bits of a car's paint from `where`, scattered more the more there are. */
void Setup_Debris(const int32_t where[3], const int32_t velocity[3], int count, int car)
{
    count &= 7;
    int end = debris_next + count;
    for (int i = debris_next; i < end; i++) {
        Debris *d = &debris[i & (DEBRIS_COUNT - 1)];
        d->life = 0x50;
        d->position[0] = where[0];
        d->position[1] = where[1];
        d->position[2] = where[2];
        d->packet[0].clut = debris_cluts[car];
        d->packet[1].clut = debris_cluts[car];
        d->velocity[0] = ((rand() & 0x7fff) - 0x4000) * count * 4 + velocity[0];
        d->velocity[1] = (rand() & 0x7fff) * count * 4;
        d->velocity[2] = ((rand() & 0x7fff) - 0x4000) * count * 4 + velocity[2];
    }
    debris_next = (uint8_t)(end & (DEBRIS_COUNT - 1));
}

/* Moves and draws the flying debris: from the newest back through every record still alive. */
void Update_Debris(void)
{
    debris[debris_next].life = 0;
    int i = debris_next == 0 ? DEBRIS_COUNT - 1 : debris_next - 1;
    while (debris[i].life != 0) {
        Debris *d = &debris[i];
        d->frame = (uint8_t)((d->frame + d->spin) & 0x7f);
        if (d->frame != 0) {
            for (int k = 0; k < 3; k++) {
                const VECTOR *v = &debris_frames[d->shape][d->frame][k];
                d->vertices[k].vx = (int16_t)v->vx;
                d->vertices[k].vy = (int16_t)v->vy;
                d->vertices[k].vz = (int16_t)v->vz;
            }
        }
        d->velocity[1] -= 0x6000;
        d->position[0] += d->velocity[0] >> 12;
        d->position[2] += d->velocity[2] >> 12;
        d->position[1] += d->velocity[1] >> 12;
        SVECTOR rel = {(int16_t)(d->position[0] - world_matrix.t[0]), (int16_t)(d->position[1] - world_matrix.t[1]),
                       (int16_t)(d->position[2] - world_matrix.t[2]), 0};
        SetRotMatrix(&world_matrix);
        gte_ldv0(&rel);
        gte_rtv0();
        VECTOR t;
        gte_stlvl(&t);
        gte_ldtr(t.vx, t.vy, t.vz);
        SetRotMatrix(&world_matrix);
        gte_ldv3(&d->vertices[0], &d->vertices[1], &d->vertices[2]);
        gte_rtpt();
        int32_t flag;
        gte_stflg(&flag);
        if ((flag & (GTE_FLAG_SZ_OTZ | GTE_FLAG_DIVIDE | GTE_FLAG_SX | GTE_FLAG_SY)) == 0) {
            POLY_FT3 *p = &d->packet[buffer_num];
            gte_stsxy3_ft3(p);
            gte_avsz3();
            int32_t otz;
            gte_stotz(&otz);
            addPrim(&cdb->ot[otz], p);
            CVECTOR c = debris_colour;
            gte_ldrgb(&c);
            gte_dpcs();
            gte_strgb(&c);
            p->r0 = c.r;
            p->g0 = c.g;
            p->b0 = c.b;
            p->code = c.cd;
        }
        i = (i - 1) & (DEBRIS_COUNT - 1);
        d->life--;
    }
}

Object wheel_part_objects[FLYING_PARTS];
Object bonnet_objects[FLYING_PARTS];
Object boot_objects[FLYING_PARTS];
int32_t bonnet_index;
int32_t boot_index;

static FlyingObject flying[24];  /* 0x800A325C */
static int32_t wheel_index;      /* 0x80079C34 */
static int32_t flying_wheels;    /* 0x80079C44 */
static int32_t flying_bonnets;   /* 0x80079A78 */
static int32_t flying_boots;     /* 0x80079BF0 */
/* The record set up last. The records go round with `& 0x17`, which after the first one only ever
 * visits 16..23 (and 7, never used, ends the walk back in Update_Flying_Objects): eight live at most. */
static uint32_t last_flying;     /* 0x80079AD8 */

#define FLYING_NEXT(i) (((i) + 1) & 0x17)
#define FLYING_PREV(i) (((i) - 1) & 0x17)

static void stop_spin(FlyingObject *f)
{
    f->spin[0] = f->spin[1] = f->spin[2] = f->spin[3] = 0;
}

void Init_Flying_Objects(void)
{
    wheel_index = bonnet_index = boot_index = 7;
    flying_wheels = flying_bonnets = flying_boots = 0;
    last_flying = 0x17;
    for (int i = 0; i < 24; i++) {
        FlyingObject *f = &flying[i];
        memset(f->position, 0, sizeof(f->position));
        memset(f->velocity, 0, sizeof(f->velocity));
        memset(f->bounce_velocity, 0, sizeof(f->bounce_velocity));
        memset(f->angle, 0, sizeof(f->angle));
        stop_spin(f);
        f->active = 0;
        f->unused_90 = 0;
        f->timer = 0;
        f->settled = 0;
        f->type = -1;
    }
    for (int i = 0; i < FLYING_PARTS; i++)
        Create_Object(&wheel_part_objects[i], (Mesh *)level_data_section(18));
    for (int i = 0; i < FLYING_PARTS; i++)
        Create_Object(&bonnet_objects[i], (Mesh *)level_data_section(19));
    for (int i = 0; i < FLYING_PARTS; i++)
        Create_Object(&boot_objects[i], (Mesh *)level_data_section(20));
}

/* Where a part starts: the car's position plus `offset` turned with the car. */
static void launch_from(FlyingObject *f, int car, const SVECTOR *offset)
{
    SetRotMatrix(&car_states[car].matrix);
    gte_ldv0(offset);
    gte_rtv0();
    VECTOR v;
    gte_stlvnl(&v);
    f->position[0] += v.vx;
    f->position[1] += v.vy;
    f->position[2] += v.vz;
}

/* A part comes off car `car` moving at `velocity` (20.12): a wheel (`wheel` its corner), the bonnet or
 * the boot. It flies up by the car's speed and tumbles. */
void Setup_Flying_Objects(int type, const int32_t velocity[3], int wheel, int car)
{
    static const SVECTOR bonnet_offset = {0, 0x1d, 0x135, 0}; /* 0x80079154 */
    static const SVECTOR boot_offset = {0, 0x27, -0x179, 0};  /* 0x8007915C */
    uint32_t n = FLYING_NEXT(last_flying);
    FlyingObject *f = &flying[n];
    const CarState *cs = &car_states[car];
    const CarBlock *cb = &car_blocks[car];
    memcpy(f->position, cs->position, sizeof(f->position));
    if (type == FLYING_WHEEL) {
        wheel_index = (wheel_index + 1) & 7;
        flying_wheels++;
        f->object = wheel_part_objects[wheel_index];
        f->type = FLYING_WHEEL;
        f->fd = wheel_fd[car][wheel];
        const CarWheelState *w = &cs->wheel[wheel];
        memcpy(f->angle, w->angle, sizeof(f->angle));
        SVECTOR centre = {w->centre[0], w->centre[1], w->centre[2], w->centre[3]};
        launch_from(f, car, &centre);
        f->velocity[0] = velocity[0] / 4096 + (rand() & 0xf);
        f->velocity[1] = cb->speed / 8 + 0x20;
        f->velocity[2] = velocity[2] / 4096 + (rand() & 0xf);
        memcpy(f->bounce_velocity, f->velocity, sizeof(f->velocity));
        f->spin[0] = (int16_t)(cb->speed << 1);
        f->spin[1] = (int16_t)((rand() & 4) << 6);
        f->spin[2] = (int16_t)((rand() & 4) << 6);
    }
    if (type == FLYING_BONNET) {
        flying_bonnets++;
        bonnet_index = (bonnet_index + 1) & 7;
        f->object = bonnet_objects[bonnet_index];
        f->type = FLYING_BONNET;
        f->fd = car_fd[car];
        memcpy(f->angle, cs->angle, sizeof(f->angle));
        launch_from(f, car, &bonnet_offset);
        f->velocity[0] = velocity[0] / 4096;
        f->velocity[1] = cb->speed / 8 + 0x20;
        f->velocity[2] = velocity[2] / 4096;
        memcpy(f->bounce_velocity, f->velocity, sizeof(f->velocity));
        f->spin[0] = (int16_t)cb->speed;
        f->spin[1] = (int16_t)((rand() & 4) << 6);
        f->spin[2] = 0;
    }
    if (type == FLYING_BOOT) {
        flying_boots++;
        boot_index = (boot_index + 1) & 7;
        f->object = boot_objects[boot_index];
        f->type = FLYING_BOOT;
        f->fd = car_fd[car];
        memcpy(f->angle, cs->angle, sizeof(f->angle));
        launch_from(f, car, &boot_offset);
        f->velocity[0] = velocity[0] / 4096;
        f->velocity[1] = cb->speed / 8 + 0x20;
        f->velocity[2] = velocity[2] / 4096;
        memcpy(f->bounce_velocity, f->velocity, sizeof(f->velocity));
        f->spin[0] = (int16_t)cb->speed;
        f->spin[1] = (int16_t)((rand() & 4) << 4);
        f->spin[2] = (int16_t)((rand() & 4) << 4);
    }
    f->active = 1;
    f->fd.x = f->position[0];
    f->fd.y = f->position[1];
    f->fd.z = f->position[2];
    f->fd.w = f->position[3];
    f->timer = 0x200;
    last_flying = n;
}

static void Zero_Flying_Object(int i) /* 0x80013E98 */
{
    flying[i].active = 0;
    flying[i].unused_90 = 0;
    flying[i].timer = 0;
    flying[i].type = -1;
    flying[i].settled = 0;
}

int Request_Flying_Object(int type)
{
    if (type == FLYING_WHEEL)
        return flying_wheels < FLYING_PARTS;
    if (type == FLYING_BONNET)
        return flying_bonnets < FLYING_PARTS;
    if (type == FLYING_BOOT)
        return flying_boots < FLYING_PARTS;
    return 0;
}

static void Flying_Objects_Pos_Ang(int i) /* 0x80014AB0 */
{
    FlyingObject *f = &flying[i];
    f->position[0] += f->velocity[0];
    f->position[1] += f->velocity[1];
    f->position[2] += f->velocity[2];
    f->angle[0] = (int16_t)(f->angle[0] + f->spin[0]);
    f->angle[1] = (int16_t)(f->angle[1] + f->spin[1]);
    f->angle[2] = (int16_t)(f->angle[2] + f->spin[2]);
}

/* A bonnet or boot at rest the frame before lies flat: its pitch goes to the nearest of flat and
 * upside down, a step of 0x100 at a time. */
static void Flying_Object_Settle_Angles(int i) /* 0x80014C04 */
{
    FlyingObject *f = &flying[i];
    if (f->settled != 1)
        return;
    f->angle[0] = (int16_t)(f->angle[0] & ANGLE_MASK);
    f->angle[2] = 0;
    if ((uint16_t)f->angle[0] <= 0x100)
        f->angle[0] = 0;
    unsigned a = (uint16_t)f->angle[0];
    if (a >= 0x101 && a < HALF_TURN) {
        f->angle[0] = (int16_t)(f->angle[0] - 0x100);
        a = (uint16_t)f->angle[0];
    }
    if (a >= HALF_TURN && a < 0xf00)
        f->angle[0] = (int16_t)(a + 0x100);
    if ((uint16_t)f->angle[0] >= 0xf00 && (uint16_t)f->angle[0] < TURN)
        f->angle[0] = ANGLE_MASK;
}

/* The side of the line a->b the point is on (< 0 outside the track edge). */
static int32_t edge_side(const TrackFd *fd, const TrackVertex *a, const TrackVertex *b)
{
    return (fd->x - a->x) * (b->z - a->z) - (fd->z - a->z) * (b->x - a->x);
}

/* Past the track's outer edges (or, in the bowls, outside a radius of 15000), where the part doesn't
 * bounce. The bowls still get the edge tests of their square grid. */
static int Flying_Object_Off_Track(int i) /* 0x80014D3C */
{
    const TrackFd *fd = &flying[i].fd;
    int off = 0;
    if (current_level >= FIRST_BOWL_LEVEL) {
        /* the products wrap in 32 bits as on the PSX */
        uint32_t d = (uint32_t)fd->x * (uint32_t)fd->x + (uint32_t)fd->z * (uint32_t)fd->z;
        off = 225000000 < (int32_t)d;
    }
    int type = fd->strip_type;
    if (fd->segment == 0 && edge_side(fd, &strip_vertices[fd->left_vertex + left_edge[type][0]],
                                      &strip_vertices[fd->right_vertex + left_edge[type][1]]) < 0)
        off = 1;
    if (fd->segment == strip_at(fd->strip)->width - 1 &&
        edge_side(fd, &strip_vertices[fd->right_vertex + right_edge[type][1] + 1],
                  &strip_vertices[fd->left_vertex + right_edge[type][0] + 1]) < 0)
        off = 1;
    return off;
}

/* Toward 0 by `step` (a spin of less than `step` overshoots and swings round 0, as on the PSX). */
static int16_t ease16(int16_t v, int step)
{
    if (v > 0)
        v = (int16_t)(v - step);
    if (v < 0)
        v = (int16_t)(v + step);
    return v;
}

static int32_t ease32(int32_t v)
{
    if (v > 0)
        v--;
    if (v < 0)
        v++;
    return v;
}

/* At rest: on the ground, still. */
static void land(FlyingObject *f)
{
    f->position[1] = f->fd.y;
    memset(f->velocity, 0, sizeof(f->velocity));
    stop_spin(f);
}

/* Falls, and bounces where it reaches the ground on the track: a wheel loses a fifth of its bounce and
 * kicks off sideways, a bonnet half, a boot three tenths. */
static void Update_Flying_Object(int i) /* 0x80013F58 */
{
    FlyingObject *f = &flying[i];
    f->fd.x = f->position[0];
    f->fd.y = f->position[1];
    f->fd.z = f->position[2];
    f->fd.w = f->position[3];
    Track_Follow(&f->fd);
    Map_Height(&f->fd);
    int off = Flying_Object_Off_Track(i);
    if (f->type == FLYING_WHEEL) {
        if (f->position[1] <= f->fd.y && !off) {
            int32_t vy = (f->bounce_velocity[1] << 3) / 10;
            f->bounce_velocity[1] = vy;
            f->velocity[1] = vy;
            int32_t bx = f->bounce_velocity[0] < 0 ? -f->bounce_velocity[0] : f->bounce_velocity[0];
            int32_t bz = f->bounce_velocity[2] < 0 ? -f->bounce_velocity[2] : f->bounce_velocity[2];
            rand();
            if (bz < bx)
                f->velocity[2] = -(f->velocity[2] * 6 / 10);
            else
                f->velocity[0] = -(f->velocity[0] * 6 / 10);
            f->spin[1] = (int16_t)((rand() & 4) << 6);
            f->spin[2] = (int16_t)((rand() & 4) << 6);
        }
        if (f->bounce_velocity[1] < 8) {
            /* lies on its side */
            f->position[1] = f->fd.y;
            stop_spin(f);
            f->angle[2] = QUARTER_TURN;
            f->angle[0] = 0;
            memset(f->velocity, 0, sizeof(f->velocity));
        } else {
            f->velocity[1] -= 4;
            Flying_Objects_Pos_Ang(i);
            f->spin[0] = ease16(f->spin[0], 1);
            f->spin[1] = ease16(f->spin[1], 1);
        }
    }
    if (f->type == FLYING_BONNET) {
        if (f->position[1] < f->fd.y && !off) {
            int32_t vy = f->bounce_velocity[1] / 2;
            f->bounce_velocity[1] = vy;
            f->velocity[1] = vy;
            f->spin[0] = (int16_t)((rand() & 4) << 5);
            f->spin[1] = (int16_t)((rand() & 4) << 5);
            f->spin[2] = (int16_t)((rand() & 4) << 5);
        }
        if (f->bounce_velocity[1] == 0) {
            land(f);
            Flying_Object_Settle_Angles(i);
            f->settled = 1;
        } else {
            f->velocity[1] -= 4;
            Flying_Objects_Pos_Ang(i);
            f->spin[0] = ease16(f->spin[0], 8);
            f->spin[1] = ease16(f->spin[1], 8);
            f->velocity[0] = ease32(f->velocity[0]);
            f->velocity[2] = ease32(f->velocity[2]);
        }
    }
    if (f->type == FLYING_BOOT) {
        if (f->position[1] < f->fd.y && !off) {
            int32_t vy = f->bounce_velocity[1] * 7 / 10;
            f->bounce_velocity[1] = vy;
            f->velocity[1] = vy;
            f->spin[1] = (int16_t)((rand() & 4) << 4);
            f->spin[2] = (int16_t)((rand() & 4) << 4);
        }
        if (f->bounce_velocity[1] == 0) {
            land(f);
            Flying_Object_Settle_Angles(i);
            f->settled = 1;
        } else {
            f->velocity[1] -= 4;
            Flying_Objects_Pos_Ang(i);
            f->spin[0] = ease16(f->spin[0], 1);
            f->spin[1] = ease16(f->spin[1], 1);
            f->velocity[0] = ease32(f->velocity[0]);
            f->velocity[2] = ease32(f->velocity[2]);
        }
    }
}

/* Draws the flying parts, newest first, and moves them; a part lives 0x200 frames. */
void Update_Flying_Objects(void)
{
    uint32_t i = last_flying;
    if (flying[i].timer <= 0)
        return;
    do {
        FlyingObject *f = &flying[i];
        MATRIX m, light;
        Calc_Object_MatrixYXZ(&m, f->angle);
        MulMatrix0(&scene_light_matrix, &m, &light);
        SetLightMatrix(&light);
        SetColorMatrix(&scene_colour_matrix);
        Set_Object_View(&m, f->position);
        VVDraw_Object(&f->object);
        f->timer--;
        if (f->type == FLYING_WHEEL) {
            if (f->timer == 0) {
                flying_wheels--;
                Zero_Flying_Object((int)i);
            } else {
                Update_Flying_Object((int)i);
            }
        }
        if (f->type == FLYING_BONNET) {
            if (f->timer == 0) {
                flying_bonnets--;
                Zero_Flying_Object((int)i);
            } else {
                Update_Flying_Object((int)i);
            }
        }
        if (f->type == FLYING_BOOT) {
            if (f->timer == 0) {
                flying_boots--;
                Zero_Flying_Object((int)i);
            } else {
                Update_Flying_Object((int)i);
            }
        }
        i = FLYING_PREV(i);
    } while (i != last_flying && flying[i].timer > 0);
}
