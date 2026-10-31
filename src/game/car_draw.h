/* Drawing the cars: lighting, brake lights, smoke, and the damage indicator. */
#ifndef GAME_CAR_DRAW_H
#define GAME_CAR_DRAW_H

void Init_Damage_Indicator(void);        /* 0x8001AAD0 */
void Update_Damage_Indicator(int car);   /* 0x8001AC6C */
void Init_Brake_Lights(void);            /* 0x8001B2B4 */
void Update_Brake_Lights(int car);       /* 0x8001B4A8 */
void Bonnet_Smoke(int car);              /* 0x8001B620 */
void Street_Lighting(int car);           /* 0x8001BF10 */
void Car_Segment_Lighting(int car);      /* 0x8001C5B0 */
void Draw_Car(int car);                  /* 0x8001D234 */
void Draw_Player_Car_Near(int car);      /* 0x8001D3C4 */

/* car_init.c */
void Init_Car_Graphics(void);            /* 0x8001D514 */
void Init_Wild_Bill(void);               /* 0x8001D944 */
void Init_Rollercoaster(void);           /* 0x8001D998 */

#endif
