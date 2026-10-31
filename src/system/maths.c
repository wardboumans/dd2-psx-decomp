#include "system/maths.h"

#include "psx/libgte.h"

/* Octant by octant: the table gives atan(small / large) for a ratio scaled to 0..ATAN_STEPS, with
 * the original's rounding (half the divisor added before the divide). */
int Calc_Angle(int a, int b)
{
    if (b == 0 && a == 0)
        return 0;
    if (a < 0) {
        int na = -a;
        if (b < 1) {
            int nb = -b;
            if (na < nb)
                return atan_table[-(a * ATAN_STEPS + b / 2) / nb] + HALF_TURN;
            return HALF_TURN + QUARTER_TURN - atan_table[-(b * ATAN_STEPS + a / 2) / na];
        }
        if (na < b)
            return TURN - atan_table[(b / 2 - a * ATAN_STEPS) / b];
        return atan_table[(b * ATAN_STEPS - a / 2) / na] + HALF_TURN + QUARTER_TURN;
    }
    if (b < 1) {
        int nb = -b;
        if (a < nb)
            return HALF_TURN - atan_table[(a * ATAN_STEPS - b / 2) / nb];
        return atan_table[(a / 2 - b * ATAN_STEPS) / a] + QUARTER_TURN;
    }
    if (a < b)
        return atan_table[(a * ATAN_STEPS + b / 2) / b];
    return QUARTER_TURN - atan_table[(b * ATAN_STEPS + a / 2) / a];
}

/* Arcsine by bisection over a quarter turn: the angle whose sine is `s` (4096 = 1). */
int Calc_Arc_Sin(int s)
{
    int angle = 0x200;
    int step = 0x100;
    for (int i = 9; i > 0; i--) {
        angle += rsin(angle >> 1) < s ? step : -step;
        step >>= 1;
    }
    return angle >> 1;
}
