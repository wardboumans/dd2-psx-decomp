#ifndef GAME_PLAY_GAME_H
#define GAME_PLAY_GAME_H

#include "game/track.h"

#include <stdint.h>

#define OVERLAY_HOLD_START 0xa000 /* the start fly-in (Update_Chase_Camera) */

/* The race clocks count seconds in 16.16: a frame is a 25th of a second. */
#define CLOCK_SECOND 0x10000
#define CLOCK_TICK 0xa3d

extern int32_t camera_car;          /* 0x80079164: the car the camera follows */
extern int32_t camera_mode;         /* 0x8007916C */
extern TrackFd *camera_fd_pt;       /* 0x80079AF8: car_fd entry of camera_car */
extern TrackFd car_fd[24];          /* 0x8008426C: where each car is on the track */
extern int32_t overlay_state;       /* 0x80079AC0: start countdown, 100 at the start; the race runs below 1 */
extern int32_t overlay_hold;        /* 0x80079AEC */
extern int32_t camera_tilt;         /* 0x80079168 */
extern int32_t quit_flag;           /* 0x80079C2C */
extern int32_t yes_quit;            /* 0x80079BD8: Play_Game's result, 1 when the race was abandoned */
extern int32_t replay_playback;     /* 0x80079734 */
extern int32_t pit_in;              /* 0x8007970C: the player's car is driven into the pit */
extern int32_t current_player_car;  /* 0x80079BEC */
extern int32_t race_timers_on;      /* 0x8007912C */
extern int32_t cars_finished;       /* 0x80079B7C */

int Play_Game(void); /* 0x8001174C */

#endif
