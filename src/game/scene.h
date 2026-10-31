/* The scenery: blocks of objects streamed in along the track, and the sky. */
#ifndef GAME_SCENE_H
#define GAME_SCENE_H

#include "graphics/object.h"
#include "psx/libgte.h"

#include <stdint.h>

#define SCENE_BLOCKS 14       /* blocks resident at once */
#define SCENE_BLOCK_SIZE 0x4000
#define BLOCK_OBJECTS 32

/* scene_block_id values besides a block number */
#define BLOCK_FREE -1
#define BLOCK_REMOVE -2       /* its objects are freed next frame */
#define BLOCK_LOADING -3

extern uint8_t *scene_block_data[SCENE_BLOCKS];              /* 0x80080064 */
extern int32_t scene_block_id[SCENE_BLOCKS];                 /* 0x8008011C */
extern int32_t scene_object_count[SCENE_BLOCKS];             /* 0x80090C74 */
extern Object scene_objects[SCENE_BLOCKS][BLOCK_OBJECTS];    /* 0x8009B8DC */

/* Per level - 1: the light's direction (first row of the light matrix) and its colour (first column of
 * the colour matrix). The car lighting also reads them by level. */
extern const int16_t light_directions[12][4]; /* 0x8006F83C */
extern const int16_t light_colours[12][4];    /* 0x8006F894 */
extern MATRIX scene_colour_matrix;            /* 0x8006F7A4 */
extern MATRIX scene_light_matrix;             /* 0x8006F7C4 */

void Init_Scene(void);                                     /* 0x80024C60 */
int VVDraw_Object(Object *obj);                            /* 0x80024E90 */
void Draw_Scene_Object(Object *obj, const SVECTOR *offset); /* 0x80024FBC */
void Draw_Scene_Blocks(void);                              /* 0x80025314 */
void Setup_Object_Block(const uint8_t *block, int slot);   /* 0x80025454 */
void Finish_Object_Block_Decrunch(void);                   /* 0x80025908 */
void Init_Scene_Objects(void);                             /* 0x80026098 */
void Remove_Scene_Objects(void);                           /* 0x80026164 */
void Init_Sky(void);                                       /* 0x80026274 */
void Draw_Sky(void);                                       /* 0x800265D8 */

#endif
