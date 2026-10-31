#ifndef GAME_FRONTEND_INIT_H
#define GAME_FRONTEND_INIT_H

#include "graphics/object.h"
#include "system/file.h"

#include <stdint.h>

#define CAMCORDER_TRACKS 11
#define DRIVERS_BIN_SIZE 0x38000

/* The front end's models (LEVEL.DAT sections of LEV0); unnamed ones keep their address.
 * slab_object (section 21) is in frontend.c. */
extern Object object_801f2cf8, dpadbuttons_object, dpadlabels_object, negleft_object;
extern Object object_801f2cdc, negbut1_object, negbut2_object, negbut3_object, madjoy_object;
extern Object madbut_object, madbase_object, object_801f2cb0, carselect_object, track_object;

extern int32_t restart_cd_audio;              /* 0x80079774 */
extern uint8_t drivers_data[DRIVERS_BIN_SIZE]; /* 0x8018C000, LEV0\DRIVERS.BIN */
extern FileRecord frontend_tracks;            /* 0x80079AAC, LEVF\TRACKS.BIN */
extern uint8_t *track_data;                   /* 0x801F2E5C, LEV0\TRACKS.BIN */
extern int32_t demo_mode;                     /* 0x80079128 */

extern int32_t copyright_pending;                   /* 0x80079858 */
extern int32_t pad_layout_setting;                  /* 0x80079768 */
extern int32_t current_pad_type;                    /* 0x80079B3C */
extern uint16_t camcorder_cluts[CAMCORDER_TRACKS];  /* 0x8007BD08 */
extern int16_t camcorder_vram_x, camcorder_vram_y;  /* 0x800799B4 */

void Init_Front_End(void);                          /* 0x80048A90 */
void Play_Outro_And_Reload(void);                   /* 0x800492A4 */
int DemoMode(void);                                 /* 0x800492CC */
void Set_Pad_Button_Sprites(void);                  /* 0x80048F90 */
void Pad_Change_Callback(int type);                 /* 0x800491DC */
void Init_Camcorder_Name(void);                     /* 0x800493C8 */
void Loading_Screen_From_Slab(const char *next_driver); /* 0x80049468 */
void Show_Camcorder_Loading_Screen(void);           /* 0x800495CC */
int Start_Selected_Race(void);                      /* 0x80049928 */
void Show_Loading_Picture(void);                    /* 0x8004961C */
void Load_Completion_Status(int progress);          /* 0x800496EC */
void Setup_Pad(int type, int layout, int config0, int config1); /* 0x80049C60 */

int View_Frontend_Replay(void); /* 0x8004925C */

void Calculate_Finish(const int16_t *place_points); /* 0x8004A2EC */
void Calculate_Results(void);                 /* 0x8004A6E0 */
int Do_End_Of_Season_Stuff(void);             /* 0x8004A898 */
void Championship(void);                      /* 0x80049F50 */
void MultiChamp(void);                        /* 0x8004A0E8 */
void Init_Wrecking_Championship(void);        /* 0x80049E00 */
void Init_StockCar_Championship(void);        /* 0x80049E58 */
void Start_Multi_Wrecking_Championship(void); /* 0x80049EB0 */
void Init_StockCar_MultiChamp(void);          /* 0x80049F00 */

#endif
