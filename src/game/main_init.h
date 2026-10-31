#ifndef GAME_MAIN_INIT_H
#define GAME_MAIN_INIT_H

#include "system/file.h"

#include <stdint.h>
#include <string.h>

#define MAX_LEVEL_TEXTURE_FILES 16
#define LEVEL_DATA_SECTIONS 29

#define RACE_BUFFER_SIZE 0x74000 /* 0x8018C000 to the end of RAM */

extern uint8_t race_buffer[RACE_BUFFER_SIZE]; /* 0x8018C000: the race's level files */
extern int32_t music_volume;   /* 0x80079760, CD audio volume setting */
extern int32_t sound_volume;   /* 0x80079764, effects volume setting */
#define VOLUME_STEP 0x199     /* a notch of the volume settings ... */
#define VOLUME_NOTCHES 10
#define VOLUME_MAX (VOLUME_NOTCHES * VOLUME_STEP) /* ... ten of them: 0xFFA */
extern int32_t current_level;  /* 0x80079BE4: LEVEL_* (the LEVn directory) */

/* The levels; race_track picks one through track_levels. From LEVEL_COLOSSEUM on they are bowls. */
enum {
    LEVEL_FRONT_END = 0,
    LEVEL_PINE_HILLS = 1,
    LEVEL_CHALK_CANYON = 2,
    LEVEL_BLACK_SAIL_VALLEY = 3,
    LEVEL_ULTIMATE_DESTRUCTION = 4,
    LEVEL_CAPRIO_COUNTY = 5,
    LEVEL_LIBERTY_CITY = 6,
    LEVEL_MOTORPLEX = 7,       /* S.C.A. Motorplex, with Wild Bill */
    LEVEL_COLOSSEUM = 8,
    LEVEL_PIT = 9,
    LEVEL_RED_PIKE = 10,
    LEVEL_DEATH_BOWL = 11,     /* the stadium */
    LEVEL_RESULTS = 15,        /* the championship's results screens */
    FIRST_BOWL_LEVEL = LEVEL_COLOSSEUM,
};
extern FileRecord level_texture_files[MAX_LEVEL_TEXTURE_FILES]; /* 0x800800EC: LEVEL.TX0, TX1, ... */
extern FileRecord level_cluts_file;  /* 0x80079B04, LEVEL.TXC */
extern FileRecord fi_levdat;         /* 0x80079C1C, LEVEL.DAT */
extern uint8_t *level_data;          /* 0x80079A50 */

/* LEVEL.DAT starts with 29 section offsets. The original turns them into pointers in place;
 * a 32-bit field can't hold a native pointer, so the port resolves them when they are used. */
static inline uint8_t *level_data_section(int index)
{
    int32_t offset;
    memcpy(&offset, level_data + 4 * index, sizeof(offset));
    return level_data + offset;
}

void Init_Main(void);                  /* 0x80042740 */
void Init_Main_Without_Data(void);     /* 0x80042668 */
void Play_Intro(void);                 /* 0x8004312C */
void Play_Xtro(void);                  /* 0x800431D4 */
void Set_Load_Textures(void);          /* 0x8004291C */
void Init_Graphics(void);              /* 0x800429E0 */
void Queue_Level_Data(void);           /* 0x80042A08 */
void Relocate_Level_Data(void);        /* 0x80042A48 */
void Sound_Timer_VSync_Callback(void); /* 0x80042E28 */

void Init_Game(void); /* 0x80042BB4 */

#endif
