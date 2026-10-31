/* The memory card screen (load, save, delete) and the save files' contents (GENERAL.BIN). */
#ifndef GAME_LOADSAVE_H
#define GAME_LOADSAVE_H

#include "system/card.h"

#include <stdint.h>

#define SAVE_IMAGE_SIZE CARD_DATA_SIZE /* the data of one save file */

/* What a save file holds (the image's first halfword). */
enum {
    SAVE_CONFIG = 0x1010,      /* the configuration and the records */
    SAVE_BOOT_CONFIG = 0x1020, /* a configuration read at start-up: no slab to turn off */
    SAVE_REPLAY = 0x2020,
    SAVE_GAME = 0x3030,        /* a championship, with the configuration */
};

/* LoadSave's modes. */
enum { LOADSAVE_LOAD = 0, LOADSAVE_CONFIG = 3, LOADSAVE_GAME = 4, LOADSAVE_REPLAY = 5 };

int LoadSave(int mode, uint8_t *image);           /* 0x801E09D0 */
int Memory_Card_Screen(void);                     /* 0x801E7094 */
void Load_Card_File(void);                        /* 0x801E70E0 */
void Save_Configuration(void);                    /* 0x801E71D8 */
void Save_Game_State_To_Card(void);               /* 0x801E7228 */
void Save_Race_Replay(void);                      /* 0x801E7278 */
void Play_Saved_Replay(void);                     /* 0x801E7418 */
void Keep_Season_Snapshot(void);                  /* 0x801E75E8 */
void Restore_Season_Snapshot(void);               /* 0x801E7624 */
void Load_First_Config(void);                     /* 0x801E765C */
void Pack_Save_Image(uint8_t *image, int type);   /* 0x801E777C */
void Unpack_Save_Image(const uint8_t *image);     /* 0x801E7CAC */

#endif
