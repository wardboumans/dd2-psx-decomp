#include "game/league.h"

#include "game/car_motion.h"
#include "game/frontend.h"
#include "game/frontend_init.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/replay.h"
#include "graphics/font.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libapi.h"
#include "psx/libgpu.h"
#include "system/controller.h"
#include "system/file.h"
#include "system/mpe_heap.h"

#include <stdio.h>
#include <string.h>

ChampRecord champ_info[CHAMP_RECORDS];
uint8_t car_order[CHAMP_RECORDS];
int32_t division;
int32_t players_per_race;
int32_t current_player;
int32_t season_race;
int32_t season_races;

/* The order the cars start the league in, top of the first division first (0x800714EC). */
static const uint8_t league_order[CHAMP_RECORDS] = {0, 6, 15, 12, 9, 17, 5, 18, 13, 19, 11, 2, 8, 14, 4, 10, 1, 16, 3, 7};

/* The computer drivers' names, by car (0x800713AC). */
static const char computer_names[CHAMP_RECORDS][16] = {
    "", "The Master", "The Trashman", "The Skum", "The Pro", "The Goddess", "Learner Driver", "Psycho",
    "The Chief", "The Optician", "The General", "Heavy Metal H.", "Barmy Army", "Pyromaniac", "The Beast",
    "Passion Wagon", "The Undertaker", "Suicide Squad", "The Bouncer", "Rivit",
};

/* The grid: in a championship by league position (the leader at the back), otherwise car 0 to 19. */
void Order_Cars(void)
{
    if (race_type == RACE_CHAMPIONSHIP) {
        for (int i = 0; i < CHAMP_RECORDS; i++)
            car_order[CHAMP_RECORDS - 1 - (champ_info[i].division * DIVISION_SLOTS + champ_info[i].rank)] = (uint8_t)i;
    } else {
        for (int i = CHAMP_RECORDS - 1; i >= 0; i--)
            car_order[i] = (uint8_t)i;
    }
}

/* The replay of the championship race just run, then the post-race front end again. */
int View_Results_Replay(void)
{
    Set_Pad_Change_Callback(NULL);
    current_level = replay_level;
    Play_Recorded_Replay();
    Init_Post_Race_Front_End();
    return 0;
}

/* The recorded race again, driven by its replay. */
void Play_Recorded_Replay(void)
{
    replay_playback = 1;
    Play_Game();
    replay_playback = 0;
}

/* Insertion sort into ascending order, as the original's (it keeps a -1 sentinel below keys[0]; the
 * keys are never below it). */
void Sort_Keys(int32_t *keys, int n)
{
    for (int i = 1; i < n; i++) {
        int32_t v = keys[i];
        int j = i;
        while (j > 0 && v < keys[j - 1]) {
            keys[j] = keys[j - 1];
            j--;
        }
        keys[j] = v;
    }
}

/* A new single-player league: everybody on no points, in league_order, four divisions of five. */
void Init_League_Info(void)
{
    memset(champ_info, 0, sizeof(champ_info));
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        ChampRecord *e = &champ_info[league_order[i]];
        e->points = 0;
        e->rank = (int16_t)(4 - i % DIVISION_SLOTS);
        e->division = (int16_t)(3 - i / DIVISION_SLOTS);
    }
}

/* A new season: points back to 0, the divisions ranked again. */
void Reset_League_Info(void)
{
    for (int i = CHAMP_RECORDS - 1; i >= 0; i--)
        champ_info[i].points = 0;
    Sort_League();
}

/* The multi-player league: one table of twenty. */
void Init_MultiLeague_Info(void)
{
    memset(champ_info, 0, sizeof(champ_info));
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        ChampRecord *e = &champ_info[league_order[i]];
        e->division = 0;
        e->rank = (int16_t)(19 - i);
        e->points = 0;
    }
}

/* The players' names, then the computer drivers'. */
void Setup_Driver_Names(void)
{
    for (int i = 0; i < human_players; i++)
        snprintf(champ_info[i].name, sizeof(champ_info[i].name), "%s", player_names[i]);
    for (int i = human_players; i < CHAMP_RECORDS; i++)
        snprintf(champ_info[i].name, sizeof(champ_info[i].name), "%s", computer_names[i]);
}

/* The players' results of the race just run: `count` players from `first`, from the first race
 * records. */
void Copy_Race_Results_To_Champ_Info(int first, int count)
{
    for (int k = 0; k < count; k++) {
        ChampRecord *e = &champ_info[first + k];
        const RaceRecord *r = &race_records[k];
        e->race_points = r->points;
        e->finish_position = r->position;
        e->race_finish = r->final_position;
        e->pending_points = r->points;
    }
}

/* The computer drivers' results (cars `first` on). */
void Add_Computer_Info(int first)
{
    for (int i = first; i < CHAMP_RECORDS; i++) {
        ChampRecord *e = &champ_info[i];
        const RaceRecord *r = &race_records[i];
        e->race_points = r->points;
        e->finish_position = r->position;
        e->race_finish = r->final_position;
        e->pending_points = (int16_t)(e->pending_points + r->points);
    }
}

void Update_League_Info(void)
{
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        champ_info[i].points = (int16_t)(champ_info[i].points + champ_info[i].pending_points);
        champ_info[i].pending_points = 0;
    }
}

/* The multi-player round's points: the players keep theirs; the computer drivers, who raced in every
 * one of the `races` races, get their average, rounded to 5. */
void Bank_MultiLeague_Points(int players, int races)
{
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        ChampRecord *e = &champ_info[i];
        int16_t add = e->pending_points;
        if (i >= players) {
            add = (int16_t)((e->pending_points / races + 3) / 5 * 5);
            e->race_points = add;
        }
        e->points = (int16_t)(e->points + add);
        e->pending_points = 0;
    }
}

void Sort_MultiLeague(void)
{
    int list[CHAMP_RECORDS];
    for (int i = 0; i < CHAMP_RECORDS; i++)
        list[champ_info[i].rank] = i;
    Rank_League_Entries(list, CHAMP_RECORDS);
}

/* The player's season: 1 promoted, 2 stays, 3 relegated, 4 won the top division, 5 last of the
 * bottom one (out of the championship). */
int Check_League_Standing(void)
{
    const ChampRecord *e = &champ_info[0];
    if (e->rank == 0)
        return e->division == 0 ? 4 : 1;
    if (e->rank == 4)
        return e->division == 3 ? 5 : 3;
    return 2;
}

/* The top of each lower division goes up, the bottom of each higher one down. */
void Promote_And_Relegate(void)
{
    int bottom[3] = {0}, top[3] = {0};
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        int place = champ_info[i].rank + champ_info[i].division * DIVISION_SLOTS;
        if (place == 4)
            bottom[0] = i;
        if (place == 9)
            bottom[1] = i;
        if (place == 14)
            bottom[2] = i;
        if (place == 5)
            top[0] = i;
        if (place == 10)
            top[1] = i;
        if (place == 15)
            top[2] = i;
    }
    for (int d = 0; d < 3; d++) {
        champ_info[top[d]].division = (int16_t)d;
        champ_info[top[d]].rank = 4;
        champ_info[bottom[d]].division = (int16_t)(d + 1);
        champ_info[bottom[d]].rank = 0;
    }
}

/* Ranks the listed entries by points (ties to the lower car), 0 the most. The cars are counted from
 * num_cars, as on the PSX. */
void Rank_League_Entries(const int *list, int n)
{
    int32_t keys[CHAMP_RECORDS];
    for (int k = 0; k < n; k++)
        keys[k] = champ_info[list[k]].points << 5 | (num_cars - (list[k] + 1));
    Sort_Keys(keys, n);
    for (int k = 0; k < n; k++)
        champ_info[num_cars - ((keys[k] & 0x1f) + 1)].rank = (int16_t)(n - (k + 1));
}

/* The places in the race just run by its points (ties to the lower car), 1 the most. */
void Sort_RacePos(void)
{
    int32_t keys[CHAMP_RECORDS];
    for (int i = 0; i < num_cars; i++)
        keys[i] = champ_info[i].race_points << 5 | (num_cars - i - 1);
    Sort_Keys(keys, num_cars);
    for (int k = 0; k < num_cars; k++)
        champ_info[num_cars - (keys[k] & 0x1f) - 1].finish_position = (int16_t)(num_cars - k);
}

/* Each division ranked by points. */
void Sort_League(void)
{
    int list[CHAMP_RECORDS];
    for (int i = 0; i < CHAMP_RECORDS; i++)
        list[champ_info[i].division * DIVISION_SLOTS + champ_info[i].rank] = i;
    for (int d = 0; d < LEAGUE_DIVISIONS; d++)
        Rank_League_Entries(&list[d * DIVISION_SLOTS], DIVISION_SLOTS);
}

/* The front end again after a championship race, with the RESULTS overlay's screens: the slab, fonts
 * and pad icons, the menu sprites (level F). */
void Init_Post_Race_Front_End(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0}; /* 0x80079870 */
    static const int32_t light_direction[3] = {0x28, 0x28a, 0x28a}; /* 0x80010B94 */
    MATRIX direction_matrix;
    FileRecord sprites = {0}, font_bank = {0};
    DrawSync(0);
    restart_cd_audio = 1;
    SetDispMask(0);
    Show_Loading_Picture();
    SetDispMask(1);
    InitHeap((u_long *)heap_base, HEAP_SIZE);
    Allocate_OT(0x400);
    Init_Primitive_Buffer(0x10000);
    Allocate_Font_Buffers(0x48, 0x200, 6);
    Reset_Print_Banks();
    File_Load("LEV0\\DRIVERS.BIN", drivers_data);
    current_level = LEVEL_RESULTS;
    Set_Load_Textures();
    Queue_Level_Data();
    set_screen_centre(screen_width / 2, screen_height / 2);
    set_projection_distance(0xf0);
    Set_Ambient_Light(black);
    Set_Depth_Cue(1000, 0x8000, black);
    Set_Zclip(0, 0x8000);
    Add_Buffer_Load("LEVF\\LEVEL.SPR", &sprites, FILE_PLAIN);
    Add_Buffer_Load("LEVF\\FONT.BNK", &font_bank, FILE_FONT_CLUTS);
    Add_Buffer_Load("LEVF\\TRACKS.BIN", &frontend_tracks, FILE_PLAIN);
    Buffer_Load(NULL, Load_Completion_Status);
    Relocate_Level_Data();
    Init_Graphics();
    Load_Sprite_Info(sprites.data);
    Setup_Font("FONT", 0, 0);
    Duplicate_Font(0, 3, "CFONT");
    Setup_Font("FONT2", 1, 0);
    Duplicate_Font(1, 4, "CFONT2");
    Duplicate_Font(1, 5, "CFONT2A");
    Setup_Font("FONT3", 2, 1);
    Create_Object(&slab_object, (Mesh *)level_data_section(21));
    Calc_Object_MatrixYXZ(&view_matrix, view_angles);
    Calc_Direction_Matrix(&direction_matrix, light_direction, 0xc);
    Init_Camcorder_Name();
    int32_t offset = Alloc_Prim_Buffer(SCREEN_PRIMS_SIZE);
    screen_prims[0] = (ScreenPrims *)(prim_buffer[0] + offset);
    screen_prims[1] = (ScreenPrims *)(prim_buffer[1] + offset);
    Set_Pad_Change_Callback(Pad_Change_Callback);
    Set_Pad_Button_Sprites();
}
