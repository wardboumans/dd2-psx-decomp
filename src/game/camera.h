/* The race cameras: bumper and chase (Update_Chase_Camera in track.c), the pit camera that circles the
 * car while it is repaired, and the floaty camera of replays and the attract mode. */
#ifndef GAME_CAMERA_H
#define GAME_CAMERA_H

#include "game/track.h"

#include <stdint.h>

/* The chase camera's height and distance behind the car, and how high above it it looks. */
#define CHASE_HEIGHT 0xf0
#define CHASE_DISTANCE 0x400
#define LOOK_HEIGHT 0xfa

extern int32_t chase_angle;          /* 0x80079170: heading of the chase camera, 20.12 */
extern int32_t chase_height;         /* 0x80079174 */
extern int32_t chase_distance;       /* 0x80079178 */
extern int32_t chase_angle_offset;   /* 0x8007917C: added to the heading (the pit camera circles with it) */
extern int32_t look_height;          /* 0x80079180: the camera looks this far above the car */
extern int32_t look_back;            /* 0x80079184: the car has reversed for a while: look the other way */
extern int32_t look_back_count;      /* 0x80079188 */
extern int32_t chase_far;            /* 0x80079ACC: bowls, the camera is near the wall */
extern int32_t camera_position[4];   /* 0x8008010C */
extern TrackFd camera_fd;            /* 0x800A2DD8 */
extern int32_t camera_ground;        /* 0x80079B1C: ground height under the chase camera */
extern TrackFd floaty_fd;            /* 0x8008858C */

extern int32_t pit_camera_active;    /* 0x80079714 */
extern int32_t pit_camera_state;     /* 0x80079724: 0 entering, 1 running, 2 leaving */
extern int32_t pit_camera_entered;   /* 0x8007919C */
extern int32_t old_camera_mode;      /* 0x8007918C */
extern int32_t pit_section;          /* 0x80079190: car section the pit camera shows (0..5) */
extern int32_t pit_bounce;           /* 0x80079194 */
extern int32_t pit_turns;            /* 0x80079198: whole turns of chase_angle_offset */
extern int32_t pit_camera_done;      /* 0x80079718 */
extern int32_t pit_camera_busy;      /* 0x80079728 */

void Car_Camera(int car);                  /* 0x80019810 */
void Camera_Pad_Control(int car);          /* 0x80019A04 */
void Init_Pit_Camera(void);                /* 0x80019B08 */
void Exit_Pit_Camera(void);                /* 0x80019B64 */
void Pit_Camera_Control(int car);          /* 0x80019BF0 */
void Init_The_Floaty_Camera(int car);      /* 0x8001A230 */
int Do_The_Floaty_Camera_Thing(const int32_t *rails, int count); /* 0x8001A2BC */

void Reset_Car_Camera(int car);            /* 0x800185F8, track.c */
void Update_Chase_Camera(int car);         /* 0x80018690, track.c */

#endif
