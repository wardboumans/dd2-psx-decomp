#ifndef SYSTEM_MATHS_H
#define SYSTEM_MATHS_H

#include <stdint.h>

/* Angles are 4096 to a turn, the units the GTE's rsin/rcos take. */
#define TURN 0x1000
#define HALF_TURN 0x800
#define QUARTER_TURN 0x400
#define ANGLE_MASK 0xfff /* wraps an angle into one turn */

/* An angle of one turn (0 .. ANGLE_MASK) as -HALF_TURN .. HALF_TURN - 1. */
static inline int32_t signed_angle(int32_t a)
{
    return a >= HALF_TURN ? a - TURN : a;
}

#define ATAN_STEPS 0x400 /* atan_table: atan(r / ATAN_STEPS) for r = 0 .. ATAN_STEPS */
extern const int16_t atan_table[ATAN_STEPS + 1]; /* 0x80071840 */

/* The angle (4096 to a turn) of the vector (a, b), measured from +b towards +a. */
int Calc_Angle(int a, int b); /* 0x800528FC */
int Calc_Arc_Sin(int s);      /* 0x80052884 */

#endif
