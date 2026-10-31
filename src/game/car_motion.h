/* Car motion and the race bookkeeping kept with it: the grid, laps and positions. */
#ifndef GAME_CAR_MOTION_H
#define GAME_CAR_MOTION_H

#include "game/track.h"
#include "system/controller.h"

#include <stdint.h>

/* A car's race progress (0x14 bytes). */
typedef struct {
    int16_t speed;           /* +0x00: car 0 only, the speedometer */
    int16_t position;        /* +0x02: place in the race, 1 first */
    int16_t final_position;  /* +0x04: the place it finished in, 21 until then */
    int16_t points;          /* +0x06: points scored (crashes and places) */
    int16_t lap;             /* +0x08: laps completed */
    int16_t lap_strip;       /* +0x0A: furthest strip reached on this lap (counting from the line) */
    int16_t lap_counted;     /* +0x0C: the lap the positions use; lags `lap` while the car is behind the line */
    int16_t strip;           /* +0x0E: strip it is on, counting from the line */
    int16_t wrecks;          /* +0x10: cars it wrecked */
    int16_t finished;        /* +0x12 */
} RaceRecord;

_Static_assert(sizeof(RaceRecord) == 0x14, "RaceRecord");

/* Per level: where the line is, how many strips a lap has, how many laps a race has. */
typedef struct {
    int16_t start_strip;
    int16_t lap_strips;      /* Init_Track_Strip_Numbers counts it on levels with forks */
    int16_t laps;
} TrackInfo;

#define RACE_RECORDS 20
#define CAR_WHEELS 4

extern RaceRecord race_records[RACE_RECORDS];        /* 0x8009568C */
extern TrackInfo track_info[12];                     /* 0x80070D30 */
extern TrackFd wheel_fd[RACE_RECORDS][CAR_WHEELS];   /* 0x80089494: where each wheel is on the track */
extern int32_t stuck_timer[RACE_RECORDS];            /* 0x800A71CC */
extern int32_t race_minutes, race_seconds, race_fraction; /* 0x8009EC88: lap clock; fraction 0x10000 a second */
extern int32_t lap_minutes, lap_seconds, lap_fraction;    /* 0x8009EBDC: the time shown */
extern int32_t timing_lap;                           /* 0x80079BE0: car 0's lap being timed */
extern int32_t lap_record_flash;                     /* 0x80079AF0 */
extern int32_t lap_time_hold;                        /* 0x80079ADC: frames the last lap time stays shown */
extern int32_t skid_sound_wait;                      /* 0x80079C14 */
extern int32_t active_cars;                          /* 0x8007968C */

void Place_Cars_On_Grid(void);          /* 0x8003F1D8 */
void Init_End_Race(void);               /* 0x8003FE10 */
void Calc_Track_Positions(void);        /* 0x8003FE3C */
void Init_Race_Records(void);           /* 0x800402F0 */
void Get_Race_Positions(void);          /* 0x800403D4 */
void Init_Track_Strip_Numbers(void);    /* 0x80040710 */
void Init_Car_Shadows(void);            /* 0x8004097C */
void Draw_Car_Shadow(int car);          /* 0x80040AB0 */

extern TrackFd corner_fd[8];             /* 0x800A6808: the body corners of the car being moved */
extern int32_t corner_pos[8][4];         /* 0x8008485C */
extern int32_t corner_clearance[8];      /* 0x80080044 */

void Car_Movement(int car);              /* 0x8003ED30 */
void Control_Player_Car_From_Pad(int car, const PadState *pad); /* 0x8003CAC8 */
void Recover_Stuck_Cars(void);           /* 0x8003BFE8 */
void Boot_Lost_Geometry(int car);        /* 0x80041A8C */
void Detach_Damaged_Wheel(int car, int wheel); /* 0x80041DF0 */
void Check_Bonnet_Removal(int car);      /* 0x80042480 */
void Check_Boot_Removal(int car);        /* 0x80042574 */
void Get_Wheel_Positions(int car);       /* 0x8003C1EC */
void Get_Corner_Positions(int car);      /* 0x8003721C */
void Car_Landed(int car);                /* 0x80037F18 */
void Car_Corner_Impact(int car, int corner); /* 0x800377DC */
void Calc_Car_Angles_From_Wheels(int car); /* 0x8003C51C */
int Calc_Car_Tilt(int car, int axis);    /* 0x8003C71C */
void Calc_Car_Angles_Square(int car, const TrackFd *p0, const TrackFd *p1, const TrackFd *p2, const TrackFd *p3,
                            int16_t pitch, int16_t roll); /* 0x8003C920 */
void Integrate_Car_Position(int car);    /* 0x8003EB74 */
void Car_Drive_Motion(int car);          /* 0x8003CAE8 */
void Car_Drive_2pt_Motion(int car);      /* 0x8003DDD0 */
void Car_Drive_1pt_Motion(int car);      /* 0x8003E724 */
void Car_Drive_Motion_3D_(int car);      /* 0x8003BC9C */
void Car_Fly_Motion_3D(int car);         /* 0x8003821C */
void Car_Grounded_Motion_3D(int car);    /* 0x80038660 */
void Car_Side_Roll_Motion(int car);      /* 0x80038B44 */
void Car_2pt_Motion_3D(int car);         /* 0x80039618 */
void Car_1pt_Motion_3D(int car);         /* 0x8003B090 */
void Calc_Suspension_All_Wheels(int car);   /* 0x800412A8 */
void Calc_Suspension_Left_Wheels(int car);  /* 0x8004148C */
void Calc_Suspension_Right_Wheels(int car); /* 0x800416E8 */
void Calc_Null_Suspension(int car);      /* 0x80041944 */
void Calc_Extended_Suspension(int car);  /* 0x800419E0 */

#endif
