#include "game/particles.h"

#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libc.h"

#define PARTICLE_END 0x8000   /* frame list: the particle dies */
#define PARTICLE_LOOP 0x8001  /* back to the first frame */
#define PARTICLE_HOLD 0x8002  /* stay on the last frame */
#define SPRITE_SIZE 63
#define SPARK_SIZE 15
#define MAX_HALF_SIZE 0x50    /* bigger on screen than this and it is not drawn */

Particle particles[MAX_PARTICLES];

/* Free slots as a stack (the top is the next to use), and where each particle is in it. */
static int16_t slot_stack[MAX_PARTICLES]; /* 0x800A6F60 */
static int16_t slot_place[MAX_PARTICLES]; /* 0x8007FF7C */
static int slot_top;                      /* 0x800794BC, as an index */

enum { SMOKE_SPRITES = 0, DUST_SPRITES = 8, SPARK_SPRITE = 16 };
#define PARTICLE_SPRITE {0, 0, 0x80, 0x80, 0x80, 0, 1, 1, 0}
static Sprite particle_sprites[17] = { /* 0x800702CC: grey, additive */
    PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE,
    PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE,
    PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE, PARTICLE_SPRITE,
};

static const uint16_t smoke_frames[] = { /* 0x80070464 */
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, PARTICLE_HOLD,
};
static const uint16_t steam_frames[] = { /* 0x80070488 */
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, PARTICLE_HOLD,
};
static const uint16_t fire_frames[] = { /* 0x800704CC */
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, PARTICLE_HOLD,
};

static const MATRIX no_translation = {{{0}}, {0}}; /* 0x80070500 */

void InitialiseParticleSystem(void)
{
    static const char *const names[17] = {
        "SMOKE1", "SMOKE2", "SMOKE3", "SMOKE4", "SMOKE5", "SMOKE6", "SMOKE7", "SMOKE8",
        "DUST1", "DUST2", "DUST3", "DUST4", "DUST5", "DUST6", "DUST7", "DUST8", "SPARK",
    };
    for (int i = 0; i < 17; i++)
        Setup_Sprite_Quad(NULL, names[i], &particle_sprites[i]);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        setPolyFT4(&particles[i].packet[0]);
        setPolyFT4(&particles[i].packet[1]);
        particles[i].life = 0;
        slot_stack[i] = (int16_t)i;
        slot_place[i] = (int16_t)i;
    }
    slot_top = MAX_PARTICLES - 1;
}

/* This frame's packet shows sprite `s`, `size` texels square, in the particle's colour. */
static void set_frame(Particle *p, const Sprite *s, int size, int blend)
{
    POLY_FT4 *q = &p->packet[buffer_num];
    q->u0 = s->u;
    q->v0 = s->v;
    q->u1 = (uint8_t)(s->u + size);
    q->v1 = s->v;
    q->u2 = s->u;
    q->v2 = (uint8_t)(s->v + size);
    q->u3 = (uint8_t)(s->u + size);
    q->v3 = (uint8_t)(s->v + size);
    q->tpage = blend < 0 ? s->tpage : (uint16_t)((s->tpage & ~TPAGE_ABR) | blend << 5);
    q->clut = s->clut;
}

/* Grows (or shrinks) towards max_size. */
static void grow_size(Particle *p)
{
    if (p->size < p->max_size)
        p->size += p->grow;
    else
        p->size = p->max_size;
}

static void fade(Particle *p)
{
    if (((uint32_t)p->r & 0xfffff000u) != ((uint32_t)p->end_r & 0xfffff000u)) {
        p->r += p->dr;
        p->g += p->dg;
        p->b += p->db;
    }
    POLY_FT4 *q = &p->packet[buffer_num];
    q->r0 = (uint8_t)(p->r >> 12);
    q->g0 = (uint8_t)(p->g >> 12);
    q->b0 = (uint8_t)(p->b >> 12);
}

static int smoke_update(Particle *p) /* 0x8002D1F4 */
{
    if (--p->life == 0)
        return 0;
    set_frame(p, &particle_sprites[SMOKE_SPRITES + p->frames[p->frame]], SPRITE_SIZE, p->blend);
    p->y += 10;
    grow_size(p);
    p->frame++;
    fade(p);
    return 1;
}

static int fire_update(Particle *p) /* 0x8002D528 */
{
    if (--p->life == 0)
        return 0;
    set_frame(p, &particle_sprites[DUST_SPRITES + p->frames[p->frame]], SPRITE_SIZE, -1);
    p->y += p->vy;
    grow_size(p);
    p->frame++;
    fade(p);
    return 1;
}

static int sparks_update(Particle *p) /* 0x8002DBB8 */
{
    if (--p->life == 0)
        return 0;
    set_frame(p, &particle_sprites[SPARK_SPRITE], SPARK_SIZE, -1);
    p->vy -= 4;
    p->x += p->vx;
    p->y += p->vy;
    p->z += p->vz;
    grow_size(p);
    fade(p);
    return 1;
}

static int steam_update(Particle *p) /* 0x8002DFF4 */
{
    if (--p->life == 0)
        return 0;
    set_frame(p, &particle_sprites[SMOKE_SPRITES + p->frames[p->frame]], SPRITE_SIZE, p->blend);
    p->y += p->vy;
    grow_size(p);
    p->frame++;
    fade(p);
    return 1;
}

/* Common set-up: translucent and tinted, sized and coloured from the parameters. */
static void start(Particle *p, int life, int (*update)(Particle *), const ParticleParams *pp)
{
    p->life = life;
    p->update = update;
    for (int b = 0; b < 2; b++) {
        p->packet[b].code |= 2;
        setShadeTex(&p->packet[b], 0);
    }
    p->max_size = pp->max_size;
    p->size = pp->size;
    p->grow = pp->grow;
    p->frame = 0;
    p->r = pp->r;
    p->g = pp->g;
    p->b = pp->b;
    p->end_r = pp->end_r;
    p->end_g = pp->end_g;
    p->end_b = pp->end_b;
    p->dr = pp->dr;
    p->dg = pp->dg;
    p->db = pp->db;
}

void Smoke(const VECTOR *pos, Particle *p, const ParticleParams *pp)
{
    start(p, 0x10, smoke_update, pp);
    p->x = pos->vx;
    p->y = pos->vy;
    p->z = pos->vz;
    p->vy = 10;
    p->vx = 1;
    p->frames = smoke_frames;
    p->blend = (uint8_t)pp->blend;
}

void Fire(const VECTOR *pos, Particle *p, const ParticleParams *pp)
{
    start(p, 0x10, fire_update, pp);
    p->x = (rand() & 0xf) + pos->vx - 7;
    p->y = pos->vy;
    p->z = (rand() & 0xf) + pos->vz - 7;
    p->vy = (rand() & 0xf) + 10;
    p->vx = 0;
    p->frames = fire_frames;
}

void Sparks(const VECTOR *pos, const int32_t vel[3], Particle *p, const ParticleParams *pp)
{
    start(p, 0x10, sparks_update, pp);
    p->x = pos->vx - vel[0] / 4096;
    p->y = pos->vy + 100;
    p->z = pos->vz - vel[2] / 4096;
    p->vx = vel[0] / 4096 - 0x10 + (rand() & 0x1f);
    p->vy = (rand() & 0xf) * 3;
    p->vz = vel[2] / 4096 - 0x10 + (rand() & 0x1f);
    p->frames = NULL;
}

void Steam(const VECTOR *pos, Particle *p, const ParticleParams *pp)
{
    start(p, 0x20, steam_update, pp);
    p->x = (rand() & 0x3f) + pos->vx - 0x1f;
    p->y = pos->vy;
    p->z = (rand() & 0x3f) + pos->vz - 0x1f;
    p->vy = 0x20;
    p->frames = steam_frames;
    p->blend = (uint8_t)pp->blend;
}

/* The next free particle; with none free, the oldest one is taken again. */
int Pop_Particle_Slot(void)
{
    if (slot_top < 0) {
        slot_top = MAX_PARTICLES - 2;
        return slot_stack[MAX_PARTICLES - 1];
    }
    return slot_stack[slot_top--];
}

/* A dead particle goes back on top of the free stack (swapping places with the one there). */
static void free_slot(int i)
{
    if (++slot_top > MAX_PARTICLES - 1)
        slot_top = 0;
    int16_t was = slot_place[i];
    int16_t other = slot_stack[slot_top];
    slot_stack[slot_top] = (int16_t)i;
    slot_place[i] = (int16_t)slot_top;
    slot_stack[was] = other;
    slot_place[other] = was;
}

/* A square around the particle's projection, its size shrinking with distance, depth-cued. */
static void Draw_Particle(Particle *p) /* 0x8002E61C */
{
    SVECTOR v = {(int16_t)(p->x - world_matrix.t[0]), (int16_t)(p->y - world_matrix.t[1]),
                 (int16_t)(p->z - world_matrix.t[2]), 0};
    gte_ldv0(&v);
    gte_rtps();
    int32_t sx = gte.sx[2], sy = gte.sy[2];
    int32_t sz = gte.sz[3];
    int32_t otz = sz >> 2;
    int32_t flag;
    gte_stflg(&flag);
    if ((flag & GTE_CLIP_FLAGS) != 0)
        return;
    int32_t half = p->size / sz;
    if (half >= MAX_HALF_SIZE)
        return;
    POLY_FT4 *q = &p->packet[buffer_num];
    q->x0 = (int16_t)(sx - half);
    q->y0 = (int16_t)(sy + half);
    q->x1 = (int16_t)(sx + half);
    q->y1 = (int16_t)(sy + half);
    q->x2 = (int16_t)(sx - half);
    q->y2 = (int16_t)(sy - half);
    q->x3 = (int16_t)(sx + half);
    q->y3 = (int16_t)(sy - half);
    addPrim(&cdb->ot[otz], q);
    CVECTOR c = {q->r0, q->g0, q->b0, q->code};
    gte_ldrgb(&c);
    gte_dpcs();
    gte_strgb(&c);
    q->r0 = c.r;
    q->g0 = c.g;
    q->b0 = c.b;
    q->code = c.cd;
}

void Update_Particles(void)
{
    SetRotMatrix(&world_matrix);
    gte_SetTransMatrix(&no_translation);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        if (p->life == 0)
            continue;
        if (p->update(p) == 0) {
            free_slot(i);
            continue;
        }
        if (p->frames != NULL) {
            int f = p->frames[p->frame];
            if (f == PARTICLE_LOOP) {
                p->frame = 0;
            } else if (f == PARTICLE_END) {
                p->life = 0;
                free_slot(i);
                continue;
            } else if (f == PARTICLE_HOLD) {
                p->frame--;
            }
        }
        Draw_Particle(p);
    }
}
