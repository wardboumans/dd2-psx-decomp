/* Debris and flying parts: the bits of body that crashes throw out (spinning triangles) and the wheels,
 * bonnets and boots that come off and bounce down the track. */
#ifndef GAME_DEBRIS_H
#define GAME_DEBRIS_H

#include "game/track.h"
#include "graphics/object.h"

#include <stdint.h>

#define FLYING_PARTS 8 /* models of each kind of part (wheel, bonnet, boot) */

/* A part that came off a car (0x9C bytes in the original). */
typedef struct {
    Object object;              /* +0x00: a copy of one of the part's models */
    TrackFd fd;                 /* +0x1C: where it is on the track */
    int32_t position[4];        /* +0x48 */
    int32_t velocity[4];        /* +0x58: per frame, not 20.12 */
    int32_t bounce_velocity[4]; /* +0x68: the velocity it set off with, then after each bounce */
    int16_t angle[4];           /* +0x78 */
    int16_t spin[4];            /* +0x80: added to angle[] each frame */
    int32_t timer;              /* +0x88: frames left to live */
    int32_t active;             /* +0x8C */
    int32_t unused_90;          /* +0x90: only ever cleared */
    int32_t type;               /* +0x94: FLYING_WHEEL, _BONNET, _BOOT, -1 free */
    int32_t settled;            /* +0x98: it has come to rest (its angles snap flat the next frame) */
} FlyingObject;

#define FLYING_WHEEL 0
#define FLYING_BONNET 1
#define FLYING_BOOT 2

extern Object wheel_part_objects[FLYING_PARTS]; /* 0x8007FDBC */
extern Object bonnet_objects[FLYING_PARTS];     /* 0x800891C0 */
extern Object boot_objects[FLYING_PARTS];       /* 0x800A703C */
extern int32_t bonnet_index;                    /* 0x80079A64: the bonnet model used last */
extern int32_t boot_index;                      /* 0x80079BB4 */

void Init_Debris(void);                                                                /* 0x80011E68 */
void Setup_Debris(const int32_t where[3], const int32_t velocity[3], int count, int car); /* 0x800125B0 */
void Update_Debris(void);                                                              /* 0x8001277C */
void Init_Flying_Objects(void);                                                        /* 0x80012D78 */
void Setup_Flying_Objects(int type, const int32_t velocity[3], int wheel, int car);    /* 0x80012FB4 */
void Update_Flying_Objects(void);                                                      /* 0x80013C1C */
int Request_Flying_Object(int type);                                                   /* 0x80013F08 */

#endif
