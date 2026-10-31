/* Car wheels: the player's wheel objects and every car's hub-cap overlays. */
#ifndef GAME_WHEELS_H
#define GAME_WHEELS_H

#include "graphics/object.h"
#include "graphics/sprite.h"

extern Object wheel_objects[4];   /* 0x8008C75C: the player's wheels */
extern Sprite wheel_sprites[3];   /* 0x80070264: WCO, WHEEL2, WHEEL3 */

void TransformWheels_(int car, int first, int second); /* 0x8002BD6C */
void TransformEnemyWheels(int car);                     /* 0x8002C01C */
void ApplyWheelOverlay(int car, int wheel);             /* 0x8002C150 */

/* collision.c */
void Init_Wheel_Objects(void);                          /* 0x8002B874 */
void Init_Car_Wheel_Overlays(int car);                  /* 0x8002B92C */

#endif
