#include "game/frontend_init.h"

#include "game/car.h"
#include "game/car_motion.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/league.h"
#include "game/play_game.h"
#include "game/replay.h"
#include "game/stats.h"
#include "graphics/font.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libapi.h"
#include "psx/libc.h"
#include "psx/libetc.h"
#include "psx/libgpu.h"
#include "system/controller.h"
#include "system/file.h"
#include "system/mpe_heap.h"

#include <stdio.h>
#include <string.h>

/* COPYRITE.RAW is a TIM with a 0x14-byte header, loaded at the buffer start; LOADING.RAW is
 * bare pixels loaded 0x14 bytes in. Either way the 320x256 picture starts at +0x14. */
#define PICTURE_OFFSET 0x14

/* 0x80071144: per pad type (digital, analog, NeGcon) four button layouts of 18 words; the
 * first 16 map pad buttons to game functions. */
static const uint16_t pad_layouts[3][4][18] = {
    { /* digital */
        {0x0001, 0x0000, 0x0000, 0x0008, 0x0000, 0x0020, 0x0000, 0x0080, 0x0100,
         0x0200, 0x0400, 0x0800, 0x0000, 0x0000, 0x4000, 0x8000, 0x0000, 0x0000},
        {0x0001, 0x0000, 0x0000, 0x0008, 0x0000, 0x0020, 0x0000, 0x0080, 0x0100,
         0x0200, 0x0400, 0x0800, 0x0000, 0x0000, 0x8000, 0x4000, 0x0000, 0x0000},
        {0x0001, 0x0000, 0x0000, 0x0008, 0x0000, 0x0020, 0x0000, 0x0080, 0x0400,
         0x0800, 0x0100, 0x0200, 0x8000, 0x4000, 0x0000, 0x0000, 0x0000, 0x0000},
        {0x0001, 0x0000, 0x0000, 0x0008, 0x0000, 0x0020, 0x0000, 0x0080, 0x0400,
         0x0800, 0x0100, 0x0200, 0x4000, 0x8000, 0x0000, 0x0000, 0x0000, 0x0000},
    },
    { /* analog */
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x0000, 0x0000, 0x4000, 0x8000, 0x0000, 0x0000},
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x0000, 0x0000, 0x8000, 0x4000, 0x0000, 0x0000},
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x8000, 0x4000, 0x0000, 0x0000, 0x0000, 0x0000},
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x4000, 0x8000, 0x0000, 0x0000, 0x0000, 0x0000},
    },
    { /* NeGcon */
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x0000, 0x0000, 0x4000, 0x8000, 0x0000, 0x0000},
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x0000, 0x0000, 0x8000, 0x4000, 0x0000, 0x0000},
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x8000, 0x4000, 0x0000, 0x0000, 0x0000, 0x0000},
        {0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0100, 0x0200, 0x4000, 0x8000, 0x0000, 0x0000, 0x0000, 0x0000},
    },
};

/* 0x80071118: the track sprites whose CLUTs colour the camcorder loading screen */
static const char *const camcorder_tracks[CAMCORDER_TRACKS] = {
    "PINEHILL", "CHALKCAN", "BLAKSAIL", "ULTDEST", "CAPRIO", "LIBCITY",
    "MOTRPLEX", "COLOSEUM", "THEPIT", "REDPIKE", "DETHBOWL",
};

/* LEVF\TRACKS.BIN: per level an offset to its compressed 128x128 16-bit picture. */
#define TRACK_PICTURE_SIZE 0x80
#define TRACK_PICTURE_BUFFER 33000
#define DEMO_LEVELS 10

Object object_801f2cf8, dpadbuttons_object, dpadlabels_object, negleft_object;
Object object_801f2cdc, negbut1_object, negbut2_object, negbut3_object, madjoy_object;
Object madbut_object, madbase_object, object_801f2cb0, carselect_object, track_object;

int32_t restart_cd_audio;
uint8_t drivers_data[DRIVERS_BIN_SIZE];
FileRecord frontend_tracks;
uint8_t *track_data;
int32_t demo_mode;

int32_t copyright_pending = 1;
int32_t pad_layout_setting;
int32_t current_pad_type;
uint16_t camcorder_cluts[CAMCORDER_TRACKS];
int16_t camcorder_vram_x, camcorder_vram_y;

static char next_driver_text[0x40]; /* 0x8007BC80 */
static POLY_G4 bar_done; /* 0x8007BCC0 */
static POLY_G4 bar_left; /* 0x8007BCE4 */

/* The front end's start: display and 3D setup, the loading picture, then everything the menus
 * need from LEV0 (textures, level data with the menu models, sprites, fonts, track tables).
 * FRONTEND.BIN and GENERAL.BIN are linked into the port, so their File_Load calls are gone. */
void Init_Front_End(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0}; /* 0x80079778 */
    static const int32_t light_direction[3] = {0x28, 0x28a, 0x28a};
    MATRIX direction_matrix;
    FileRecord sprites = {0}, font_bank = {0}, tracks = {0};

    DrawSync(0);
    restart_cd_audio = 1;
    Set_Draw_Mode(0);
    set_screen_centre(screen_width / 2, screen_height / 2);
    set_projection_distance(0xf0);
    Set_Ambient_Light(black);
    Set_Depth_Cue(1000, 0x8000, black);
    Set_Zclip(0, 0x8000);
    Show_Loading_Picture();
    InitHeap((u_long *)heap_base, HEAP_SIZE);
    Allocate_OT(0x400);
    Swap_Draw_Buffers();
    Draw_All(0);
    Swap_Draw_Buffers();
    Draw_All(0);
    SetDispMask(1);
    Init_Primitive_Buffer(0x18000);
    Allocate_Font_Buffers(0x40, 0x200, 6);
    Reset_Print_Banks();
    File_Load("LEV0\\DRIVERS.BIN", drivers_data);
    current_level = LEVEL_FRONT_END;
    Set_Load_Textures();
    Queue_Level_Data();
    Add_Buffer_Load("LEV0\\LEVEL.SPR", &sprites, FILE_PLAIN);
    Add_Buffer_Load("LEV0\\FONT.BNK", &font_bank, FILE_FONT_CLUTS);
    Add_Buffer_Load("LEV0\\TRACKS.BIN", &tracks, FILE_PLAIN);
    Add_Buffer_Load("LEVF\\TRACKS.BIN", &frontend_tracks, FILE_PLAIN);
    Buffer_Load(NULL, Load_Completion_Status);
    track_data = tracks.data;
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
    Create_Object(&object_801f2cf8, (Mesh *)level_data_section(22));
    Create_Object(&dpadbuttons_object, (Mesh *)level_data_section(23));
    Create_Object(&dpadlabels_object, (Mesh *)level_data_section(24));
    Create_Object(&negleft_object, (Mesh *)level_data_section(25));
    Create_Object(&object_801f2cdc, (Mesh *)level_data_section(26));
    Create_Object(&negbut1_object, (Mesh *)level_data_section(15));
    Create_Object(&negbut2_object, (Mesh *)level_data_section(16));
    Create_Object(&negbut3_object, (Mesh *)level_data_section(17));
    Create_Object(&madjoy_object, (Mesh *)level_data_section(7));
    Create_Object(&madbut_object, (Mesh *)level_data_section(8));
    Create_Object(&madbase_object, (Mesh *)level_data_section(9));
    Create_Object(&object_801f2cb0, (Mesh *)level_data_section(10));
    Create_Object(&carselect_object, (Mesh *)level_data_section(27));
    Create_Object(&track_object, (Mesh *)level_data_section(28));
    Calc_Object_MatrixYXZ(&view_matrix, view_angles);
    Calc_Direction_Matrix(&direction_matrix, light_direction, 0xc);
    Init_Camcorder_Name();
    int32_t offset = Alloc_Prim_Buffer(SCREEN_PRIMS_SIZE);
    screen_prims[0] = (ScreenPrims *)(prim_buffer[0] + offset);
    screen_prims[1] = (ScreenPrims *)(prim_buffer[1] + offset);
    Set_Pad_Change_Callback(Pad_Change_Callback);
    Set_Pad_Button_Sprites();
}

/* Copies the button icons for the connected pad type into the five ZZZZPADn sprites. */
void Set_Pad_Button_Sprites(void)
{
    static const char *const icons[3][5] = {
        {"CROSS", "TRIANGLE", "SQUARE", "CIRCLE", "START"},
        {"NEG_I", "NEG_B", "NEG_II", "NEG_A", "NEG_S"},
        {"MAD_I", "MAD_B", "MAD_II", "MAD_A", "MAD_S"},
    };
    int set;
    if (pad.type == PAD_NEGCON)
        set = 1;
    else if (pad.type == PAD_DIGITAL)
        set = 0;
    else if (pad.type == PAD_ANALOG)
        set = 2;
    else
        return;
    const SpriteInfo *src[5];
    for (int i = 0; i < 5; i++)
        src[i] = Search_For_Sprite(icons[set][i]);
    for (int i = 0; i < 5; i++) {
        char name[16];
        sprintf(name, "ZZZZPAD%d", i + 1);
        const SpriteInfo *dst = Search_For_Sprite(name);
        RECT r = {src[i]->vram_x, src[i]->vram_y, 4, 16};
        MoveImage(&r, dst->vram_x, dst->vram_y);
        DrawSync(0);
        r.x = src[i]->clut_x;
        r.y = src[i]->clut_y;
        r.w = 16;
        r.h = 1;
        MoveImage(&r, dst->clut_x, dst->clut_y);
        DrawSync(0);
    }
}

void Pad_Change_Callback(int type)
{
    int layout_type;
    if (type == PAD_NEGCON)
        layout_type = 2;
    else if (type == PAD_DIGITAL)
        layout_type = 1;
    else if (type == PAD_ANALOG)
        layout_type = 3;
    else
        return;
    Setup_Pad(layout_type, 0, 1, 2);
    Set_Pad_Button_Sprites();
    current_pad_type = type;
}

void Init_Camcorder_Name(void)
{
    for (int i = 0; i < CAMCORDER_TRACKS; i++) {
        const SpriteInfo *s = Search_For_Sprite(camcorder_tracks[i]);
        camcorder_cluts[i] = GetClut(s->clut_x, s->clut_y);
    }
    const SpriteInfo *cam = Search_For_Sprite("CAMCORDR");
    camcorder_vram_x = cam->vram_x;
    camcorder_vram_y = cam->vram_y;
}

/* GO: the race the menus set up. Practice, time trials and total destruction run the race here (a
 * practice race offers the replay afterwards, a time trial updates the target times); the
 * championships run their own season. Then back to the front end, loading screen up. */
int Start_Selected_Race(void)
{
    int abandoned = 0;
    Set_Pad_Change_Callback(NULL);
    switch (race_type) {
    case RACE_PRACTICE:
    case RACE_TOTAL_DESTRUCTION:
        current_level = track_levels[race_track];
        Loading_Screen_From_Slab(NULL);
        Order_Cars();
        abandoned = Play_Game();
        Init_Front_End();
        if (abandoned == 0) {
            Practice_Over();
            return 0;
        }
        break;
    case RACE_TIME_TRIAL: {
        current_level = track_levels[race_track];
        int level = current_level - 1;
        Copy_Fastest_Laps_To_Track_Records();
        Loading_Screen_From_Slab(NULL);
        Order_Cars();
        abandoned = Play_Game();
        Init_Front_End();
        Show_Camcorder_Loading_Screen();
        if (abandoned == 0)
            Update_Jimmy_Spunk_Times(race_track, level);
        return 0;
    }
    case RACE_MULTIPLAYER:
        if (race_mode == MODE_WRECKING_RACING)
            Start_Multi_Wrecking_Championship();
        else if (race_mode == MODE_STOCK_CAR)
            Init_StockCar_MultiChamp();
        Init_Front_End();
        break;
    case RACE_CHAMPIONSHIP:
        if (race_mode == MODE_WRECKING_RACING)
            Init_Wrecking_Championship();
        else if (race_mode == MODE_STOCK_CAR)
            Init_StockCar_Championship();
        Init_Front_End();
        break;
    default:
        return 0;
    }
    Show_Camcorder_Loading_Screen();
    return 0;
}

/* The replay of the race just run, from the front end. */
int View_Frontend_Replay(void)
{
    Set_Pad_Change_Callback(NULL);
    current_level = replay_level;
    Play_Recorded_Replay();
    Init_Front_End();
    return 0;
}

/* The outro FMV, then everything the front end needs loaded again. */
void Play_Outro_And_Reload(void)
{
    Play_Xtro();
    Init_Front_End();
}

/* The attract mode: a 20-car wrecking race on a random track, with the race setup kept. */
int DemoMode(void)
{
    static int32_t saved_race_type, saved_race_mode, saved_race_car, saved_num_cars; /* 0x800799A4 */
    Set_Pad_Change_Callback(NULL);
    saved_num_cars = num_cars;
    saved_race_car = race_car;
    saved_race_mode = race_mode;
    saved_race_type = race_type;
    race_car = 2;
    num_cars = 20;
    race_mode = MODE_WRECKING_RACING;
    race_type = RACE_PRACTICE;
    demo_mode = 1;
    current_level = rand() % DEMO_LEVELS + 1;
    Order_Cars();
    int result = Play_Game();
    demo_mode = 0;
    race_type = saved_race_type;
    race_mode = saved_race_mode;
    race_car = saved_race_car;
    num_cars = saved_num_cars;
    Init_Front_End();
    return result;
}

/* 0x8007101C: the loading screen's sprites */
static Sprite loading_sprites[3] = {
    {16, 24, 0, 0, 0, 0, 0, 0, 1},  /* ILOAD */
    {32, 72, 0, 0, 0, 0, 0, 0, 1},  /* CAMCORDR */
    {32, 216, 0, 0, 0, 0, 0, 0, 1}, /* LOADBAR */
};

static ScreenPoly loading_polys[] = { /* 0x80071088 */
    {1, "CAMCORDR", SCREEN_GREY, &loading_sprites[1]},
    {1, "ILOAD", SCREEN_GREY, &loading_sprites[0]},
    {1, "LOADBAR", SCREEN_GREY, &loading_sprites[2]},
    {0, NULL, 0, NULL},
};

static ScreenText loading_text_records[] = { /* 0x800710C8 */
    {1, 0, {0x80, 0x80, 0x80, 0}, -104, -96, -0x17, "%R%JL%T/Loading"},
    {2, 3, {0, 0, 0, 0}, -101, -93, -0x17, "%R%JL%T/Loading"},
    {1, 2, {0, 0, 0, 0}, 0, 78, -0x17, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine loading_lines[] = { /* 0x80071064 */
    {1, 54, 52, 92, 0xff, 0xff, 0xff, 0},
    {2, 57, 55, 92, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* The loading screen flips in; with `next_driver`, it says who races next. For a track level the
 * camcorder shows the track's picture in the track's colours. */
void Loading_Screen_From_Slab(const char *next_driver)
{
    if (!next_driver) {
        loading_text_records[2].text = "";
    } else {
        snprintf(next_driver_text, sizeof(next_driver_text), "%%R%%JC%%T/%s to race next !", next_driver);
        loading_text_records[2].text = next_driver_text;
    }
    Setup_Screen_Polys(loading_polys);
    Setup_Screen_Text(loading_text_records);
    Setup_Screen_Lines(loading_lines);
    if (current_level != LEVEL_FRONT_END) {
        RECT area = {camcorder_vram_x, camcorder_vram_y, TRACK_PICTURE_SIZE, TRACK_PICTURE_SIZE};
        const uint8_t *offsets = frontend_tracks.data;
        int32_t offset;
        memcpy(&offset, offsets + current_level * 4, sizeof(offset));
        u_long *picture = MPE_malloc(TRACK_PICTURE_BUFFER);
        DecompressState d = {offsets + offset, (uint8_t *)picture, 0, 0x8000, 0};
        Decompress(&d);
        DrawSync(0);
        LoadImage(&area, picture);
        DrawSync(0);
        MPE_free(picture); /* PSX: libc free() on an MPE_malloc block */
        screen_prims[0]->polys[0].clut = camcorder_cluts[current_level - 1];
        screen_prims[1]->polys[0].clut = camcorder_cluts[current_level - 1];
    }
    Rotate_Slab_On(NULL);
}

/* The loading screen with the camcorder picture, shown by turning the slab away. */
void Show_Camcorder_Loading_Screen(void)
{
    Setup_Screen_Polys(loading_polys);
    Setup_Screen_Text(loading_text_records);
    Setup_Screen_Lines(loading_lines);
    Rotate_Slab_Off(NULL);
}

/* Puts the picture in both display buffers, so it stays up whichever one is shown. */
void Show_Loading_Picture(void)
{
    if (copyright_pending == 0) {
        File_Load("LEV0\\LOADING.RAW", heap_base + PICTURE_OFFSET);
    } else {
        File_Load("LEV0\\COPYRITE.RAW", heap_base);
        copyright_pending = 0;
    }
    RECT area = {0, 0, 320, 256};
    DrawSync(0);
    VSync(0);
    LoadImage(&area, (u_long *)(heap_base + PICTURE_OFFSET));
    area.y = 256;
    DrawSync(0);
    VSync(0);
    LoadImage(&area, (u_long *)(heap_base + PICTURE_OFFSET));
    DrawSync(0);
}

/* The loading bar under the picture: grey up to `progress` (0..0x1000), blue after it. */
void Load_Completion_Status(int progress)
{
    Swap_Draw_Buffers();
    short top = pal_mode ? 0xd8 : 0xd0;
    int done = progress / 16;
    if (done > 0x100)
        done = 0x100;
    short split = (short)(done + 0x20);
    short bottom = (short)(top + 0xc);

    setPolyG4(&bar_done);
    setRGB0(&bar_done, 0, 0, 0);
    setRGB1(&bar_done, 0x32, 0x32, 0x32);
    setRGB2(&bar_done, 0, 0, 0);
    setRGB3(&bar_done, 0x32, 0x32, 0x32);
    setXY4(&bar_done, 0x20, top, split, top, 0x20, bottom, split, bottom);
    DrawPrim(&bar_done);

    setPolyG4(&bar_left);
    setRGB0(&bar_left, 0x89, 0xc9, 0xdf);
    setRGB1(&bar_left, 0x36, 0x66, 0x9b);
    setRGB2(&bar_left, 0x89, 0xc9, 0xdf);
    setRGB3(&bar_left, 0x36, 0x66, 0x9b);
    setXY4(&bar_left, split, top, 0x120, top, split, bottom, 0x120, bottom);
    DrawPrim(&bar_left);
    Swap_Buffers(1);
}

void Setup_Pad(int type, int layout, int config0, int config1)
{
    const uint16_t(*table)[18];
    if (type == 2)
        table = pad_layouts[2];
    else if (type >= 0 && type < 2)
        table = pad_layouts[0];
    else if (type == 3)
        table = pad_layouts[1];
    else
        return;
    memcpy(pad_layout, table[layout], sizeof(pad_layout));
    pad_config[0] = (uint16_t)config0;
    pad_config[1] = (uint16_t)config1;
    pad_layout_setting = layout;
    Init_Pad_Config();
}

/* Each division's season: the levels of its races (0x800712F4). */
static const int32_t season_levels[LEAGUE_DIVISIONS][5] = {
    {1, 2, 5, 7, 10}, {2, 5, 7, 3, 8}, {1, 7, 3, 6, 9}, {2, 6, 3, 4, 11},
};

/* The computer drivers' wrecking skill, by car (0x80071344). */
static const int16_t computer_wrecking[CHAMP_RECORDS] = {
    0, 100, 70, 100, 80, 50, 0, 90, 90, 20, 80, 60, 40, 50, 70, 30, 90, 60, 80, 80,
};

/* Points by place: wrecking racing (on top of the wrecking points), stock car (0x80071394, 0x8007136C). */
static const int16_t wrecking_place_points[CHAMP_RECORDS] = {50, 25, 10};
static const int16_t stock_place_points[CHAMP_RECORDS] = {
    100, 75, 50, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
};

/* The race's places and points. When nobody finished, the places come from how far each car got,
 * the cars still running helped by chance and by their league place. */
void Calculate_Finish(const int16_t *place_points)
{
    for (int i = 0; i < num_cars; i++) {
        if (race_records[i].finished == 0)
            continue;
        for (int c = 0; c < num_cars; c++) {
            RaceRecord *r = &race_records[c];
            int place = r->final_position == 21 ? r->position : r->final_position;
            r->points = (int16_t)(r->points + place_points[place - 1]);
            if (r->points > 999)
                r->points = 999;
        }
        return;
    }
    int32_t keys[CHAMP_RECORDS];
    for (int c = 0; c < num_cars; c++) {
        const RaceRecord *r = &race_records[c];
        int32_t key = r->lap_counted * track_info[current_level].lap_strips + r->strip;
        if (car_blocks[c].wrecked == 0) {
            const ChampRecord *e = &champ_info[c];
            key += (rand() & 0x7f) + ((3 - e->division) * 5 + 4 - e->rank) * 10;
        }
        keys[c] = key << 5 | c;
    }
    Sort_Keys(keys, num_cars);
    for (int k = 0; k < num_cars; k++) {
        int place = num_cars - (k + 1);
        RaceRecord *r = &race_records[keys[k] & 0x1f];
        r->points = (int16_t)(r->points + place_points[place]);
        r->position = (int16_t)(place + 1);
        r->final_position = (int16_t)(place + 1);
        if (r->points > 999)
            r->points = 999;
    }
}

/* Points for the race just run. In wrecking racing the computer drivers' wrecking points are made up
 * from their skill and league division, with chance, rather than taken from the race. */
void Calculate_Results(void)
{
    if (race_mode == MODE_WRECKING_RACING) {
        for (int c = 1; c < num_cars; c++) {
            int d = 3 - champ_info[c].division;
            int r = rand();
            int32_t points = computer_wrecking[c] / 2 + r % (d * 3 + 0x11) * 5 + d * 0x19;
            int16_t *p = &race_records[c].points;
            *p = (int16_t)points;
            if ((int16_t)points < 0)
                *p = 0;
            if (*p > 999)
                *p = 999;
        }
        Calculate_Finish(wrecking_place_points);
    } else {
        for (int c = 0; c < num_cars; c++)
            race_records[c].points = 0;
        Calculate_Finish(stock_place_points);
    }
}

/* The outro after winning the top division. */
static void Championship_Won(void) /* 0x8004A878 */
{
    Play_Xtro();
}

/* The season's outcome: promotion opens more tracks and arenas and raises the division; relegation
 * lowers it. Returns 1 when the top division was won, -1 when the player is out, else 0 with the
 * next season set up. */
int Do_End_Of_Season_Stuff(void)
{
    switch (Check_League_Standing()) {
    case 1:
        if (race_track_count < division + 5)
            race_track_count = division + 5;
        if (arena_count < division + 2)
            arena_count = division + 2;
        division++;
        break;
    case 3:
        division--;
        break;
    case 4:
        return 1;
    case 5:
        return -1;
    }
    Promote_And_Relegate();
    season_race = 0;
    Reset_League_Info();
    Start_Next_Season_Stats();
    return 0;
}

/* The single-player championship: the season's races, the status screen between them, the season's
 * end; until the top division is won, the player is out, or quits. */
void Championship(void)
{
    int result;
    do {
        for (int race = season_race; race < season_races; race++) {
            current_level = season_levels[division][season_race];
            Loading_Screen_From_Slab(NULL);
            Order_Cars();
            if (Play_Game() != 0)
                return;
            Calculate_Results();
            Update_Driver_Stats();
            Update_Track_Stats(current_level - 1);
            Copy_Race_Results_To_Champ_Info(0, players_per_race);
            Add_Computer_Info(players_per_race);
            Update_League_Info();
            Sort_RacePos();
            Sort_League();
            season_race++;
            if (race < season_races - 1) {
                Init_Post_Race_Front_End();
                if (Display_Season_Status() == 0)
                    return;
            }
        }
        Update_Championship_Stats();
        Init_Post_Race_Front_End();
        if (End_Of_Season() == 0)
            return;
        result = Do_End_Of_Season_Stuff();
        if (result == -1)
            return;
    } while (result != 1);
    Championship_Won();
}

/* The multi-player championship: each race is run once per player in turn (players_per_race at a
 * time), the computer drivers scoring their average. */
void MultiChamp(void)
{
    for (int race = season_race; race < season_races; race++) {
        for (current_player = 0; current_player < human_players; current_player += players_per_race) {
            current_level = season_levels[division][season_race];
            Loading_Screen_From_Slab(champ_info[current_player].name);
            Order_Cars();
            if (Play_Game() != 0)
                return;
            Calculate_Results();
            Copy_Race_Results_To_Champ_Info(current_player, players_per_race);
            Add_Computer_Info(human_players);
            if (current_player < human_players - players_per_race) {
                Init_Post_Race_Front_End();
                if (Race_Over() == 0)
                    return;
            }
        }
        Bank_MultiLeague_Points(human_players, human_players / players_per_race);
        Sort_RacePos();
        Sort_MultiLeague();
        season_race++;
        if (race < season_races - 1) {
            Init_Post_Race_Front_End();
            if (Display_Season_Status() == 0)
                return;
        }
    }
    Init_Post_Race_Front_End();
    End_Of_Season();
}

static void new_championship(int races)
{
    players_per_race = 1;
    current_player = 0;
    season_race = 0;
    division = 0;
    season_races = races;
}

void Init_Wrecking_Championship(void)
{
    Init_League_Info();
    Start_New_Season_Stats();
    Setup_Driver_Names();
    new_championship(5);
    Championship();
}

void Init_StockCar_Championship(void)
{
    Init_League_Info();
    Start_New_Season_Stats();
    Setup_Driver_Names();
    new_championship(4);
    Championship();
}

void Start_Multi_Wrecking_Championship(void)
{
    Init_MultiLeague_Info();
    Setup_Driver_Names();
    new_championship(5);
    MultiChamp();
}

void Init_StockCar_MultiChamp(void)
{
    Init_MultiLeague_Info();
    Setup_Driver_Names();
    new_championship(4);
    MultiChamp();
}
