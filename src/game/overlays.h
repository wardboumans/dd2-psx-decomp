/* The race overlays: start lights, speed, laps, positions, timers, damage, the pit and replay panels. */
#ifndef GAME_OVERLAYS_H
#define GAME_OVERLAYS_H

#include "psx/libgpu.h"

#include <stdint.h>

/* One overlay sprite: its quad in each display buffer (the original keeps them as globals). */
typedef struct {
    POLY_FT4 q[2];
} HudSprite;

enum {
    HUD_LIGHT_SURROUND,             /* 0x80079C94 */
    HUD_GREEN_LIGHT_1, HUD_GREEN_LIGHT_2, HUD_GREEN_DIM_1, HUD_GREEN_DIM_2, /* 0x80079CE4 */
    HUD_RED_LIGHT_1, HUD_RED_LIGHT_2, HUD_RED_DIM_1, HUD_RED_DIM_2,         /* 0x80079E24 */
    HUD_1ST, HUD_2ND, HUD_3RD,      /* 0x80079F64 */
    HUD_DEMO,                       /* 0x8007A054 */
    HUD_SPEED, HUD_STILL_RUNNING, HUD_RACE_POINTS, HUD_LAP, /* 0x8007A0A4 */
    HUD_POSITION_1, HUD_POSITION_2, /* 0x8007A1E4 */
    HUD_SPEED_DIGIT_1, HUD_SPEED_DIGIT_2, HUD_SPEED_DIGIT_3, HUD_KPH, /* 0x8007A284 */
    HUD_CARS_LEFT_1, HUD_CARS_LEFT_2,       /* 0x8007A3C4 */
    HUD_POINTS_DIGIT_1, HUD_POINTS_DIGIT_2, HUD_POINTS_DIGIT_3, /* 0x8007A464 */
    HUD_LAP_1, HUD_LAP_2,       /* 0x8007A554 */
    HUD_LAP_SLASH, HUD_LAPS_TENS, HUD_LAPS_UNITS, /* 0x8007A5F4 */
    HUD_CAR, HUD_DAMAGE_TOP, HUD_DAMAGE_MIDDLE, HUD_DAMAGE_BOTTOM, /* 0x8007A6E4 */
    HUD_TIME_DIGITS,                /* 0x8007A824, 6 */
    HUD_FAST_DIGITS = HUD_TIME_DIGITS + 6, /* 0x8007AA04, 6 */
    HUD_POSITION_BIG = HUD_FAST_DIGITS + 6, /* 0x8007ABE4 */
    HUD_TOTAL_DIGITS,               /* 0x8007AC34, 6 */
    HUD_LAP_TIME = HUD_TOTAL_DIGITS + 6, HUD_FAST_LAP, /* 0x8007AE14 */
    HUD_EXIT_PIT, HUD_REPAIR, HUD_SPIN_LEFT, /* 0x8007AEB4 */
    HUD_PAD_X, HUD_PAD_NEG_I, HUD_PAD_MAD_I, HUD_PAD_SQUARE, HUD_PAD_NEG_II, HUD_PAD_MAD_II, HUD_SIDE_ONLY, /* 0x8007AFA4 */
    HUD_REPLAY_BUTTON_1, HUD_REPLAY_SLOW_MOTION, HUD_REPLAY_BUTTON_2, HUD_REPLAY_EXIT, HUD_REPLAY_R, /* 0x8007B2D4: buttons by pad type */
    HUD_COUNT
};

extern HudSprite hud_sprites[HUD_COUNT];
extern POLY_F4 pit_panel[2];         /* 0x8007B1D4: the dark box behind the pit menu */
extern POLY_FT4 pit_panel_mode[2];   /* 0x8007B204: sets the blend mode for it */
extern POLY_F4 replay_panel[2];      /* 0x8007B254 */
extern POLY_FT4 replay_panel_mode[2]; /* 0x8007B284 */
extern int32_t derby_time[3];        /* 0x80089310: total destruction clock: minutes, seconds, fraction (0x10000 a second) */

extern int32_t pit_countdown;        /* 0x80079720: seconds left in the pit */
extern int32_t pit_second;           /* 0x8007971C: fraction of the pit countdown's second (0x10000) */

void Init_Overlays(void);            /* 0x80020C6C */
void Update_Race_Timers(int car);    /* 0x80021A78 */
void Update_Race_CountDown(void);    /* 0x80024430 */
void Display_Position_Pointers(void); /* 0x800248A0 */

#endif
