#ifndef GAME_FRONTEND_H
#define GAME_FRONTEND_H

#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"

#include <stdint.h>

#define SCREEN_POLY_MAX 64
#define SCREEN_LINE_MAX 8
#define SCREEN_Z (-0x17) /* depth of everything drawn on the slab */
#define OFF_SCREEN (-1000) /* where sprites that are not shown are put */

/* Initialisers of the screen tables: sprites with their raw flag set (the screen lists colour
 * them), plain or semi-transparent, and the inks of titles and plain text. */
#define BUTTON_SPRITE(x, y) {x, y, 0, 0, 0, 0, 0, 0, 1}
#define TRANS_SPRITE(x, y) {x, y, 0, 0, 0, 0, 1, 1, 1}
#define TITLE_INK {0x64, 0x80, 0x80, 0}
#define GREY_INK {0x80, 0x80, 0x80, 0}

/* Colour modes of a screen quad. */
enum {
    SCREEN_GREY,           /* 0x80 grey, opaque */
    SCREEN_GREY_TRANS,     /* 0x80 grey, semi-transparent */
    SCREEN_HIGHLIGHT,      /* the pulsing highlight colour, semi-transparent */
    SCREEN_SPRITE_TRANS,   /* the sprite's colour, semi-transparent */
    SCREEN_SPRITE,         /* the sprite's colour, opaque */
};

/* A sprite drawn flat on the slab (0x10 bytes in the original). A list ends with a NULL sprite. */
typedef struct {
    int16_t ot_slot;
    const char *name; /* the sprite the quads are set up from */
    uint8_t mode;     /* SCREEN_* */
    Sprite *sprite;   /* position, size and colour */
} ScreenPoly;

/* A line of text on the slab (0x14 bytes in the original). A list ends with a NULL text. */
typedef struct {
    int16_t ot_slot;
    int16_t font;
    uint8_t ink[4];
    int16_t x, y, z;
    const char *text;
} ScreenText;

/* A line drawn twice, one pixel apart, for thickness (12 bytes in the original). Modes 0 and 1:
 * horizontal, opaque or semi-transparent, the copies offset diagonally; 2: horizontal, offset
 * down; 3: vertical, offset right; others repeat the previous line's shape. A negative OT slot
 * ends a list. */
typedef struct {
    int16_t ot_slot;
    int16_t x, y, length;
    uint8_t r, g, b;
    uint8_t mode;
} ScreenLine;

_Static_assert(sizeof(ScreenLine) == 12, "ScreenLine keeps the original layout");

/* The primitives of the screen lists, one block per display buffer (Init_Front_End). */
typedef struct {
    POLY_FT4 polys[SCREEN_POLY_MAX];      /* 0x000 in the original */
    LINE_F2 lines[SCREEN_LINE_MAX * 2];   /* 0xA00, 0x28 bytes apart */
    POLY_F4 panel;                        /* 0xC80, Draw_Semi_Trans_Poly */
} ScreenPrims;

/* A line from a label to the button it names, in screen pixels. */
typedef struct {
    int16_t x0, y0, x1, y1;
} LabelLine;

#define SCREEN_PRIMS_SIZE 0xd20 /* bytes of the block in the original */
_Static_assert(sizeof(ScreenPrims) <= SCREEN_PRIMS_SIZE * PRIM_SIZE_SCALE, "ScreenPrims fits its allocation");

extern ScreenPrims *screen_prims[2]; /* 0x801F2D74 */

/* An item of the main menu and the configuration menu (0x14 bytes in the original). */
typedef struct {
    int32_t row;              /* 0 top row (RING marks it), 1 bottom row (RINGS) */
    int16_t x, y;             /* where the ring goes */
    void (*caption)(void);    /* sets the titles while the item is selected */
    int (*enter)(void);       /* Cross; the result is not used here */
    void (*toggle)(int forward); /* Square back, Circle forward */
} MenuItem;

/* An item of the small slab menus (0x14 bytes in the original). */
typedef struct {
    int16_t x, y;                /* where the ring goes */
    void (*caption)(void);
    int (*select)(void);         /* Cross; nonzero ends the whole selection */
    void (*toggle)(int forward); /* unused by these menus */
    int32_t last;                /* 0x10000 on the last item; never read */
} SubMenuItem;

extern Object slab_object;           /* 0x801F2D14, the menu panel */
extern MATRIX view_matrix;           /* 0x801F2D4C, the slab's rotation */
extern int16_t view_angles[3];       /* 0x801EDC6C */

/* The game setup the menus edit. */
extern int32_t race_type;         /* 0x80079744: RACE_* */
extern int32_t race_mode;         /* 0x80079748: MODE_* */

enum {
    RACE_PRACTICE,
    RACE_TIME_TRIAL,
    RACE_TOTAL_DESTRUCTION,
    RACE_MULTIPLAYER,   /* a championship for up to ten players taking turns */
    RACE_CHAMPIONSHIP,
};

enum {
    MODE_WRECKING_RACING,
    MODE_STOCK_CAR,
    MODE_DESTRUCTION_DERBY,
};
extern int32_t race_track;        /* 0x8007974C */
extern int32_t race_car;          /* 0x80079750: rookie, amateur, pro */
extern int32_t race_track_count;  /* 0x80079758 */
extern int32_t arena_count;       /* 0x8007975C */
extern int32_t race_menu_choice;  /* 0x8007976C, Toggle_Race_Type's place in race_menu */
#define MAX_PLAYERS 10 /* a multiplayer championship */
extern int32_t human_players;     /* 0x80079860: players taking turns (a championship has one) */
extern int32_t num_cars;          /* 0x80079864 */
extern char player_names[MAX_PLAYERS][12]; /* 0x80090B4C */

/* GENERAL.BIN */
void Rotate_Slab_Off(const int16_t *rect);            /* 0x801EA9D0 */
void Rotate_Slab_On(const int16_t *rect);             /* 0x801EAB0C */
void Setup_Screen_Polys(ScreenPoly *list);            /* 0x801EAD60 */
void Draw_Screen_Polys(void);                         /* 0x801EAEA0 */
void Setup_Screen_Text(const ScreenText *list);       /* 0x801EB428 */
void Setup_Screen_Lines(const ScreenLine *list);      /* 0x801EB544 */
void Draw_Screen_Lines(void);                         /* 0x801EB578 */
void Button_Pressed(int frames, const int16_t *rect); /* 0x801EBD4C */
void Glow_Selector(void);                             /* 0x801EBE84 */
void Draw_Slab(void);                                 /* 0x801EBF8C */
void Null_Menu_Callback(int forward);                 /* 0x801EC04C */
void Draw_Slab_Frame(void);                           /* 0x801EC06C */
void Draw_Semi_Trans_Poly(int x, int y, int w, int h); /* 0x801EC114 */
void Set_Highlight_Grey(void);                        /* 0x801EC3FC */
void Play_Click_FX(void);                             /* 0x801EC44C */
void Play_Warning_FX(void);                           /* 0x801EC49C */
void Play_Select_FX(void);                            /* 0x801EC4EC */

/* FRONTEND.BIN */
void Front_End(void);                                 /* 0x801C9970 */
void Show_Race_Mode_Title(void);                      /* 0x801CA55C */
void Show_Track_Item_Titles(void);                    /* 0x801CA664 */
void Show_Car_Item_Titles(void);                      /* 0x801CA780 */
void Show_File_Manager_Title(void);                   /* 0x801CA840 */
void Show_CD_Player_Title(void);                      /* 0x801CA8B8 */
void Show_Information_Title(void);                    /* 0x801CA930 */
void Show_Configuration_Title(void);                  /* 0x801CA9A8 */
void Show_Go_Title(void);                             /* 0x801CAA20 */
void Toggle_Race_Type(int forward);                   /* 0x801CAA98 */
void Toggle_Track(int forward);                       /* 0x801CAC98 */
void Toggle_Car(int forward);                         /* 0x801CAE78 */
int Remap_Team_Race_Menu_Item(int item, int base);    /* 0x801CAF38 */
void Show_Searching_For_Config(void);                 /* 0x801CAFC0 */
int Select_ChampQS(void);                             /* 0x801D2C8C */
int Select_Pract(void);                               /* 0x801D2D04 */
int Select_TimeT(void);                               /* 0x801D2D64 */
int Select_Total(void);                               /* 0x801D2DC8 */
int Select_DDPract(void);                             /* 0x801D2E2C */
void Restrict_Race_Track_Group(int arenas);           /* 0x801D3324 */
int Choose_Race_Mode(void);                           /* 0x801D1608 */
void Show_Wrecking_Racing_Caption(void);              /* 0x801D1AA0 */
void Show_Stock_Car_Caption(void);                    /* 0x801D1AF8 */
void Show_Destruction_Derby_Caption(void);            /* 0x801D1B50 */
int Select_Wrecking(void);                            /* 0x801D1BA8 */
int Select_Stock(void);                               /* 0x801D1C00 */
int Select_DD(void);                                  /* 0x801D1C5C */
int Choose_Race_Type(void);                           /* 0x801D252C */
void Show_Championship_Caption(void);                 /* 0x801D29C4 */
void Show_Practice_Caption(void);                     /* 0x801D2A1C */
void Show_Time_Trials_Caption(void);                  /* 0x801D2A74 */
void Show_Multiplayer_Caption(void);                  /* 0x801D2ACC */
void Show_Total_Destruction_Caption(void);            /* 0x801D2B24 */
int Select_Champ(void);                               /* 0x801D2B7C */
int Select_Multi(void);                               /* 0x801D2C04 */
int Choose_DD_Race_Type(void);                        /* 0x801D2E8C */
void Draw_Label_Lines(const LabelLine *lines, int count); /* 0x801D0A9C */
int Select_Control_Method(void);                      /* 0x801D05E8 */
int Control_Method_Menu(void);                        /* 0x801CE8D0: the NeGcon set-up */
int Analog_Pad_Config(void);                          /* 0x801CC268 */
int Ask_Analog_Pad_Type(void);                        /* 0x801D0EFC */
int CD_Player_Screen(void);                           /* 0x801CB8D4 */
void Show_CD_Prev_Track_Caption(void);                /* 0x801CBE54 */
void Show_CD_Play_Caption(void);                      /* 0x801CBE94 */
void Show_CD_Stop_Caption(void);                      /* 0x801CBED4 */
void Show_CD_Next_Track_Caption(void);                /* 0x801CBF14 */
int CD_Player_Previous_Track(void);                   /* 0x801CBF54 */
int CD_Player_Play_Track(void);                       /* 0x801CBFC0 */
int CD_Player_Stop(void);                             /* 0x801CC01C */
int CD_Player_Next_Track(void);                       /* 0x801CC058 */
void Set_CD_Track_Texts(void);                        /* 0x801CC0C8 */
int Run_Track_Select(void);                           /* 0x801D33A4 */
int Step_Track_Select_Next(void);                     /* 0x801D363C */
int Step_Track_Select_Prev(void);                     /* 0x801D3684 */
void Set_Track_Picture_Names(void);                   /* 0x801D36CC */
void Show_Track_Picture(void);                        /* 0x801D3958 */
void Toggle_Track_With_Unlocks(int forward);          /* 0x801D3C4C */

extern const int32_t track_levels[11];                /* 0x80070FF0, the level of each race track */

extern int32_t race_cutoff; /* 0x80079868 */
extern int32_t stats_available; /* 0x80079770: a championship has been played */

/* A best lap record (saved with the configuration). */
typedef struct {
    char name[10];
    uint16_t minutes, seconds;
    uint16_t fraction; /* of a second, in 1/65536 */
} BestLap;

_Static_assert(sizeof(BestLap) == 0x10, "BestLap keeps the saved layout");

extern BestLap best_laps[7][5]; /* 0x8007152C: per race track, the five fastest */

void Print_Shadowed_Text(int x, int y, const char *text); /* 0x801C8290 */
void Print_White_Text(int x, int y, const char *text);    /* 0x801C8364 */
int View_BestLaps(void);                              /* 0x801C9454 */
void Fill_Best_Laps_Table(void);                      /* 0x801C95F8 */
int Next_Best_Laps_Track(void);                       /* 0x801C9850 */
int Prev_Best_Laps_Track(void);                       /* 0x801C98E0 */
int Run_Info_Menu(void);                              /* 0x801CB204 */
void Show_View_Statistics_Caption(void);              /* 0x801CB7CC */
void Show_View_Lap_Times_Caption(void);               /* 0x801CB824 */
void Show_View_Credits_Caption(void);                 /* 0x801CB87C */
int Select_Go_Start_Race(void);                       /* 0x801CA520 */
int Enter_Driver_Names(int first, int count);         /* 0x801CDCD0 */
void Restart_Name_Entry_Screen(void);                 /* 0x801CE6F8 */
void Name_Cheat_Outro(void);                          /* 0x801CE7B0 */

extern char lap_name_entry[16];                       /* 0x800A716C */

void Show_Practice_View_Replay_Caption(void); /* FRONTEND:801D236C */
void Show_Practice_Save_Replay_Caption(void); /* FRONTEND:801D23C4 */
void Show_Practice_Quit_Caption(void);        /* FRONTEND:801D241C */
int Select_Practice_View_Replay(void);        /* FRONTEND:801D24DC */
int Practice_Over(void);                      /* FRONTEND:801D1CB8 */

/* RESULTS.BIN */
void Fill_League_Table(void);                 /* RESULTS:801C537C */
int View_League(void);                        /* RESULTS:801C51D8 */
int Prev_League_Division(void);               /* RESULTS:801C5604 */
int Next_League_Division(void);               /* RESULTS:801C56A0 */
int View_MultiLeague(void);                   /* RESULTS:801C6E64 */
void Fill_MultiLeague_Table(void);            /* RESULTS:801C6F90 */
int View_Race_Results(void);                  /* RESULTS:801C81CC */
void Fill_Race_Results_Table(void);           /* RESULTS:801C82F8 */
int Display_Season_Status(void);              /* RESULTS:801C8488 */
void Show_Status_View_Results_Caption(void);  /* RESULTS:801C8D40 */
void Show_Status_View_League_Caption(void);   /* RESULTS:801C8DA0 */
void Show_Status_View_Replay_Caption(void);   /* RESULTS:801C8E00 */
void Show_Status_File_Options_Caption(void);  /* RESULTS:801C8E60 */
void Show_Status_View_Statistics_Caption(void); /* RESULTS:801C8EC0 */
void Show_Status_Next_Race_Caption(void);     /* RESULTS:801C8F20 */
int Ask_Quit_Season_Status(void);             /* RESULTS:801C8FAC */
int End_Of_Season(void);                      /* RESULTS:801C573C */
void Show_Season_View_Results_Caption(void);  /* RESULTS:801C603C */
void Show_Season_View_League_Caption(void);   /* RESULTS:801C609C */
void Show_Season_View_Replay_Caption(void);   /* RESULTS:801C60FC */
void Show_Season_File_Options_Caption(void);  /* RESULTS:801C615C */
void Show_Season_View_Stats_Caption(void);    /* RESULTS:801C61BC */
void Show_Season_Go_Caption(void);            /* RESULTS:801C621C */
int Select_Season_Go(void);                   /* RESULTS:801C6340 */
int Save_Game(void);                          /* RESULTS:801C4934 */
void Show_File_Save_Game_Caption(void);       /* RESULTS:801C5058 */
void Show_File_Save_Replay_Caption(void);     /* RESULTS:801C50B0 */
int Select_File_Save_Game(void);              /* RESULTS:801C5108 */
int Select_File_Save_Replay(void);            /* RESULTS:801C5144 */
int Save_Game_At_Season_End(void);            /* RESULTS:801C636C */
int Ask_Quit_Season_End_Menu(void);           /* RESULTS:801C63C0 */
void Set_Season_End_Title(void);              /* RESULTS:801C6B68 */
int Race_Over(void);                          /* RESULTS:801C7114 */
void Show_Race_Over_View_Replay_Caption(void); /* RESULTS:801C78B4 */
void Show_Race_Over_Save_Replay_Caption(void); /* RESULTS:801C790C */
void Show_Race_Over_Next_Race_Caption(void);  /* RESULTS:801C7964 */
int Select_Race_Over_Save_Replay(void);       /* RESULTS:801C79BC */
int Select_Race_Over_Next_Race(void);         /* RESULTS:801C79F8 */
int Ask_Quit_Season_Race_Over(void);          /* RESULTS:801C7A24 */

#endif
