/* Collisions between cars: overlapping boxes, then corner-in-body or body-to-body responses that
 * share the impulse, spin the cars, damage, dent and knock them over, and score spins. */
#ifndef GAME_CAR_COLLISION_H
#define GAME_CAR_COLLISION_H

#include <stdint.h>

extern int32_t car_collisions; /* 0x80079AE8: count of car-to-car hits */

void Do_Car_Collisions(void);                                      /* 0x800337F8 */
void Calc_Head_On_Clsn_Dynamics(int a, int b, int32_t t);          /* 0x800308CC */
void Car_Corner_Collision_Response(int a, int b, int32_t t, int32_t angle, int32_t cx, int32_t cz, int which,
                                   int corner);                    /* 0x8002EC80 */

#endif
