/* Denting: damage reshapes the car models and swaps their textures for damaged ones. */
#ifndef GAME_DENTING_H
#define GAME_DENTING_H

#include "game/car.h"

#include <stdint.h>

#define HI_CAR_VERTICES 107 /* the player's detailed model */

extern uint8_t dent_levels[MAX_CARS][12]; /* 0x800890D0: each region's texture damage stage */

void InitialiseDenting(void);                                                   /* 0x80014F68 */
void Dent_Car_Zone(Object *model, CarBlock *cb, int zone, int parts, int car); /* 0x80015228 */

#endif
