/* Collisions: the barriers (wheels and body corners against the track edges or the bowl wall). */
#ifndef GAME_COLLISION_H
#define GAME_COLLISION_H

#include "psx/libgte.h"

#include <stdint.h>

extern int32_t barrier_remark_wait; /* 0x80079BCC */
extern int32_t commentary_busy;     /* 0x80079B4C */

int Barrier_Collision(int wheel, int car);         /* 0x80029E8C */
int Barrier_Corner_Collision(int corner, int car); /* 0x8002B0A4 */
void Spark_At(const VECTOR *pos, int car);        /* one spark flying with the car */
void CheckPointScoring(void);                      /* 0x80029A74 */

#endif
