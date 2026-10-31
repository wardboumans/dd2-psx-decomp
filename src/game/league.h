#ifndef GAME_LEAGUE_H
#define GAME_LEAGUE_H

#include <stdint.h>

#define CHAMP_RECORDS 20
#define LEAGUE_DIVISIONS 4
#define DIVISION_SLOTS 5 /* league position = rank + DIVISION_SLOTS * division */

/* One championship entry, one per car (0x36 bytes; saved with the game). */
typedef struct {
    char name[0x10];          /* +0x00 */
    int16_t points;           /* +0x10 */
    int16_t division;         /* +0x12: 0 (top) .. 3 */
    int16_t rank;             /* +0x14: slot within the division (overall in the multi-player league) */
    int16_t pending_points;   /* +0x16: race points not yet added */
    int16_t race_finish;      /* +0x18 */
    int16_t finish_position;  /* +0x1A: one-based place in the last race */
    int16_t race_points;      /* +0x1C */
    uint8_t unused[0x36 - 0x1e];
} ChampRecord;

_Static_assert(sizeof(ChampRecord) == 0x36, "ChampRecord keeps the saved layout");

extern ChampRecord champ_info[CHAMP_RECORDS]; /* 0x8007F2F8 */
extern uint8_t car_order[CHAMP_RECORDS];      /* 0x800A67F4: the car of each grid slot */
extern int32_t division;                       /* 0x80079C40: the championship division, 0 (slowest computer drivers) to 3 */

extern int32_t players_per_race; /* 0x80079A54: players whose cars are in each race (1) */
extern int32_t current_player;   /* 0x80079C48: the multi-player championship's player in this race */
extern int32_t season_race;      /* 0x80079B28: races run this season */
extern int32_t season_races;     /* 0x8007985C: 5 wrecking racing, 4 stock car */

void Sort_Keys(int32_t *keys, int n);
void Init_League_Info(void);                          /* 0x8004A988 */
void Reset_League_Info(void);                         /* 0x8004AA58 */
void Init_MultiLeague_Info(void);                     /* 0x8004AA98 */
void Setup_Driver_Names(void);                        /* 0x8004AB34 */
void Copy_Race_Results_To_Champ_Info(int first, int count); /* 0x8004AC14 */
void Add_Computer_Info(int first);                    /* 0x8004ACD0 */
void Update_League_Info(void);                        /* 0x8004ADA8 */
void Bank_MultiLeague_Points(int players, int races); /* 0x8004ADE8 */
void Sort_League(void);                               /* 0x8004AEC8 */
void Sort_MultiLeague(void);                          /* 0x8004AF6C */
void Sort_RacePos(void);                              /* 0x8004AFCC */
void Promote_And_Relegate(void);                      /* 0x8004B10C */
int Check_League_Standing(void);                      /* 0x8004B254 */
void Order_Cars(void);                                /* 0x8004B2B8 */
void Rank_League_Entries(const int *list, int n);     /* 0x8004B348 */
void Init_Post_Race_Front_End(void);                  /* 0x8004B494 */

int View_Results_Replay(void);   /* 0x8004B794 */
void Play_Recorded_Replay(void); /* 0x8004B7DC */

#endif
