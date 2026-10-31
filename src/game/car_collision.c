#include "game/car_collision.h"

#include "game/ai.h"
#include "game/car.h"
#include "game/car_motion.h"
#include "game/debris.h"
#include "game/denting.h"
#include "game/frontend.h"
#include "game/play_game.h"
#include "game/sound_fx.h"
#include "graphics/object.h"
#include "psx/libgte.h"

#include <stdlib.h>

int32_t car_collisions;

/* The two cars being tested: where each one's corners are inside the other (by Check_2D_Car_Collision),
 * and where they are and face, stepped back through the frame to find the moment of contact. */
static int32_t clsn_corners[2][8]; /* 0x8007B464 */
static int32_t clsn_x[2];          /* 0x8007B4A4 */
static int32_t clsn_z[2];          /* 0x8007B4AC */
static int32_t clsn_yaw[2];        /* 0x8007B4C8 */

#define HALF_WIDTH 0xba
#define HALF_LENGTH 0x1c2

/* After a hard enough hit a car on its wheels (or lying on a face) is thrown onto two wheels, or onto
 * its nose or tail: {motion_state, roll_side} by its ground mode, the last row for a car on its
 * wheels. */
typedef struct {
    int32_t motion_state, roll_side;
} Knock;

static const Knock knock_roll[6] = { /* 0x80070520 */
    {2, 4}, {1, 1}, {-1, -1}, {-1, -1}, {2, 5}, {1, 0},
};
static const Knock knock_pitch[2][6] = {
    {{-1, -1}, {-1, -1}, {2, 2}, {2, 7}, {-1, -1}, {2, 3}}, /* 0x800705B0 */
    {{-1, -1}, {-1, -1}, {2, 6}, {2, 3}, {-1, -1}, {2, 2}}, /* 0x80070580 */
};

static int knock_row(const CarBlock *cb)
{
    if (cb->motion_state == MOTION_DRIVING)
        return 5;
    if (cb->motion_state == MOTION_STUCK)
        return cb->ground_mode;
    return -1;
}

static void hold_attitude(int car)
{
    CarBlock *cb = &car_blocks[car];
    for (int k = 0; k < 3; k++)
        cb->angle[k] = car_states[car].angle[k] * ONE;
}

/* A side hit rolls the car onto two wheels, away (`dir` 0) or towards (1). */
static void Knock_Car_Roll(int car, int32_t impact, int dir)
{
    CarBlock *cb = &car_blocks[car];
    int row = knock_row(cb);
    if (row < 0 || impact <= 0x64000 || (frame_count & 3) != 0)
        return;
    hold_attitude(car);
    const Knock *k = &knock_roll[row];
    if (k->motion_state == -1)
        return;
    cb->angular_velocity[2] = dir == 0 ? impact / 2 + 0x19000 : -0x19000 - impact / 2;
    cb->motion_state = k->motion_state;
    cb->roll_side = k->roll_side;
}

/* An end hit pitches the car onto its nose or tail. The original takes the edge (roll_side) from the
 * first table for both directions. */
static void Knock_Car_Pitch(int car, int32_t impact, int dir)
{
    CarBlock *cb = &car_blocks[car];
    int row = knock_row(cb);
    if (row < 0 || impact <= 0x1c2000 || (frame_count & 3) != 0)
        return;
    hold_attitude(car);
    const Knock *k = &knock_pitch[dir][row];
    if (k->motion_state == -1)
        return;
    cb->angular_velocity[0] = dir == 0 ? -0x19000 - impact / 16 : impact / 16 + 0x19000;
    cb->motion_state = k->motion_state;
    cb->roll_side = knock_pitch[0][row].roll_side;
}

static void dent(int car, int zone)
{
    if (car == current_player_car)
        Dent_Car_Zone(&car_objects[car][0], &car_blocks[car], zone, 0, car);
    else
        Dent_Car_Zone(&car_objects[car][1], &car_blocks[car], zone, 0xb, car);
}

/* A damage zone takes `amount`; over a wheel it stops just short of the limit while the wheel is on. */
static void zone_damage(CarBlock *cb, int zone, int32_t amount, int wheel)
{
    cb->damage[zone] += amount;
    if (cb->damage[zone] > MAX_DAMAGE)
        cb->damage[zone] = wheel < 0 || cb->wheel_lost[wheel] == 1 ? MAX_DAMAGE : MAX_DAMAGE - 1;
}

/* A hit on a side (right: zone 9, left: zone 4), maybe losing the wheel there, then a roll. */
static void side_hit(int car, int32_t impulse, int right, int roll_dir, int detach)
{
    zone_damage(&car_blocks[car], right ? 9 : 4, abs(impulse / 2048), right ? 3 : 2);
    if (detach >= 0 && abs(impulse) > 0x64000)
        Detach_Damaged_Wheel(car, detach);
    dent(car, right ? 2 : 3);
    Knock_Car_Roll(car, abs(impulse), roll_dir);
}

/* A hit on an end (both its corner zones), maybe losing the bonnet or boot, then a pitch. */
static void end_hit(int car, int32_t impulse, int rear, int check_parts)
{
    CarBlock *cb = &car_blocks[car];
    int32_t d = abs(impulse / 4096);
    if (rear) {
        zone_damage(cb, 5, d, 0);
        zone_damage(cb, 7, d, 1);
        cb->damage[6] = cb->damage[5];
        cb->damage[8] = cb->damage[7];
    } else {
        zone_damage(cb, 0, d, -1);
        zone_damage(cb, 2, d, -1);
        cb->damage[1] = cb->damage[0];
        cb->damage[3] = cb->damage[2];
    }
    if (check_parts && abs(impulse) > 0xc8000) {
        if (rear)
            Check_Boot_Removal(car);
        else
            Check_Bonnet_Removal(car);
    }
    dent(car, rear ? 1 : 0);
    Knock_Car_Pitch(car, abs(impulse), rear);
}

/* The corner of the other car that went in takes the hit too. */
static void corner_hit(int car, int32_t impulse, int corner)
{
    static const struct {
        int zone, wheel, rear, detach;
    } corners[4] = {{0, -1, 0, -1}, {2, -1, 0, -1}, {5, 0, 1, 1}, {7, 1, 1, 3}};
    CarBlock *cb = &car_blocks[car];
    int32_t size = abs(impulse);
    zone_damage(cb, corners[corner].zone, abs(impulse / 2048), corners[corner].wheel);
    cb->damage[corners[corner].zone + 1] = cb->damage[corners[corner].zone];
    if (size > 0xc8000) {
        if (corners[corner].rear)
            Check_Boot_Removal(car);
        else
            Check_Bonnet_Removal(car);
    }
    if (corners[corner].detach >= 0 && size > 0x64000)
        Detach_Damaged_Wheel(car, corners[corner].detach);
    dent(car, corners[corner].rear);
}

/* Debris from the contact point (offset (ox, oz) turned by (c, s) from car `a`) for both cars, and the
 * crash. */
static void crash_debris(int a, int b, int32_t impulse, int32_t ox, int32_t oz, int32_t c, int32_t s)
{
    int32_t n = abs(impulse) >> 16;
    if (n > 0xc)
        n = 0xc;
    const CarBlock *ca = &car_blocks[a];
    int32_t at[3] = {ca->position[0] / 4096 + (ox * c - oz * s) / 4096, ca->position[1] * 2 / 8192 + 0x82,
                     ca->position[2] / 4096 - (ox * s + oz * c) / 4096};
    if (car_states[a].drawn != 0)
        Setup_Debris(at, car_blocks[a].velocity, n, a);
    if (car_states[b].drawn != 0)
        Setup_Debris(at, car_blocks[b].velocity, n, b);
}

/* Undoes `t` (ONE a frame) of the cars' motion before the contact (spin given negated), or redoes
 * it after. PSX: the second car's spin turns its roll (angle[2]), not its heading. */
static void step_cars(int a, int b, int32_t spin_a, int32_t spin_b, int32_t t, int sign)
{
    CarBlock *ca = &car_blocks[a], *cb = &car_blocks[b];
    ca->angle[1] += sign * (spin_a / 4096) * t;
    ca->position[0] += sign * (ca->velocity[0] / 4096) * t;
    ca->position[2] += sign * (ca->velocity[2] / 4096) * t;
    cb->angle[2] += sign * (spin_b / 4096) * t;
    cb->position[0] += sign * (cb->velocity[0] / 4096) * t;
    cb->position[2] += sign * (cb->velocity[2] / 4096) * t;
}

/* The new velocities from their parts across (u) and along (w) the contact frame (c, s). */
static void set_velocity(CarBlock *cb, int32_t u, int32_t w, int32_t c, int32_t s)
{
    cb->velocity[0] = ((u >> 4) * c + (w >> 4) * s) >> 8;
    cb->velocity[2] = ((w >> 4) * c - (u >> 4) * s) >> 8;
}

static void sync_state(int car)
{
    const CarBlock *cb = &car_blocks[car];
    car_states[car].angle[1] = (int16_t)(cb->angle[1] / 4096);
    car_states[car].position[0] = cb->position[0] / 4096;
    car_states[car].position[2] = cb->position[2] / 4096;
}

/* The parts of a velocity across (u) and along (w) the frame (c, s). */
static void frame_velocity(const CarBlock *cb, int32_t c, int32_t s, int32_t *u, int32_t *w)
{
    *u = ((cb->velocity[0] >> 4) * c - (cb->velocity[2] >> 4) * s) >> 8;
    *w = ((cb->velocity[2] >> 4) * c + (cb->velocity[0] >> 4) * s) >> 8;
}

/* Which side of the second car a head-on hit lands on, by the cars' relative heading. The quarter is
 * worked out as for 20.12 angles, so with the 0..0xFFF headings used here it is always the first. */
static int other_side(const int table[4])
{
    return table[((clsn_yaw[1] - clsn_yaw[0]) + 0x200000) / 0x400000 & 3];
}

/* Two cars whose bodies (not corners) met: an impulse along the line between them in a frame halfway
 * between their headings, spin from where on the bodies it acts, damage to the sides that met. `t` is
 * how far back in the frame they touched. */
void Calc_Head_On_Clsn_Dynamics(int a, int b, int32_t t)
{
    static const int sides_e0[4] = {2, 0, 3, 1}; /* 0x800705E0 */
    static const int sides_f0[4] = {3, 1, 2, 0}; /* 0x800705F0 */
    static const int sides_00[4] = {1, 3, 0, 2}; /* 0x80070600 */
    static const int sides_10[4] = {0, 3, 1, 2}; /* 0x80070610 */
    CarBlock *ca = &car_blocks[a], *cb = &car_blocks[b];
    int32_t mid = ((((clsn_yaw[1] - clsn_yaw[0]) + 0x200) & 0x3ff) - 0x200) / 2 + clsn_yaw[0];
    int32_t c = rcos(mid), s = rsin(mid);
    int32_t spin_a = ca->angular_velocity[1], spin_b = cb->angular_velocity[1];
    int32_t back_a = -spin_a, back_b = -spin_b;
    int32_t u1, w1, u2, w2;
    frame_velocity(ca, c, s, &u1, &w1);
    frame_velocity(cb, c, s, &u2, &w2);
    int32_t dx = ((clsn_x[1] - clsn_x[0]) * c - (clsn_z[1] - clsn_z[0]) * s) / 4096;
    int32_t dz = ((clsn_x[1] - clsn_x[0]) * s + (clsn_z[1] - clsn_z[0]) * c) / 4096;
    int32_t impulse, ox, oz;
    int side_a, side_b;
    /* the original also compares two words nothing writes (0x8007B4CC, 0x8007B4DC), always equal, so
     * the bodies are always taken as side by side */
    if (abs(dz) * HALF_WIDTH > abs(dx) * HALF_LENGTH) {
        ox = -dx / 2;
        int32_t ox2 = dx / 2;
        if (dz < 1) {
            oz = HALF_LENGTH;
            side_b = other_side(sides_f0);
            side_a = 2;
        } else {
            oz = -HALF_LENGTH;
            side_b = other_side(sides_e0);
            side_a = 3;
        }
        impulse = (w2 - w1) + (-ox2 * back_b + ox * back_a) / 0x28c;
        if ((impulse ^ -dz) < 0)
            return;
        spin_a = HALF_WIDTH < abs(dx) ? back_a - ox * (impulse / 512) : spin_a / 2;
        spin_b = abs(dx) <= HALF_WIDTH ? spin_b / 2 : back_b + ox2 * (impulse / 512);
        int32_t k = (-ox2 * spin_b + ox * spin_a) / 0x28c + impulse / 4;
        int32_t d = w2 - w1;
        int32_t w2k = w2 + k;
        w2 = (w2 + (w1 - w2) / 3 + (w1 - k)) / 2;
        w1 = (w1 + d / 3 + w2k) / 2;
    } else {
        oz = -dz / 2;
        int32_t oz2 = dz / 2;
        if (dx < 1) {
            ox = HALF_WIDTH;
            side_b = other_side(sides_10);
            side_a = 1;
        } else {
            ox = -HALF_WIDTH;
            side_b = other_side(sides_00);
            side_a = 0;
        }
        impulse = (u2 - u1) + (oz2 * back_b - oz * back_a) / 0x28c;
        if ((impulse ^ -dx) < 0)
            return;
        spin_a = HALF_LENGTH < abs(dz) ? back_a + oz * (impulse / 512) : spin_a / 2;
        spin_b = abs(dz) <= HALF_LENGTH ? spin_b / 2 : back_b - oz2 * (impulse / 512);
        int32_t k = (oz2 * spin_b - oz * spin_a) / 0x28c + impulse / 4;
        int32_t old_u1 = u1;
        u1 = (u1 + (u2 - u1) / 3 + u2 + k) / 2;
        u2 = (u2 + (old_u1 - u2) / 3 + (old_u1 - k)) / 2;
    }
    if (abs(impulse) > 0x20000) {
        switch (side_a) {
        case 0: side_hit(a, impulse, 1, 1, -1); break;
        case 1: side_hit(a, impulse, 0, 0, -1); break;
        case 2: end_hit(a, impulse, 1, 0); break;
        default: end_hit(a, impulse, 0, 0); break;
        }
        switch (side_b) {
        case 0: side_hit(b, impulse, 1, 1, -1); break;
        case 1: end_hit(b, impulse, 1, 0); break;
        case 2: end_hit(b, impulse, 0, 0); break;
        default: side_hit(b, impulse, 0, 0, -1); break;
        }
        crash_debris(a, b, impulse, ox, oz, c, s);
        Allocate_Sound_Effect(SAMPLE_CAR_COLLISION, CAR_SOUND_VOLUME, car_states[a].position);
    }
    step_cars(a, b, back_a, back_b, t, -1);
    ca->angular_velocity[1] = -spin_a;
    set_velocity(ca, u1, w1, c, s);
    cb->angular_velocity[1] = -spin_b;
    set_velocity(cb, u2, w2, c, s);
    step_cars(a, b, spin_a, spin_b, t, 1);
    sync_state(a);
    sync_state(b);
    car_collisions++;
}

/* A corner (cx, cz in the hit car's frame `-angle`) of car b in car a: an impulse at the corner across
 * whichever face of a it is nearest, damage to that face and to b's corner. `t` is how far back in the
 * frame they touched, `which` which of the pair a is in the test, `corner` b's corner. */
void Car_Corner_Collision_Response(int a, int b, int32_t t, int32_t angle, int32_t cx, int32_t cz, int which,
                                   int corner)
{
    CarBlock *ca = &car_blocks[a], *cb = &car_blocks[b];
    int32_t back_a = -ca->angular_velocity[1], back_b = -cb->angular_velocity[1];
    int32_t c = rcos(-angle), s = rsin(-angle);
    int32_t u1, w1, u2, w2;
    frame_velocity(ca, c, s, &u1, &w1);
    frame_velocity(cb, c, s, &u2, &w2);
    int face;
    int32_t far_z = abs(HALF_LENGTH - cz), near_z = abs(cz + HALF_LENGTH);
    if (abs(HALF_WIDTH - cx) < abs(cx + HALF_WIDTH)) {
        if (far_z < near_z)
            face = far_z <= abs(HALF_WIDTH - cx) ? 2 : 0;
        else
            face = near_z <= abs(HALF_WIDTH - cx) ? 3 : 0;
    } else {
        if (far_z < near_z)
            face = far_z <= abs(cx + HALF_WIDTH) ? 2 : 1;
        else
            face = abs(cx + HALF_WIDTH) < near_z ? 1 : 3;
    }
    int other = which ^ 1;
    int32_t rx = ((clsn_x[which] - clsn_x[other]) * c - (clsn_z[which] - clsn_z[other]) * s) / 4096;
    int32_t rz = ((clsn_z[which] - clsn_z[other]) * c + (clsn_x[which] - clsn_x[other]) * s) / 4096;
    int32_t impulse, spin_a, spin_b;
    if (face < 2) {
        int32_t arm = cz - rz;
        int32_t d = u2 - u1;
        impulse = d + (arm * back_b - cz * back_a) / 0x28c;
        if ((impulse ^ rx) < 0)
            return;
        spin_a = back_a + cz * (impulse / 512);
        spin_b = back_b - arm * impulse / 512;
        int32_t k = (arm * spin_b - cz * spin_a) / 0x28c + impulse / 4;
        int32_t u2k = u2 + k;
        u2 = (u2 + (u1 - u2) / 3 + (u1 - k)) / 2;
        u1 = (u1 + d / 3 + u2k) / 2;
    } else {
        int32_t arm = cx - rx;
        impulse = (w2 - w1) + (-arm * back_b + cx * back_a) / 0x28c;
        if ((rz ^ impulse) < 0)
            return;
        spin_b = back_b + arm * (impulse / 512);
        spin_a = back_a - cx * (impulse / 512);
        int32_t k = (-arm * spin_b + cx * spin_a) / 0x28c + impulse / 4;
        int32_t old_w1 = w1;
        w1 = (w1 + (w2 - w1) / 3 + w2 + k) / 2;
        w2 = (w2 + (old_w1 - w2) / 3 + (old_w1 - k)) / 2;
    }
    if (abs(impulse) > 0x20000) {
        corner_hit(b, impulse, corner);
        switch (face) {
        case 0: side_hit(a, impulse, 0, 1, 0); break;
        case 1: side_hit(a, impulse, 1, 0, 2); break;
        case 2: end_hit(a, impulse, 1, 1); break;
        default: end_hit(a, impulse, 0, 1); break;
        }
        crash_debris(a, b, impulse, cx, cz, c, s);
        Allocate_Sound_Effect(SAMPLE_CAR_COLLISION, CAR_SOUND_VOLUME, car_states[a].position);
    }
    step_cars(a, b, back_a, back_b, t, -1);
    ca->angular_velocity[1] = -spin_a;
    set_velocity(ca, u1, w1, c, s);
    cb->angular_velocity[1] = -spin_b;
    set_velocity(cb, u2, w2, c, s);
    step_cars(a, b, spin_a, spin_b, t, 1);
    car_collisions++;
    sync_state(a);
    sync_state(b);
}

/* Which corners of car `which` (bits 1, 2, 4, 8) are inside the other car's body, and where (in
 * `out`, x and z per corner). */
static unsigned Check_2D_Car_Collision(int32_t *out, int which)
{
    int other = which ^ 1;
    int32_t rel = clsn_yaw[which] - clsn_yaw[other];
    int32_t c = rcos(rel), s = rsin(rel);
    int32_t hc = rcos(clsn_yaw[which]), hs = rsin(clsn_yaw[which]);
    int32_t dx = clsn_x[which] - clsn_x[other], dz = clsn_z[which] - clsn_z[other];
    int32_t px = dx * hc - dz * hs;
    int32_t pz = dx * hs + dz * hc;
    static const int8_t corner_signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    unsigned hits = 0;
    for (int k = 0; k < 4; k++) {
        int32_t sx = corner_signs[k][0] * HALF_WIDTH, sz = corner_signs[k][1] * HALF_LENGTH;
        int32_t x = c * sx - s * sz + px;
        int32_t z = s * sx + c * sz + pz;
        if (x <= HALF_WIDTH * ONE && x >= -HALF_WIDTH * ONE && z <= HALF_LENGTH * ONE && z >= -HALF_LENGTH * ONE) {
            hits |= 1u << k;
            out[k * 2] = x >> 12;
            out[k * 2 + 1] = z >> 12;
        }
    }
    return hits;
}

/* A spin to score: the car's heading now, the car that caused it. */
static void start_spin(int car, int by)
{
    if (car_blocks[car].wrecked != 0 || pointy_bits[car].timer != 0)
        return;
    PointyRecord *p = &pointy_bits[car];
    p->heading = Get_Car_Angle(car_states[car].angle[1]) & ANGLE_MASK;
    p->timer = 75;
    p->cause = (uint8_t)by;
    p->turn = 0;
    p->flags = 0;
}

/* After a hit: drivers running a script start over, and the hit may score spin points. */
static void after_hit(int a, int b)
{
    if (car_blocks[a].ai_command != NULL)
        car_blocks[a].ai_commands = car_blocks[a].ai_command = ai_unstick_script;
    if (car_blocks[b].ai_command != NULL)
        car_blocks[b].ai_commands = car_blocks[b].ai_command = ai_unstick_script;
    if (car_blocks[a].motion_state != MOTION_STUCK && car_blocks[b].motion_state != MOTION_STUCK) {
        start_spin(a, b);
        start_spin(b, a);
    }
}

/* Two cars on the ground: find the corners inside each other, step back half a frame and bisect twice
 * for the moment of contact, then a corner response if one corner went in, else the head-on one. */
static void Check_Ground_Car_Collision(int a, int b)
{
    const int car[2] = {a, b};
    for (int k = 0; k < 2; k++) {
        clsn_yaw[k] = car_states[car[k]].angle[1];
        clsn_x[k] = car_states[car[k]].position[0];
        clsn_z[k] = car_states[car[k]].position[2];
    }
    unsigned hits = Check_2D_Car_Collision(clsn_corners[0], 0) + Check_2D_Car_Collision(clsn_corners[1], 1) * 0x10;
    if (hits == 0)
        return;
    int32_t t = ONE / 2, step = ONE / 2;
    int16_t spin[2];
    int32_t vx[2], vz[2];
    for (int k = 0; k < 2; k++) {
        const CarBlock *cb = &car_blocks[car[k]];
        spin[k] = (int16_t)(cb->angular_velocity[1] / 8192);
        vx[k] = cb->velocity[0] / 8192;
        vz[k] = cb->velocity[2] / 8192;
        clsn_yaw[k] -= spin[k];
        clsn_x[k] -= vx[k];
        clsn_z[k] -= vz[k];
    }
    for (int i = 0; i < 2; i++) {
        step /= 2;
        for (int k = 0; k < 2; k++) {
            spin[k] = (int16_t)(spin[k] / 2);
            vx[k] /= 2;
            vz[k] /= 2;
        }
        unsigned now = Check_2D_Car_Collision(clsn_corners[0], 0) + Check_2D_Car_Collision(clsn_corners[1], 1) * 0x10;
        int sign = 1;
        if (now != 0) {
            hits = now;
            sign = -1;
            t += step;
        } else {
            t -= step;
        }
        for (int k = 0; k < 2; k++) {
            clsn_yaw[k] += sign * spin[k];
            clsn_x[k] += sign * vx[k];
            clsn_z[k] += sign * vz[k];
        }
    }
    int which, corner;
    switch (hits) {
    case 1: which = 0, corner = 0; break;
    case 2: which = 0, corner = 1; break;
    case 4: which = 0, corner = 2; break;
    case 8: which = 0, corner = 3; break;
    case 0x10: which = 1, corner = 0; break;
    case 0x20: which = 1, corner = 1; break;
    case 0x40: which = 1, corner = 2; break;
    case 0x80: which = 1, corner = 3; break;
    default: which = -1, corner = 0; break;
    }
    if (which < 0)
        Calc_Head_On_Clsn_Dynamics(a, b, t);
    else
        Car_Corner_Collision_Response(car[which], car[which ^ 1], t, -clsn_yaw[which],
                                      clsn_corners[which][corner * 2], clsn_corners[which][corner * 2 + 1], which,
                                      corner);
    after_hit(a, b);
}

/* A car in the air and another: a sphere bounce. */
static void Check_Space_Car_Collision(int a, int b)
{
    CarBlock *ca = &car_blocks[a], *cb = &car_blocks[b];
    VECTOR d = {car_states[a].position[0] - car_states[b].position[0],
                car_states[a].position[1] - car_states[b].position[1],
                car_states[a].position[2] - car_states[b].position[2], 0};
    VECTOR n;
    if (VectorNormal(&d, &n) >= 160000)
        return;
    int32_t closing = ((ca->velocity[0] - cb->velocity[0]) / 4096 * n.vx +
                       (ca->velocity[1] - cb->velocity[1]) / 4096 * n.vy +
                       (ca->velocity[2] - cb->velocity[2]) / 4096 * n.vz) / 4096;
    if (closing >= 0)
        return;
    closing += (closing + 0xf) >> 4;
    int32_t ix = closing * n.vx, iy = closing * n.vy, iz = closing * n.vz;
    ca->velocity[0] -= ix;
    ca->velocity[1] -= iy;
    ca->velocity[2] -= iz;
    cb->velocity[0] += ix;
    cb->velocity[1] += iy;
    cb->velocity[2] += iz;
    after_hit(a, b);
}

/* Every pair of cars whose boxes overlap (the player's car stays out while it is in the pit). */
void Do_Car_Collisions(void)
{
    int16_t box[MAX_CARS][6];
    for (int car = pit_in; car < num_cars; car++) {
        const int32_t *p = car_states[car].position;
        for (int k = 0; k < 3; k++) {
            box[car][k] = (int16_t)((p[k] - 0x236) / 16);
            box[car][k + 3] = (int16_t)((p[k] + 0x236) / 16);
        }
    }
    for (int a = pit_in; a < num_cars - 1; a++) {
        for (int b = a + 1; b < num_cars; b++) {
            if (box[a][0] < box[b][3] && box[b][0] < box[a][3] && box[a][2] < box[b][5] && box[b][2] < box[a][5] &&
                box[a][1] < box[b][4] && box[b][1] < box[a][4]) {
                if (car_blocks[a].motion_state == MOTION_AIRBORNE || car_blocks[b].motion_state == MOTION_AIRBORNE)
                    Check_Space_Car_Collision(a, b);
                else
                    Check_Ground_Car_Collision(a, b);
            }
        }
    }
}
