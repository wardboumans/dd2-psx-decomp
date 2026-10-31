/* Level objects set up with the cars: Wild Bill and the rollercoaster train. */
#ifndef GAME_CAR_INIT_H
#define GAME_CAR_INIT_H

#include "graphics/object.h"

#include <stdint.h>

/* The car models in the level data. */
#define SECTION_CAR_FAR 15
#define SECTION_CAR_NEAR 16
#define SECTION_PLAYER_CAR 17

#define ROLLER_CARS 12

/* A rollercoaster car (0x50 bytes), drawn by Draw_Other_Objects. */
typedef struct {
    int32_t position[4];   /* +0x00 */
    int32_t point[2][4];   /* +0x10: the track points it is between */
    int16_t angle[4];      /* +0x30 */
    int32_t point_index;   /* +0x38: of point[0] in the track */
    int32_t step;          /* +0x3C: frames since point[0] (6 to the next) */
    int32_t t;             /* +0x40: 0x1000 at point[1] */
    int32_t ready;         /* +0x44: running (else waiting out `delay`) */
    int32_t delay;         /* +0x48: frames to wait; at the start its place in the train */
    int32_t restart_point;  /* +0x4C: the point the train starts its lap from (15 or 0; -1 at first); never read */
} RollerCar;

_Static_assert(sizeof(RollerCar) == 0x50, "RollerCar");

extern Object wild_bill_objects[2];          /* 0x800848DC, 0x8008C464 */
extern RollerCar roller_cars[ROLLER_CARS];   /* 0x80088608 */
extern Object roller_objects[ROLLER_CARS];   /* 0x8009EA3C */

#endif
