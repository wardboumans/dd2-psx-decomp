#ifndef GAME_STATS_H
#define GAME_STATS_H

#include <stdint.h>

#define TRACK_RECORD_LEVELS 11

/* A level's record time. */
typedef struct {
    int32_t minutes, seconds, fraction;
} TrackRecord;

extern TrackRecord track_records[TRACK_RECORD_LEVELS]; /* 0x80070D78 */

#define STAT_SEASONS 5

/* A track's line in a season's statistics (0x14 bytes). */
typedef struct {
    char winner[16];
    int16_t wrecks;   /* wrecks dealt in its races */
    int16_t wrecked;  /* cars wrecked or stuck before the end */
} TrackStat;

/* A driver's line (0x14 bytes). */
typedef struct {
    char name[16];
    uint8_t wins;
    uint8_t wrecks;
    int16_t wrecked;
} DriverStat;

/* One season's statistics (0x3AC bytes; saved with the game). */
typedef struct {
    TrackStat tracks[TRACK_RECORD_LEVELS];
    DriverStat drivers[20];
    char standings[20][16]; /* by league place */
} SeasonStats;

_Static_assert(sizeof(SeasonStats) == 0x3ac, "SeasonStats keeps the saved layout");

extern SeasonStats season_stats[STAT_SEASONS]; /* 0x800942C8 */
extern int32_t stats_season;                   /* 0x800798A4: the page being filled */
extern int32_t seasons_played;                 /* 0x800798A8 */

void Start_New_Season_Stats(void);              /* 0x8004B810 */
void Start_Next_Season_Stats(void);             /* 0x8004B878 */
void Update_Track_Stats(int level);             /* 0x8004B9E8 */
void Update_Driver_Stats(void);                 /* 0x8004BBC8 */
void Update_Championship_Stats(void);           /* 0x8004BDCC */
void Update_Jimmy_Spunk_Times(int track, int level); /* 0x8004BF50 */
void Copy_Fastest_Laps_To_Track_Records(void);  /* 0x8004BEAC */

/* FRONTEND.BIN: car select */
int Car_Select_Screen(void);                    /* 0x801C622C */
int Select_Next_Car_Class_Stats(void);          /* 0x801C64A8 */
int Select_Prev_Car_Class_Stats(void);          /* 0x801C651C */
void Show_Car_Stat_Markers(const int32_t *stats); /* 0x801C6590 */
void Spin_Car_Select_Model(void);               /* 0x801C6BF0 */
void Init_Menu_Swatch_Cluts(void);              /* 0x801C6CD8 */
void Restore_Menu_Swatch_Clut(void);            /* 0x801C6E20 */

/* FRONTEND.BIN: configuration, sound volume, centre screen */
int Adjust_Sound_Volume(void);                  /* 0x801C5870 */
void Show_Music_Volume_Caption(void);           /* 0x801C5DC4 */
void Show_Sound_Effects_Caption(void);          /* 0x801C5E1C */
int Select_Music_Volume(void);                  /* 0x801C5E74 */
int Select_Sound_Effects_Volume(void);          /* 0x801C5EA0 */
void Update_Volume_Bars(void);                  /* 0x801C5ECC */
int Centre_Screen_Page(void);                   /* 0x801C6E98 */
int Config_Menu(void);                          /* 0x801C7334 */
void Show_Control_Method_Caption(void);         /* 0x801C7994 */
void Show_Sound_Volume_Caption(void);           /* 0x801C79EC */
void Show_Centre_Screen_Caption(void);          /* 0x801C7A44 */
void Show_Save_Config_Caption(void);            /* 0x801C7A9C */
int Select_Save_Configuration(void);            /* 0x801C7AF4 */
int Credits_Screen(void);                       /* 0x801C7B30 */
int Setup_Credits_Page(int page);               /* 0x801C7D98 */

/* GENERAL.BIN: statistics */
int Get_First_Recorded_Season(void);            /* 0x8004BE80 */
int Get_Current_Recording_Season(void);         /* 0x8004BEA0 */
int Champ_Stats_Screen(void);                   /* 0x801E8274 */
void Format_Champ_Stats_Rows(void);             /* 0x801E8464 */
void Next_Stats_Season(void);                   /* 0x801E85E4 */
int View_Driver_Stats(void);                    /* 0x801E8658 */
void Set_Stats_Driver_Sprite(void);             /* 0x801E8900 */
void Format_Driver_Stats_Rows(void);            /* 0x801E8AE0 */
int Next_Stats_Driver(void);                    /* 0x801E8E4C */
void Format_Driver_Stats_Title(void);           /* 0x801E8F6C */
int Run_Statistics_Menu(void);                  /* 0x801E9010 */
void Show_View_Driver_Stats_Caption(void);      /* 0x801E9484 */
void Show_View_Track_Stats_Caption(void);       /* 0x801E94DC */
void Show_View_Champ_Stats_Caption(void);       /* 0x801E9534 */
int View_Track_Stats(void);                     /* 0x801E958C */
void Format_Track_Stats_Rows(void);             /* 0x801E9780 */

/* GENERAL.BIN */
void Init_Face_Credit_Screen(void);             /* 0x801E9C44 */
void Secret(void);                              /* 0x801E9EEC */

#endif
