#include "game/collision.h"

#include "game/ai.h"
#include "game/car.h"
#include "game/car_motion.h"
#include "game/debris.h"
#include "game/denting.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/particles.h"
#include "game/play_game.h"
#include "game/sound_fx.h"
#include "game/track.h"
#include "game/wheels.h"
#include "graphics/object.h"
#include "psx/libc.h"
#include "psx/libgte.h"
#include "sound/sound.h"
#include "system/maths.h"

#define WHEEL_TEXELS 0x1f

/* Each wheel's hub-cap overlay: its packets, where the wheel sits on the car, and the overlay's side
 * (the player's car is narrower inside). */
void Init_Car_Wheel_Overlays(int car)
{
    Setup_Sprite_Quad(NULL, "WHEEL2", &wheel_sprites[1]);
    Setup_Sprite_Quad(NULL, "WHEEL3", &wheel_sprites[2]);
    CarState *cs = &car_states[car];
    for (int w = 0; w < 4; w++) {
        for (int b = 0; b < 2; b++) {
            POLY_FT4 *p = &cs->wheel[w].packets[b];
            Setup_Sprite_Quad(p, "WCO", &wheel_sprites[0]);
            p->u1--;
            p->v2--;
            p->u3--;
            p->v3--;
        }
    }
    static const int16_t centres[4][3] = {
        {-0x98, -0x3c, -0xe8}, {0x98, -0x3c, -0xe8}, {-0x98, -0x3c, 0x123}, {0x98, -0x3c, 0x123},
    };
    for (int w = 0; w < 4; w++) {
        cs->wheel[w].centre[0] = centres[w][0];
        cs->wheel[w].centre[1] = centres[w][1];
        cs->wheel[w].centre[2] = centres[w][2];
    }
    for (int w = 0; w < 4; w++) {
        cs->wheel[w].angle[0] = 0;
        cs->wheel[w].angle[1] = 0;
        cs->wheel[w].angle[2] = 0;
        cs->wheel[w].spin = (int16_t)(rand() & 0xfe);
    }
    /* the other cars' overlays are not centred: -0x17 left, 0x2B right */
    int16_t left = car == current_player_car ? -0x21 : -0x17;
    int16_t right = car == current_player_car ? 0x21 : 0x2b;
    for (int k = 0; k < 4; k++) {
        cs->wheel[0].corner[k].x = left;
        cs->wheel[1].corner[k].x = right;
        cs->wheel[2].corner[k].x = left;
        cs->wheel[3].corner[k].x = right;
    }
}

void Init_Wheel_Objects(void)
{
    for (int car = 0; car < num_cars; car++)
        Init_Car_Wheel_Overlays(car);
    Create_Object(&wheel_objects[0], (Mesh *)level_data_section(5));
    Create_Object(&wheel_objects[1], (Mesh *)level_data_section(6));
    Create_Object(&wheel_objects[2], (Mesh *)level_data_section(5));
    Create_Object(&wheel_objects[3], (Mesh *)level_data_section(6));
}

int32_t barrier_remark_wait; /* 0x80079BCC: frames before the commentator remarks on another barrier hit */
int32_t commentary_busy;     /* 0x80079B4C: frames the commentary channel stays taken */

/* Where a point has crossed the barrier: the barrier's direction (as an angle) and a point on it.
 * On tracks the barriers are the outer edges of the first and last quads across the strip; in the
 * bowls the circular wall. Returns 0 when the point is inside. */
static int barrier_crossed(const TrackFd *fd, int32_t px, int32_t pz, int32_t wall[3], int *angle)
{
    if (current_level < FIRST_BOWL_LEVEL) {
        int type = fd->strip_type;
        if (fd->segment == 0) {
            const TrackVertex *a = &strip_vertices[fd->left_vertex + left_edge[type][0]];
            const TrackVertex *b = &strip_vertices[fd->right_vertex + left_edge[type][1]];
            if ((px - a->x) * (b->z - a->z) - (pz - a->z) * (b->x - a->x) < 0) {
                *angle = Calc_Angle(b->z - a->z, b->x - a->x);
                wall[0] = a->x;
                wall[1] = a->y;
                wall[2] = a->z;
                return 1;
            }
        }
        if (fd->segment == strip_at(fd->strip)->width - 1) {
            const TrackVertex *a = &strip_vertices[fd->right_vertex + right_edge[type][1] + 1];
            const TrackVertex *b = &strip_vertices[fd->left_vertex + right_edge[type][0] + 1];
            if ((px - a->x) * (b->z - a->z) - (pz - a->z) * (b->x - a->x) < 0) {
                *angle = Calc_Angle(b->z - a->z, b->x - a->x);
                wall[0] = a->x;
                wall[1] = a->y;
                wall[2] = a->z;
                return 1;
            }
        }
        return 0;
    }
    int32_t r = bowl_radius[current_level];
    if (r * r >= px * px + pz * pz)
        return 0;
    *angle = -Calc_Angle(px, pz);
    wall[0] = -r * rsin(*angle) / 4096;
    wall[1] = 0;
    wall[2] = r * rcos(*angle) / 4096;
    return 1;
}

static ParticleParams spark_params(void)
{
    int32_t p = projection_distance;
    ParticleParams pp = {p * 0x19, p * 0x32, p * -0x19 / 128, 0, 0xff000, 0xff000, 0xff000,
                         0xe000, 0xe000, 0xe000, -0x18199, -0x18199, -0x18199, 1};
    return pp;
}

void Spark_At(const VECTOR *pos, int car)
{
    ParticleParams pp = spark_params();
    Sparks(pos, car_blocks[car].velocity, &particles[Pop_Particle_Slot()], &pp);
}

static void spark_at_fd(const TrackFd *fd, int car)
{
    VECTOR pos = {fd->x, fd->y, fd->z, 0};
    Spark_At(&pos, car);
}

typedef struct {
    int32_t impact;    /* how hard (negative) */
    int32_t into;      /* the normal speed of the centre */
    int32_t slide;     /* the speed along the wall after the hit */
    int32_t push[2];   /* how far the car moved out of the wall (20.12) */
} BarrierHit;

/* The impulse of the barrier on the car: `into` the barrier's normal speed at the contact (with the
 * spin's part), turned into a bounce off it with a little drag along it, a spin, and the car moved
 * back out of the wall. 0 when the point is already moving away from the wall. */
static int barrier_impulse(int car, const TrackFd *at, const int32_t wall[3], int angle, BarrierHit *hit)
{
    CarBlock *cb = &car_blocks[car];
    const TrackFd *fd = &car_fd[car];
    int32_t c = rcos(angle), s = rsin(angle);
    hit->into = ((cb->velocity[2] >> 4) * c - (cb->velocity[0] >> 4) * s) >> 8;
    int32_t lever = ((fd->x - at->x) * c + (fd->z - at->z) * s) / 4096;
    int32_t along = ((cb->velocity[0] >> 4) * c + (cb->velocity[2] >> 4) * s) >> 8;
    int32_t arm = ((fd->z - at->z) * c - (fd->x - at->x) * s) / 4096;
    hit->impact = -(lever * (cb->angular_velocity[1] / 0x28c)) - hit->into;
    if (hit->impact > 0)
        return 0;
    int32_t spin = cb->angular_velocity[1] + lever * (hit->impact >> 13) * 8 + ((along * arm) >> 17);
    hit->slide = along - along / 16;
    if ((hit->slide >= 0) != (along >= 0))
        hit->slide = 0;
    int32_t bounce = -lever * (spin / 0x28c) + (hit->impact >> 2) + 0x800;
    if (car_states[car].drawn != 0 && abs(hit->impact) > 0x20000) {
        int32_t v[3] = {cb->velocity[0], cb->velocity[1], cb->velocity[2]};
        int n = abs(hit->impact) / 0x10000;
        Setup_Debris(&at->x, v, n > 0xc ? 0xc : n, car);
    }
    int32_t out = ((wall[2] - at->z) * c - (wall[0] - at->x) * s) / 4096;
    hit->push[0] = -out * s;
    hit->push[1] = out * c;
    cb->angular_velocity[1] = spin;
    cb->position[0] += hit->push[0];
    cb->position[2] += hit->push[1];
    cb->velocity[0] = ((hit->slide >> 4) * c - (bounce >> 4) * s) / 256;
    cb->velocity[2] = ((bounce >> 4) * c + (hit->slide >> 4) * s) / 256;
    return 1;
}

/* How much a hit of `impact` adds to a damage zone (and its copy), up to `limit`. */
static void add_damage(CarBlock *cb, int zone, int32_t impact, int32_t limit)
{
    int32_t d = abs(impact / 2048);
    cb->damage[zone] += d;
    if (cb->damage[zone] > MAX_DAMAGE)
        cb->damage[zone] = limit;
    cb->damage[zone + 1] = cb->damage[zone];
}

/* A wheel (its track record) against the barriers: bounces the car, damages and dents it by the wheel's
 * corner (0 front left, 1 rear left, 2 front right, 3 rear right), sparks, and the commentator may
 * remark on it. */
int Barrier_Collision(int wheel, int car)
{
    TrackFd *w = &wheel_fd[car][wheel];
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    int32_t wall[3];
    int angle = 0;
    if (!barrier_crossed(w, w->x, w->z, wall, &angle))
        return 0;
    BarrierHit hit;
    if (!barrier_impulse(car, w, wall, angle, &hit))
        return 0;
    cs->angle[1] = (int16_t)(cb->angle[1] >> 12);
    cs->position[0] = cb->position[0] >> 12;
    cs->position[2] = cb->position[2] >> 12;
    int32_t dx = hit.push[0] / 4096, dz = hit.push[1] / 4096;
    for (int k = 0; k < 4; k++) {
        wheel_fd[car][k].x += dx;
        wheel_fd[car][k].z += dz;
    }
    int32_t impact = abs(hit.impact);
    if (impact > 0x20000) {
        Object *model = car == current_player_car ? &car_objects[car][0] : &car_objects[car][1];
        int parts = car == current_player_car ? 0 : 0xb;
        switch (wheel) {
        case 0:
            add_damage(cb, 0, hit.impact, MAX_DAMAGE);
            if (impact > 0xfa000)
                Check_Bonnet_Removal(car);
            Dent_Car_Zone(model, cb, 0, parts, car);
            break;
        case 1:
            add_damage(cb, 7, hit.impact, cb->wheel_lost[1] == 1 ? MAX_DAMAGE : MAX_DAMAGE - 1);
            if (impact > 0x64000)
                Detach_Damaged_Wheel(car, 3);
            if (impact > 0xfa000)
                Check_Boot_Removal(car);
            Dent_Car_Zone(model, cb, 1, parts, car);
            break;
        case 2:
            add_damage(cb, 2, hit.impact, MAX_DAMAGE);
            if (impact > 0xfa000)
                Check_Bonnet_Removal(car);
            Dent_Car_Zone(model, cb, 0, parts, car);
            break;
        default:
            add_damage(cb, 5, hit.impact, cb->wheel_lost[0] == 1 ? MAX_DAMAGE : MAX_DAMAGE - 1);
            if (impact > 0x64000)
                Detach_Damaged_Wheel(car, 1);
            if (impact > 0xfa000)
                Check_Boot_Removal(car);
            Dent_Car_Zone(model, cb, 1, parts, car);
            break;
        }
        Allocate_Sound_Effect(SAMPLE_CRASH, CAR_SOUND_VOLUME, &w->x);
    }
    if (cs->drawn != 0 && abs(hit.slide) > 0xa000) {
        spark_at_fd(w, car);
        if (hit.into > 0x32000) {
            spark_at_fd(w, car);
            spark_at_fd(w, car);
            rand();
        }
    }
    if (barrier_remark_wait == 0 && car == current_player_car && commentary_busy == 0 && cb->speed >= 0x33) {
        static const int remarks[4] = {SAY_BARRIER_A, SAY_BARRIER_B, SAY_BARRIER_C, SAY_BARRIER_D};
        int pick = cb->position[0] & 3;
        int sample = pick == 3 && (cb->position[2] & 1) ? SAY_BARRIER_E : remarks[pick];
        Play_Sound(COMMENTARY_CHANNEL, sample_bank, sample, SOUND_VOLUME_MAX, VOICE_PITCH, 0);
        commentary_busy = 55;
        barrier_remark_wait = 300;
    }
    for (int k = 0; k < 4; k++)
        cb->wheel_rise[k] = NO_RISE;
    return 1;
}

/* A body corner (off its wheels) against the barriers: bounces the car and sparks. */
int Barrier_Corner_Collision(int corner, int car)
{
    TrackFd *c = &corner_fd[corner];
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    int32_t wall[3];
    int angle = 0;
    if (!barrier_crossed(c, c->x, c->z, wall, &angle))
        return 0;
    BarrierHit hit;
    if (!barrier_impulse(car, c, wall, angle, &hit))
        return 0;
    cs->angle[1] = (int16_t)(cb->angle[1] / 4096);
    cs->position[0] = cb->position[0] / 4096;
    cs->position[2] = cb->position[2] / 4096;
    spark_at_fd(c, car);
    return 1;
}

/* Spins scored for the car that caused them (pointy_bits): a quarter turn 10 points, a half 25, a full
 * one 50 at once; wrecking the car 25 and a wreck to its count. Points stop at 999. */
void CheckPointScoring(void)
{
    for (int car = 0; car < 20; car++) {
        PointyRecord *p = &pointy_bits[car];
        if (p->timer != 0) {
            int by = p->cause;
            p->timer--;
            int32_t heading = Get_Car_Angle(car_states[car].angle[1]) & ANGLE_MASK;
            int32_t d = heading - p->heading, c;
            Get_Direction_Cosines(&d, &c);
            p->turn += d < c ? d : -c;
            p->heading = heading;
            if (car_blocks[by].wrecked != 0)
                continue;
            if (car_blocks[car].wrecked != 0) {
                race_records[by].wrecks++;
                race_records[by].points += 25;
                p->timer = 0;
            } else {
                int32_t turn = p->turn;
                if (abs(turn) >= HALF_TURN)
                    p->flags |= POINTY_HALF_TURN;
                else if (abs(turn) >= QUARTER_TURN)
                    p->flags |= POINTY_QUARTER_TURN;
                if (abs(turn) >= TURN) {
                    race_records[by].points += 50;
                    p->timer = 0;
                } else if (p->timer == 0) {
                    if (p->flags & POINTY_HALF_TURN)
                        race_records[by].points += 25;
                    else if (p->flags & POINTY_QUARTER_TURN)
                        race_records[by].points += 10;
                }
            }
        }
        if (race_records[car].points > 999)
            race_records[car].points = 999;
    }
}
