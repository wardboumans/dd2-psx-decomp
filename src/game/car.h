/* The per-car record the race code shares (car motion, collisions, AI, drawing, sound).
 *
 * Layout from the PSX accesses to car 0 (0x8008A254, stride 0x1B4), naturally aligned, with the body matrix a
 * full MATRIX (rotation and translation). Integers are 20.12 fixed point unless stated otherwise. */
#ifndef GAME_CAR_H
#define GAME_CAR_H

#include "graphics/object.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"

#include <stddef.h>
#include <stdint.h>

#define MAX_CARS 20
#define STEER_LOCK 0x200 /* CarBlock.steering at full lock */
#define MAX_DAMAGE 0x1000 /* a wrecked damage zone */
#define NO_RISE (-0x3fff)

/* CarBlock.motion_state: which of Car_Movement's motions moves the car. */
enum {
    MOTION_DRIVING,         /* on its wheels (Car_Drive_Motion_3D_) */
    MOTION_SIDE_ROLL,       /* rolling over a long side, roll_side 0 / 1 (Car_Side_Roll_Motion) */
    MOTION_TWO_CORNER_ROLL, /* rolling on a pair of corners, roll_side 2..7 (Car_2pt_Motion_3D) */
    MOTION_ONE_CORNER,      /* pivoting on one corner (Car_1pt_Motion_3D) */
    MOTION_AIRBORNE,        /* flying (Car_Fly_Motion_3D) */
    MOTION_STUCK,           /* lying on a side, an end or the roof, by ground_mode (Car_Grounded_Motion_3D) */
};

typedef struct {
    int32_t angular_velocity[4]; /* +0x000: spin rates x (pitch), y (yaw), z (roll), added to angle[]; [3] unused */
    int32_t velocity[4];         /* +0x010: x, y, z; [3] is copied with the vector but unused */
    int32_t angle[4];            /* +0x020: rotation x, y (heading), z; [3] unused */
    int32_t position[4];         /* +0x030: world x, y (height), z; [3] unused */
    int32_t velocity_change[4];  /* +0x040: this tick's drive change of velocity; [3] unused */
    MATRIX matrix;               /* +0x050: body rotation */
    int16_t fly_angles[4];       /* +0x070: tumble angles while airborne (eased back to 0); [3] unused */
    int32_t speed;               /* +0x078: ground speed */
    int32_t forward_speed;       /* +0x07C: signed speed along the heading (< 0 reversing) */
    int32_t ground_slope;        /* +0x080: last track plane normal y (0x1000 flat) */
    int32_t steering;            /* +0x084: -STEER_LOCK .. STEER_LOCK, < 0 left */
    int32_t throttle;            /* +0x088: throttle / brake */
    int32_t ai_speed_offset;     /* +0x08C: AI corner braking, added to the handicap */
    int32_t grip;                /* +0x090 */
    int32_t motion_state;        /* +0x094: MOTION_* */
    int32_t wheel_ground[4];     /* +0x098: last ground height under each wheel */
    int32_t wheel_rise[4];       /* +0x0A8: last rise of each wheel (NO_RISE none) */
    int32_t fly_corner_bias;     /* +0x0B8 */
    int32_t roll_side;           /* +0x0BC: the corner pair the car rolls on (0..7) */
    int32_t ground_mode;         /* +0x0C0: the corners touching the ground */
    int32_t pivot_corner;        /* +0x0C4: the corner a one-corner car pivots on */
    int32_t wrecked;             /* +0x0C8: out of the race */
    int32_t heavy_damage;        /* +0x0CC: front badly damaged (smokes) */
    int32_t suspension[4];       /* +0x0D0 */
    int32_t suspension_velocity[4]; /* +0x0E0 */
    int32_t damage[11];          /* +0x0F0: 0..MAX_DAMAGE per zone */
    int32_t fire_timer;          /* +0x11C */
    int32_t part_loss_timer;     /* +0x120: frames since a body part came off (100, counts down) */
    int32_t steer_bias;          /* +0x124: steering pull from lost wheels */
    int32_t steer_factor;        /* +0x128 */
    int32_t drive_factor;        /* +0x12C: 0x1000 with all wheels */
    int32_t wheel_lost[4];       /* +0x130 */
    int32_t wheels_lost;         /* +0x140 */
    int32_t bonnet_lost;         /* +0x144 */
    int32_t boot_lost;           /* +0x148 */
    int32_t wheel_mode[4];       /* +0x14C: 0 sprung, 1 dropping off, 2 gone */
    int32_t car_type;            /* +0x15C */
    int32_t brightness;          /* +0x160: lighting level (its low byte colours the shadow) */
    int32_t ground_timer;        /* +0x164 */
    const int32_t *ai_command;   /* +0x168: AI command script cursor (game/ai.c) */
    const int32_t *ai_commands;  /* +0x16C: the script being run */
    int32_t ai_wait_state;       /* +0x170 */
    int32_t ai_wait_count;       /* +0x174 */
    int32_t ai_aim_timer;        /* +0x178 */
    int32_t ai_corner_speed;     /* +0x17C */
    int32_t ai_corner_type;      /* +0x180 */
    int32_t ai_heading_diff;     /* +0x184 */
    int32_t ai_off_line;         /* +0x188 */
    int32_t ai_bowl_target;      /* +0x18C */
    int32_t ai_corner_index;     /* +0x190 */
    int32_t ai_speed_limit;      /* +0x194 */
    int32_t unused_198;          /* +0x198: nothing in the PSX code touches it */
    int32_t ai_handicap;         /* +0x19C */
    int32_t ai_swerving;         /* +0x1A0 */
    int32_t lane;                /* +0x1A4 */
    int32_t ai_swerve_cooldown;  /* +0x1A8 */
    int32_t ai_personality;      /* +0x1AC */
    uint8_t barrier_hit;         /* +0x1B0 */
    uint8_t ai_timer;            /* +0x1B1 */
    uint8_t ai_route;            /* +0x1B2 */
    uint8_t ai_prev_route;       /* +0x1B3 */
} CarBlock;

/* The offsets are the PSX ones only with 4-byte pointers; on 64-bit hosts the two AI pointers grow the record,
 * which nothing depends on (it is runtime data, never read from a file). */
#define CAR_OFFSET_CHECK(field, off) _Static_assert(offsetof(CarBlock, field) == (off), "CarBlock." #field)
CAR_OFFSET_CHECK(velocity, 0x10);
CAR_OFFSET_CHECK(angle, 0x20);
CAR_OFFSET_CHECK(position, 0x30);
CAR_OFFSET_CHECK(matrix, 0x50);
CAR_OFFSET_CHECK(fly_angles, 0x70);
CAR_OFFSET_CHECK(speed, 0x78);
CAR_OFFSET_CHECK(motion_state, 0x94);
CAR_OFFSET_CHECK(damage, 0xf0);
CAR_OFFSET_CHECK(part_loss_timer, 0x120);
CAR_OFFSET_CHECK(wheel_mode, 0x14c);
CAR_OFFSET_CHECK(brightness, 0x160);
CAR_OFFSET_CHECK(ai_command, 0x168);
#undef CAR_OFFSET_CHECK

extern CarBlock car_blocks[MAX_CARS]; /* 0x8008A254 */

/* One corner of a wheel's overlay quad. */
typedef struct {
    int16_t x;           /* set by Init_Car_Wheel_Overlays */
    int16_t y, z;        /* from the spin table at the wheel's spin angle */
    int16_t pad;         /* never accessed */
} CarWheelCorner;

/* A wheel of the car state (0x84 bytes in the original). */
typedef struct {
    int16_t centre[4];        /* +0x00: wheel centre in car space (y follows the suspension); [3] only copied */
    int16_t angle[4];         /* +0x08: x, y (steer), z; [3] only copied */
    POLY_FT4 packets[2];      /* +0x10: the overlay quad per display buffer (0x50 bytes in the original) */
    CarWheelCorner corner[4]; /* +0x60 */
    int16_t spin;             /* +0x80: spin angle, the spin-table index of the overlay */
} CarWheelState;

/* The per-car state the drawing and motion code share (0x288 bytes per car from 0x80091018 in the original;
 * runtime data, so the packets are native and the offsets only hold on the PSX). */
typedef struct {
    CarWheelState wheel[4];   /* +0x000 */
    int32_t unused_210[7];    /* +0x210: nothing in the PSX code touches these */
    int32_t position[4];      /* +0x22C: world x, y, z and a word copied with them */
    MATRIX matrix;            /* +0x23C: body rotation */
    MATRIX previous_matrix;   /* +0x25C: copy taken by Make_Car_Fly */
    int16_t angle[4];         /* +0x27C: x (pitch), y (heading), z (roll); [3] only copied */
    int32_t drawn;            /* +0x284: the last draw put the car on screen */
} CarState;

extern CarState car_states[MAX_CARS]; /* 0x80091018 */

#define CAR_MODEL_VERTICES 102 /* the near model's vertices; each car dents its own copy */

/* [0] the distant model (the player's car: its own detailed model), [1] the near model, drawn with each
 * car's own dented vertices. */
extern Object car_objects[MAX_CARS][2];            /* 0x8007EE38 */
extern SVECTOR car_vertices[MAX_CARS][CAR_MODEL_VERTICES]; /* 0x8008C7E4 */
extern MATRIX car_colour_matrix[MAX_CARS];         /* 0x80090D0C */
extern MATRIX car_light_matrix;                    /* 0x8006EAD8 */

#endif
