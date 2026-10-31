/* Car paint: each car's colour lookup tables and door textures, applied to the packets of its models. */
#ifndef GAME_CAR_CLUTS_H
#define GAME_CAR_CLUTS_H

#include "graphics/sprite.h"

#include <stdint.h>

/* A car's paint (0x58 bytes). */
typedef struct {
    uint16_t clut[8];   /* body B, A, D, C, E, smoke B, smoke A, door */
    Sprite door[3];     /* door textures, by damage */
} CarPaint;

_Static_assert(sizeof(CarPaint) == 0x58, "CarPaint");

/* Where a body part's damage stage is in VRAM (6 bytes). */
typedef struct {
    uint16_t tpage;
    uint8_t u, v;
    uint16_t clut;      /* only the bonnet and boot stages use it */
} PartTexture;

/* The damage stages of the body parts (0x8007BBC4). */
typedef struct {
    PartTexture front[5];
    PartTexture front_window[5];
    PartTexture back_window[5];
    PartTexture bumper[5];
    PartTexture bonnet[4];
    PartTexture roof[3];
    PartTexture boot[4];
} PartTextures;

extern CarPaint car_paint[20];          /* 0x8007B4E4 */
extern PartTextures part_textures;      /* 0x8007BBC4 */
extern uint8_t door_state[20][2];
/* The texture number of each car's paint (the CLUTnn / DRnn sprite names). */
extern const uint8_t car_textures[20];  /* 0x80070948 */       /* 0x8007095C: door texture shown per car and side */

void Init_Car_Cluts(void);                                          /* 0x80033B34 */
void Init_Car_Doors(void);                                          /* 0x80034608 */
void Apply_Car_Part_Cluts(int car, int paint, int model);           /* 0x80034AB4 */
void Change_Bonnet_Clut(int paint, int bonnet);                     /* 0x80034C84 */
void Change_Boot_Clut(int paint, int boot);                         /* 0x80034E64 */
void Highlight_Area(int car, int area, uint8_t level);              /* 0x80034FB4 */
void Set_Car_Door_Texture(int car, int paint, int model, int door, int state); /* 0x8003533C */
void TextureDentHiCar(int car, int region, int from, int to);        /* 0x80035568 */
void TextureDentMidCar(int car, int region, int from, int to);       /* 0x80035FD8 */

#endif
