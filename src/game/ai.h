/* The computer drivers: on tracks they follow the strip headings and lanes, swerve round slower cars,
 * take corners by the strip markings and may bully the player; in the bowls they pick a car and ram it. */
#ifndef GAME_AI_H
#define GAME_AI_H

#include "game/car.h"

#include <stdint.h>

/* A spin a car took from a hit (0x0C bytes per car): while `timer` runs, its turning scores points for
 * the car that caused it (CheckPointScoring). */
typedef struct {
    int32_t heading;  /* +0x00: the heading when it was hit */
    int32_t turn;     /* +0x04: how far it has turned since */
    uint16_t flags;   /* +0x08: POINTY_* (read and written as 16 bits) */
    uint8_t timer;    /* +0x0A */
    uint8_t cause;    /* +0x0B: the car that hit it */
} PointyRecord;

_Static_assert(sizeof(PointyRecord) == 0xc, "PointyRecord");

#define POINTY_QUARTER_TURN 0x20 /* it has turned a quarter turn: 10 points when the timer runs out */
#define POINTY_HALF_TURN 0x40    /* half a turn: 25 points */

extern PointyRecord pointy_bits[20]; /* 0x8007F780 */
extern int32_t ai_target_car;        /* 0x80079A90: the car (if any) already crowding the player */
extern int32_t ai_segment;           /* 0x80079AA8: the quad across the strip of the car being driven */

extern const int32_t ai_unstick_script[6]; /* 0x8006F8F0: the script a stuck or knocked driver starts with */

int32_t Get_Car_Angle(int32_t a);                          /* 0x800287A4 */
void Get_Direction_Cosines(int32_t *angle, int32_t *cosine); /* 0x800287D0 */
void Interpolate_Direction_Vectors_Left(CarBlock *cb, int32_t angle, int32_t step);  /* 0x8002886C */
void Interpolate_Direction_Vectors_Right(CarBlock *cb, int32_t angle, int32_t step); /* 0x800288FC */
void InitialiseAI(int car);          /* 0x8002898C */
void AI_Update_Strip_Info(void);     /* 0x800297BC */
void AI_Com_Server(int car);         /* 0x80026750 */

#endif
