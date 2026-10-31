#include "game/frontend.h"

#include "game/frontend_init.h"
#include "game/league.h"
#include "game/loadsave.h"
#include "game/main_init.h"
#include "game/sound_fx.h"
#include "game/stats.h"
#include "graphics/font.h"
#include "psx/libgte.h"
#include "sound/cd.h"
#include "system/controller.h"
#include "system/debug.h"
#include "system/maths.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_HALF_W (SCREEN_W / 2) /* screen lists are in SCREEN_W x SCREEN_H pixels */
#define SCREEN_HALF_H (SCREEN_H / 2)
#define DEMO_DELAY 0x708           /* idle vblanks before the demo starts */
#define DEMO_RETRY 0xf0
#define FRONT_END_CD_TRACK 12

ScreenPrims *screen_prims[2];
Object slab_object;
MATRIX view_matrix;
int16_t view_angles[3] = {QUARTER_TURN, 0, 0};

int32_t race_type;
int32_t race_mode;
int32_t race_track;
int32_t race_car;
int32_t race_track_count = 4;
int32_t arena_count = 1;
int32_t race_menu_choice;
int32_t human_players = 1;
int32_t num_cars = 20;
int32_t race_cutoff;
char player_names[MAX_PLAYERS][12];

static ScreenPoly *screen_poly_list;         /* 0x801F2D7C */
static const ScreenLine *screen_line_list;   /* 0x801F2D70 */
static TILE back_tile[2];                    /* 0x801F2CCC */
static MATRIX slab_light_matrix;             /* 0x801F2E98 */
static CVECTOR highlight_colour = {0x80, 0x80, 0x80, 0}; /* 0x801EDC78 */
static int32_t glow_counter;                 /* 0x801EDC7C */

static const int16_t slab_world_matrix[3][3] = {{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}; /* 0x801E099C */
static const int32_t slab_position[3] = {0, 0, 0x107};    /* 0x801EDA20 */
static const MATRIX slab_colour_matrix = {{{0x16a1, 0, 0}, {0x16a1, 0, 0}, {0x16a1, 0, 0}}, {0, 0, 0}}; /* 0x801EDA30 */
static const MATRIX slab_light_direction = {{{0, -0xb50, -0xb50}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}}; /* 0x801EDA50 */
static const int32_t slab_world_position[3] = {0, 0, 0}; /* 0x801EDA70 */
static const int16_t slab_bounce[] = {-87, -120, -82, 0, -41, -56, -38, 0, -15, -20, -14, 0, 0, 0, 0, 0x7fff}; /* 0x801EDA80 */
static const uint8_t back_colour[4] = {0, 0, 0, 0}; /* 0x801EDC74 */
static const SVECTOR screen_normal = {0, 0, -0x1000, 0}; /* 0x801EDC80 */

/* ---- GENERAL.BIN: the slab and the screen lists drawn on it ---- */

/* The slab turns away (x from 0 to a quarter turn), showing what is behind it. */
void Rotate_Slab_Off(const int16_t *rect)
{
    Set_World_Matrix(slab_world_matrix);
    view_angles[0] = 0;
    view_angles[1] = 0;
    view_angles[2] = 0;
    while (view_angles[0] != QUARTER_TURN) {
        view_angles[0] += 0x80;
        Draw_Slab_Frame();
        if (rect)
            Draw_Semi_Trans_Poly(rect[0], rect[1], rect[2], rect[3]);
        Draw_All(1);
    }
}

/* The slab flips in from edge-on, thuds and bounces to rest. */
void Rotate_Slab_On(const int16_t *rect)
{
    Set_World_Matrix(slab_world_matrix);
    view_angles[0] = HALF_TURN + QUARTER_TURN;
    view_angles[1] = 0;
    view_angles[2] = 0;
    while (view_angles[0] != TURN) {
        view_angles[0] += 0x80;
        Draw_Slab_Frame();
        if (rect)
            Draw_Semi_Trans_Poly(rect[0], rect[1], rect[2], rect[3]);
        Draw_All(1);
    }
    Play_Sound(ANY_CHANNEL, sample_bank, SAMPLE_MENU_CONFIRM, SOUND_VOLUME_MAX, PITCH_HALF, 0);
    for (int i = 0; slab_bounce[i] != 0x7fff; i++) {
        view_angles[0] = slab_bounce[i];
        Draw_Slab_Frame();
        if (rect)
            Draw_Semi_Trans_Poly(rect[0], rect[1], rect[2], rect[3]);
        Draw_All(1);
    }
}

void Setup_Screen_Polys(ScreenPoly *list)
{
    screen_poly_list = list;
    DrawSync(0);
    for (int i = 0; i < SCREEN_POLY_MAX && list[i].sprite; i++) {
        Setup_Sprite_Quad(&screen_prims[0]->polys[i], list[i].name, list[i].sprite);
        Setup_Sprite_Quad(&screen_prims[1]->polys[i], list[i].name, list[i].sprite);
    }
}

/* Each sprite rectangle goes onto the slab at z = SCREEN_Z, lit by the slab's light in its
 * mode's colour. A mode the game does not know keeps the previous quad's colour. */
void Draw_Screen_Polys(void)
{
    CVECTOR colour = {0x80, 0x80, 0x80, CODE_POLY_FT4 | CODE_SEMI_TRANS}; /* 0x801EDC88 */
    for (int i = 0; i < SCREEN_POLY_MAX && screen_poly_list[i].sprite; i++) {
        const ScreenPoly *e = &screen_poly_list[i];
        const Sprite *s = e->sprite;
        POLY_FT4 *q = &screen_prims[buffer_num]->polys[i];
        int16_t x = (int16_t)(s->x - SCREEN_HALF_W), y = (int16_t)(s->y - SCREEN_HALF_H);
        int16_t w = (int16_t)(s->w ? s->w - 1 : 0x100), h = (int16_t)(s->h ? s->h - 1 : 0x100);
        SVECTOR v[4] = {
            {x, y, SCREEN_Z, 0},
            {(int16_t)(x + w), y, SCREEN_Z, 0},
            {x, (int16_t)(y + h), SCREEN_Z, 0},
            {(int16_t)(x + w), (int16_t)(y + h), SCREEN_Z, 0},
        };
        gte_ldv3(&v[0], &v[1], &v[2]);
        gte_rtpt();
        gte_stsxy3_xy(q);
        gte_ldv0(&v[3]);
        gte_rtps();
        gte_stsxy_xy3(q);
        switch (e->mode) {
        case SCREEN_GREY:
            colour = (CVECTOR){0x80, 0x80, 0x80, CODE_POLY_FT4};
            break;
        case SCREEN_GREY_TRANS:
            colour = (CVECTOR){0x80, 0x80, 0x80, CODE_POLY_FT4 | CODE_SEMI_TRANS};
            break;
        case SCREEN_HIGHLIGHT:
            colour = highlight_colour;
            colour.cd = CODE_POLY_FT4 | CODE_SEMI_TRANS;
            break;
        case SCREEN_SPRITE_TRANS:
            colour = (CVECTOR){s->r, s->g, s->b, CODE_POLY_FT4 | CODE_SEMI_TRANS};
            break;
        case SCREEN_SPRITE:
            colour = (CVECTOR){s->r, s->g, s->b, CODE_POLY_FT4};
            break;
        default:
            break;
        }
        CVECTOR lit;
        gte_ldv0(&screen_normal);
        gte_ldrgb(&colour);
        gte_nccs();
        gte_strgb(&lit);
        setRGB0(q, lit.r, lit.g, lit.b);
        q->code = lit.cd;
        addPrim(&cdb->ot[e->ot_slot], q);
    }
}

/* Replaces all text with the list's. */
void Setup_Screen_Text(const ScreenText *list)
{
    DrawSync(0);
    Reset_Print_Banks();
    for (; list->text; list++) {
        Print_Font(list->font);
        Print_Ink(list->ink);
        Print_Locate(list->x, list->y, list->z);
        Print(list->ot_slot, list->text);
    }
}

void Setup_Screen_Lines(const ScreenLine *list)
{
    screen_line_list = list;
}

void Draw_Screen_Lines(void)
{
    /* The previous line's shape, for modes past 3 (the original leaves the first undefined). */
    int16_t dx = 0, dy = 0, ox = 0, oy = 0;
    const ScreenLine *e = screen_line_list;
    for (int i = 0; i < SCREEN_LINE_MAX && e->ot_slot >= 0; i++, e++) {
        LINE_F2 *a = &screen_prims[buffer_num]->lines[i * 2];
        LINE_F2 *b = &screen_prims[buffer_num]->lines[i * 2 + 1];
        setLineF2(a);
        setLineF2(b);
        setRGB0(a, e->r, e->g, e->b);
        setRGB0(b, e->r, e->g, e->b);
        if (e->mode == 2) {
            dx = e->length; dy = 0; ox = 0; oy = 1;
        } else if (e->mode < 2) {
            setSemiTrans(a, e->mode != 0);
            setSemiTrans(b, e->mode != 0);
            dx = e->length; dy = 0; ox = 1; oy = 1;
        } else if (e->mode == 3) {
            dx = 0; dy = e->length; ox = 1; oy = 0;
        }
        int16_t x = (int16_t)(e->x - SCREEN_HALF_W), y = (int16_t)(e->y - SCREEN_HALF_H);
        SVECTOR v[4] = {
            {(int16_t)(x + ox), y, SCREEN_Z, 0},
            {(int16_t)(x + dx + ox), (int16_t)(y + dy), SCREEN_Z, 0},
            {x, (int16_t)(y + oy), SCREEN_Z, 0},
            {(int16_t)(x + dx), (int16_t)(y + dy + oy), SCREEN_Z, 0},
        };
        gte_ldv3(&v[0], &v[1], &v[2]);
        gte_rtpt();
        a->x0 = gte.sx[0]; a->y0 = gte.sy[0];
        a->x1 = gte.sx[1]; a->y1 = gte.sy[1];
        b->x0 = gte.sx[2]; b->y0 = gte.sy[2];
        gte_ldv0(&v[3]);
        gte_rtps();
        b->x1 = gte.sx[2]; b->y1 = gte.sy[2];
        addPrim(&cdb->ot[e->ot_slot], a);
        addPrim(&cdb->ot[e->ot_slot], b);
    }
}

/* Keeps the screen up for `frames` frames, and for as long as Cross is held. */
void Button_Pressed(int frames, const int16_t *rect)
{
    if (frames != 2)
        Play_Select_FX();
    for (int n = 0;; n++) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (!(pad.held & PAD_CROSS) && n >= frames)
            break;
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        if (rect)
            Draw_Semi_Trans_Poly(rect[0], rect[1], rect[2], rect[3]);
        Draw_All(1);
    }
}

/* The highlight colour pulses down and up over 64 frames. */
void Glow_Selector(void)
{
    int step = glow_counter < 0x20 ? -4 : 4;
    highlight_colour.r = (u_char)(highlight_colour.r + step);
    highlight_colour.g = (u_char)(highlight_colour.g + step);
    highlight_colour.b = (u_char)(highlight_colour.b + step);
    if (++glow_counter == 0x40)
        glow_counter = 0;
}

/* The slab at its current angles, lit by a light that turns with it. */
void Draw_Slab(void)
{
    Glow_Selector();
    Set_World_Position(slab_world_position);
    Calc_Object_MatrixYXZ(&view_matrix, view_angles);
    MulMatrix0(&slab_light_direction, &view_matrix, &slab_light_matrix);
    SetLightMatrix(&slab_light_matrix);
    SetColorMatrix(&slab_colour_matrix);
    Set_Object_View(&view_matrix, slab_position);
    Update_Object(&slab_object);
}

void Null_Menu_Callback(int forward)
{
    (void)forward;
}

void Draw_Slab_Frame(void)
{
    Debug_Stub();
    Swap_Draw_Buffers();
    Poll_Pad_Buttons();
    Draw_Slab();
    Print_Draw();
    Draw_Screen_Polys();
    Draw_Screen_Lines();
    Draw_Tile(&back_tile[buffer_num], otsize - 1, 0, 0, SCREEN_W, SCREEN_H, back_colour);
}

/* A black semi-transparent panel on the slab, under everything in OT slot 3. */
void Draw_Semi_Trans_Poly(int x, int y, int w, int h)
{
    POLY_F4 *p = &screen_prims[buffer_num]->panel;
    setPolyF4(p);
    setRGB0(p, 0, 0, 0);
    setSemiTrans(p, 1);
    SVECTOR v[4] = {
        {(int16_t)x, (int16_t)y, SCREEN_Z, 0},
        {(int16_t)(x + w), (int16_t)y, SCREEN_Z, 0},
        {(int16_t)x, (int16_t)(y + h), SCREEN_Z, 0},
        {(int16_t)(x + w), (int16_t)(y + h), SCREEN_Z, 0},
    };
    gte_ldv3(&v[0], &v[1], &v[2]);
    gte_rtpt();
    gte_stsxy3_xy(p);
    gte_ldv0(&v[3]);
    gte_rtps();
    gte_stsxy_xy3(p);
    addPrim(&cdb->ot[3], p);
}

/* After a move the highlight restarts from a light grey. */
void Set_Highlight_Grey(void)
{
    glow_counter = -8;
    highlight_colour.r = 0xa0;
    highlight_colour.g = 0xa0;
    highlight_colour.b = 0xa0;
}

void Play_Click_FX(void)
{
    Play_Sound(ANY_CHANNEL, sample_bank, SAMPLE_MENU_CLICK, SOUND_VOLUME_MAX, PITCH_HALF, 0);
}

void Play_Warning_FX(void)
{
    Play_Sound(ANY_CHANNEL, sample_bank, SAMPLE_MENU_WARNING, SOUND_VOLUME_MAX, PITCH_NORMAL, 0);
}

void Play_Select_FX(void)
{
    Play_Sound(ANY_CHANNEL, sample_bank, SAMPLE_MENU_SELECT, SOUND_VOLUME_MAX, PITCH_HALF, 0);
}

/* ---- FRONTEND.BIN: the main menu ---- */

enum {
    SPRITE_DD2L1, SPRITE_DD2L2, SPRITE_RACETYPE, SPRITE_CAR, SPRITE_TRACK, SPRITE_CARD, SPRITE_LINK,
    SPRITE_INFO, SPRITE_CONFIG, SPRITE_GO, SPRITE_RING, SPRITE_RINGS, SPRITE_REDCROSS, SPRITE_PAD3,
    SPRITE_PAD4, SPRITE_TQUICK, SPRITE_TLEFT, SPRITE_TRIGHT, SPRITE_PAD1, SPRITE_TSELECT, SPRITE_4WAY,
    SPRITE_TMOVE, SPRITE_CARD_ERR, FRONTEND_SPRITES
};

/* 0x801D5B18: x, y, colour, semi-transparency, blend, raw */
static Sprite frontend_sprites[FRONTEND_SPRITES] = {
    [SPRITE_DD2L1] = {0, 24, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_DD2L2] = {256, 24, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_RACETYPE] = {32, 96, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_CAR] = {96, 96, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_TRACK] = {160, 96, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_CARD] = {224, 96, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_LINK] = {73, 154, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_INFO] = {137, 154, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_CONFIG] = {201, 154, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_GO] = {262, 192, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_RING] = {31, 94, 0, 0, 0, 0, 1, 1, 1},
    [SPRITE_RINGS] = {OFF_SCREEN, OFF_SCREEN, 0, 0, 0, 0, 1, 1, 1},
    [SPRITE_REDCROSS] = {OFF_SCREEN, OFF_SCREEN, 0x80, 0x80, 0x80, 0, 0, 0, 1},
    [SPRITE_PAD3] = {19, 168, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_PAD4] = {39, 168, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_TQUICK] = {15, 185, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_TLEFT] = {19, 192, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_TRIGHT] = {38, 192, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_PAD1] = {19, 207, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_TSELECT] = {15, 224, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_4WAY] = {39, 199, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_TMOVE] = {43, 224, 0, 0, 0, 0, 0, 0, 1},
    [SPRITE_CARD_ERR] = {96, 96, 0, 0, 0, 0, 0, 0, 1},
};

#define MENU_ITEMS 8

/* The item sprites, as drawn and while pressed. */
static const char *const item_sprites[MENU_ITEMS] = {
    "RACETYPE", "CAR", "TRACK", "CARD", "LINK", "INFO", "CONFIG", "GO"}; /* 0x801D5FE8 */
static const char *const item_pressed_sprites[MENU_ITEMS] = {
    "RACTYPEP", "CARP", "TRACKP", "CARDP", "LINKP", "INFOP", "CONFIGP", "GOP"}; /* 0x801D6008 */

/* 0x801D5D4C; the first MENU_ITEMS are the items, in item order. */
static ScreenPoly frontend_polys[] = {
    {2, "RACETYPE", SCREEN_GREY, &frontend_sprites[SPRITE_RACETYPE]},
    {2, "CAR", SCREEN_GREY, &frontend_sprites[SPRITE_CAR]},
    {2, "TRACK", SCREEN_GREY, &frontend_sprites[SPRITE_TRACK]},
    {2, "CARD", SCREEN_GREY, &frontend_sprites[SPRITE_CARD]},
    {2, "LINK", SCREEN_GREY, &frontend_sprites[SPRITE_LINK]},
    {2, "INFO", SCREEN_GREY, &frontend_sprites[SPRITE_INFO]},
    {2, "CONFIG", SCREEN_GREY, &frontend_sprites[SPRITE_CONFIG]},
    {2, "GO", SCREEN_GREY, &frontend_sprites[SPRITE_GO]},
    {2, "DD2L1", SCREEN_GREY, &frontend_sprites[SPRITE_DD2L1]},
    {2, "DD2L2", SCREEN_GREY, &frontend_sprites[SPRITE_DD2L2]},
    {1, "ZZZZPAD3", SCREEN_GREY_TRANS, &frontend_sprites[SPRITE_PAD3]},
    {1, "ZZZZPAD4", SCREEN_GREY_TRANS, &frontend_sprites[SPRITE_PAD4]},
    {1, "TQUICK", SCREEN_SPRITE_TRANS, &frontend_sprites[SPRITE_TQUICK]},
    {1, "TLEFT", SCREEN_SPRITE_TRANS, &frontend_sprites[SPRITE_TLEFT]},
    {1, "TRIGHT", SCREEN_SPRITE_TRANS, &frontend_sprites[SPRITE_TRIGHT]},
    {1, "ZZZZPAD1", SCREEN_GREY_TRANS, &frontend_sprites[SPRITE_PAD1]},
    {1, "TSELECT", SCREEN_SPRITE_TRANS, &frontend_sprites[SPRITE_TSELECT]},
    {1, "4WAY", SCREEN_GREY, &frontend_sprites[SPRITE_4WAY]},
    {1, "TMOVE", SCREEN_SPRITE_TRANS, &frontend_sprites[SPRITE_TMOVE]},
    {1, "REDCROSS", SCREEN_GREY_TRANS, &frontend_sprites[SPRITE_REDCROSS]},
    {1, "RING", SCREEN_HIGHLIGHT, &frontend_sprites[SPRITE_RING]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &frontend_sprites[SPRITE_RINGS]},
    {0, NULL, 0, NULL},
};

/* 0x801D5EBC: the item's title and its setting, each with a shadow, and two message lines. */
static ScreenText race_title_records[] = {
    {3, 1, GREY_INK, 0, 78, SCREEN_Z, ""},
    {4, 4, GREY_INK, 3, 81, SCREEN_Z, ""},
    {3, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {4, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {1, 2, {0, 0, 0, 0}, 0, -3, SCREEN_Z, ""},
    {1, 2, {0, 0, 0, 0}, 0, 17, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine frontend_lines[] = {{-1, 0, 0, 0, 0, 0, 0, 0}}; /* 0x801D5D40 */

static const char *const race_mode_names[3] = {
    "%R%JC%T/Wrecking Racing", "%R%JC%T/Stock Car", "%R%JC%T/Destruction Derby"}; /* 0x801D6048 */
static const char *const race_type_names[5] = {
    "%R%JC%T/Practice", "%R%JC%T/Time Trials", "%R%JC%T/Total Destruction", "%R%JC%T/Multiplayer",
    "%R%JC%T/Championship"}; /* 0x801D6054 */
static const char *const difficulty_names[3] = {
    "%R%JC%T/Rookie", "%R%JC%T/Amateur", "%R%JC%T/Pro"}; /* 0x801D6068 */
static const char *const track_names[] = {
    "%R%JC%T/Pine Hills Raceway", "%R%JC%T/Chalk Canyon", "%R%JC%T/S.C.A. Motorplex",
    "%R%JC%T/Caprio County Raceway", "%R%JC%T/Black Sail Valley", "%R%JC%T/Liberty City",
    "%R%JC%T/Ultimate Destruction", "%R%JC%T/Red Pike Arena", "%R%JC%T/The Colosseum", "%R%JC%T/The Pit",
    "%R%JC%T/Death Bowl"}; /* 0x801D60B4 */

/* 0x801D6074: what Toggle_Race_Type steps through, race mode and race type. */
static const int32_t race_menu[8][2] = {
    {MODE_WRECKING_RACING, RACE_PRACTICE}, {MODE_WRECKING_RACING, RACE_CHAMPIONSHIP},
    {MODE_WRECKING_RACING, RACE_TIME_TRIAL}, {MODE_STOCK_CAR, RACE_PRACTICE}, {MODE_STOCK_CAR, RACE_CHAMPIONSHIP},
    {MODE_STOCK_CAR, RACE_TIME_TRIAL}, {MODE_DESTRUCTION_DERBY, RACE_PRACTICE},
    {MODE_DESTRUCTION_DERBY, RACE_TOTAL_DESTRUCTION},
};

static void set_titles(const char *title, const char *setting)
{
    race_title_records[0].text = title;
    race_title_records[1].text = title;
    race_title_records[2].text = setting;
    race_title_records[3].text = setting;
    Setup_Screen_Text(race_title_records);
}

void Show_Race_Mode_Title(void)
{
    set_titles(race_mode_names[race_mode], race_type_names[race_type]);
}

void Show_Track_Item_Titles(void)
{
    if (race_type >= RACE_MULTIPLAYER && race_type <= RACE_CHAMPIONSHIP)
        set_titles("%R%JC%T/Select Track", "");
    else
        set_titles("%R%JC%T/Select Track", track_names[race_track]);
}

void Show_Car_Item_Titles(void)
{
    set_titles("%R%JC%T/Select Car", difficulty_names[race_car]);
}

void Show_File_Manager_Title(void)
{
    set_titles("%R%JC%T/Memory card Manager", "");
}

void Show_CD_Player_Title(void)
{
    set_titles("%R%JC%T/CD Audio Player", "");
}

void Show_Information_Title(void)
{
    set_titles("%R%JC%T/Information", "");
}

void Show_Configuration_Title(void)
{
    set_titles("%R%JC%T/Configuration", "");
}

void Show_Go_Title(void)
{
    set_titles("%R%JC%T/Go !", "");
}

/* Steps through race_menu and sets up the race type picked. */
void Toggle_Race_Type(int forward)
{
    if (!forward) {
        if (--race_menu_choice == -1)
            race_menu_choice = 7;
    } else {
        if (++race_menu_choice == 8)
            race_menu_choice = 0;
    }
    race_mode = race_menu[race_menu_choice][0];
    race_type = race_menu[race_menu_choice][1];
    switch (race_type) {
    case RACE_PRACTICE:
        if (race_mode == MODE_DESTRUCTION_DERBY)
            Select_DDPract();
        else
            Select_Pract();
        break;
    case RACE_TIME_TRIAL:
        Select_TimeT();
        break;
    case RACE_TOTAL_DESTRUCTION:
        Select_Total();
        break;
    case RACE_CHAMPIONSHIP:
        Select_ChampQS();
        snprintf(player_names[0], sizeof(player_names[0]), "Player 1");
        snprintf(player_names[1], sizeof(player_names[1]), "Player 2");
        break;
    default:
        break;
    }
}

/* Race tracks wrap within the races (0 .. race_track_count - 1) or within the arenas (7 ..). */
void Toggle_Track(int forward)
{
    if (race_type == RACE_CHAMPIONSHIP || race_type == RACE_MULTIPLAYER)
        return;
    if (!forward) {
        if (race_mode == MODE_DESTRUCTION_DERBY) {
            if (race_track == 7)
                race_track = arena_count + 6;
            else
                race_track--;
        } else {
            if (race_track == 0)
                race_track = race_track_count;
            race_track--;
        }
    } else if (race_mode == MODE_DESTRUCTION_DERBY) {
        if (race_track == arena_count + 6)
            race_track = 7;
        else
            race_track++;
    } else if (race_track == race_track_count - 1) {
        race_track = 0;
    } else {
        race_track++;
    }
}

void Toggle_Car(int forward)
{
    if (!forward)
        race_car = race_car == 0 ? 2 : race_car - 1;
    else
        race_car = race_car == 2 ? 0 : race_car + 1;
}

/* In the championship types there is no track to pick: moving onto TRACK goes past it. */
int Remap_Team_Race_Menu_Item(int item, int base)
{
    if (item == 2 && (race_type == RACE_CHAMPIONSHIP || race_type == RACE_MULTIPLAYER))
        item = base + 2;
    return item;
}

/* Shown while the memory cards are searched for a configuration. */
void Show_Searching_For_Config(void)
{
    POLY_FT4 quads[2];
    frontend_sprites[SPRITE_CARD_ERR].y = (int16_t)(pal_mode * 8 + 0x60);
    Setup_Sprite_Quad(&quads[0], "CARD_ERR", &frontend_sprites[SPRITE_CARD_ERR]);
    Setup_Sprite_Quad(&quads[1], "CARD_ERR", &frontend_sprites[SPRITE_CARD_ERR]);
    race_title_records[5].text = "%R%JC%T/Please Wait..";
    race_title_records[4].text = "%R%JC%T/Searching For Config";
    Setup_Screen_Text(race_title_records);
    for (int n = 0;; n++) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (n == 2)
            break;
        addPrim(&cdb->ot[2], &quads[buffer_num]);
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
    DrawSync(0);
    race_title_records[4].text = "";
    race_title_records[5].text = "";
    Setup_Screen_Text(race_title_records);
}

int Select_Pract(void)
{
    num_cars = 20;
    human_players = 1;
    race_type = RACE_PRACTICE;
    Restrict_Race_Track_Group(0);
    return 1;
}

int Select_TimeT(void)
{
    num_cars = 1;
    human_players = 1;
    race_type = RACE_TIME_TRIAL;
    Restrict_Race_Track_Group(0);
    return 1;
}

int Select_Total(void)
{
    num_cars = 20;
    human_players = 1;
    race_type = RACE_TOTAL_DESTRUCTION;
    Restrict_Race_Track_Group(1);
    return 1;
}

int Select_DDPract(void)
{
    num_cars = 20;
    human_players = 1;
    race_type = RACE_PRACTICE;
    Restrict_Race_Track_Group(1);
    return 1;
}

int Select_ChampQS(void)
{
    snprintf(player_names[0], sizeof(player_names[0]), "Player");
    num_cars = 20;
    human_players = 1;
    race_type = RACE_CHAMPIONSHIP;
    race_track = 0;
    return 1;
}

/* Races are tracks 0..6, arenas 7 on. */
void Restrict_Race_Track_Group(int arenas)
{
    if (!arenas) {
        if (race_track > 6)
            race_track = 0;
    } else if (race_track < 7) {
        race_track = 7;
    }
}


/* 0x801D5F48 */
static const MenuItem menu_items[MENU_ITEMS] = {
    {0, 31, 94, Show_Race_Mode_Title, Choose_Race_Mode, Toggle_Race_Type},
    {0, 95, 94, Show_Car_Item_Titles, Car_Select_Screen, Toggle_Car},
    {0, 159, 94, Show_Track_Item_Titles, Run_Track_Select, Toggle_Track},
    {0, 223, 94, Show_File_Manager_Title, Memory_Card_Screen, Null_Menu_Callback},
    {1, 72, 152, Show_CD_Player_Title, CD_Player_Screen, Null_Menu_Callback},
    {1, 136, 152, Show_Information_Title, Run_Info_Menu, Null_Menu_Callback},
    {1, 200, 152, Show_Configuration_Title, Config_Menu, Null_Menu_Callback},
    {1, 262, 190, Show_Go_Title, Select_Go_Start_Race, Null_Menu_Callback},
};

/* 0x801D6028: the item reached by left, up, right and down. */
static const uint8_t menu_moves[MENU_ITEMS][4] = {
    {0, 0, 1, 4}, {0, 1, 2, 5}, {1, 2, 3, 6}, {2, 3, 3, 6},
    {4, 0, 5, 7}, {4, 1, 6, 7}, {5, 2, 6, 7}, {7, 6, 7, 7},
};

static void place_ring(int item)
{
    Sprite *ring = &frontend_sprites[SPRITE_RING], *rings = &frontend_sprites[SPRITE_RINGS];
    if (menu_items[item].row == 0) {
        ring->x = menu_items[item].x;
        ring->y = menu_items[item].y;
        rings->x = rings->y = OFF_SCREEN;
    } else if (menu_items[item].row == 1) {
        ring->x = ring->y = OFF_SCREEN;
        rings->x = menu_items[item].x;
        rings->y = menu_items[item].y;
    }
}

static void show_front_end(int item)
{
    menu_items[item].caption();
    Setup_Screen_Polys(frontend_polys);
    Setup_Screen_Text(race_title_records);
    Setup_Screen_Lines(frontend_lines);
}

/* The main menu: eight items on the slab. Arrows move the ring, Square and Circle change the
 * race type, car or track in place, Cross opens an item's screen. Idle for 30 seconds, it shows
 * the demo. Never returns. */
void Front_End(void)
{
    int item = 0;
    bool changed = false, toggle = false;
    int forward = 0;
    int idle = DEMO_DELAY;

    Show_Camcorder_Loading_Screen();
    race_type = RACE_PRACTICE;
    race_mode = MODE_WRECKING_RACING;
    race_track = 0;
    race_car = 0;
    num_cars = 20;
    human_players = 1;
    race_cutoff = 1;
    show_front_end(0);
    Set_CD_Track(FRONT_END_CD_TRACK, 1);
    Rotate_Slab_On(NULL);
    Start_CD_Audio();
    restart_cd_audio = 0;
    Show_Searching_For_Config();
    Load_First_Config();
    show_front_end(0);

    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = Remap_Team_Race_Menu_Item(menu_moves[item][0], -1);
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = Remap_Team_Race_Menu_Item(menu_moves[item][2], 1);
            changed = true;
        } else if (pad.pressed & PAD_UP) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = Remap_Team_Race_Menu_Item(menu_moves[item][1], 1);
            changed = true;
        } else if (pad.pressed & PAD_DOWN) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = menu_moves[item][3];
            changed = true;
        } else if (pad.pressed & PAD_SQUARE) {
            forward = 0;
            toggle = true;
        } else if (pad.pressed & PAD_CIRCLE) {
            forward = 1;
            toggle = true;
        } else if (pad.pressed & PAD_CROSS) {
            frontend_polys[item].name = item_pressed_sprites[item];
            Setup_Screen_Polys(frontend_polys);
            Button_Pressed(7, NULL);
            frontend_polys[item].name = item_sprites[item];
            Setup_Screen_Polys(frontend_polys);
            Button_Pressed(2, NULL);
            Rotate_Slab_Off(NULL);
            menu_items[item].enter();
            place_ring(item);
            changed = true;
            menu_items[item].caption();
            Setup_Screen_Polys(frontend_polys);
            Setup_Screen_Lines(frontend_lines);
            if (restart_cd_audio)
                Set_CD_Track(FRONT_END_CD_TRACK, 1);
            Rotate_Slab_On(NULL);
            if (restart_cd_audio)
                Start_CD_Audio();
            restart_cd_audio = 0;
            continue;
        }

        /* The red cross over the track item when the race type has no track choice. */
        Sprite *cross = &frontend_sprites[SPRITE_REDCROSS];
        if (race_type == RACE_CHAMPIONSHIP || race_type == RACE_MULTIPLAYER) {
            cross->x = 0xb1;
            cross->y = 0x6f;
        } else {
            cross->x = cross->y = OFF_SCREEN;
        }

        bool toggled = toggle;
        toggle = false;
        if (toggled && item < 3) {
            Play_Select_FX();
            frontend_polys[item].name = item_pressed_sprites[item];
            Setup_Screen_Polys(frontend_polys);
            Button_Pressed(7, NULL);
            frontend_polys[item].name = item_sprites[item];
            menu_items[item].toggle(forward);
            menu_items[item].caption();
            changed = true;
            continue;
        }
        if (changed) {
            changed = false;
            idle = DEMO_DELAY;
            place_ring(item);
            Setup_Screen_Polys(frontend_polys);
            menu_items[item].caption();
        }
        if (--idle == 0) {
            DrawSync(0);
            Rotate_Slab_Off(NULL);
            Loading_Screen_From_Slab(NULL);
            idle = DemoMode() == 0 ? DEMO_RETRY : DEMO_DELAY;
            DrawSync(0);
            Show_Camcorder_Loading_Screen();
            show_front_end(item);
            Set_CD_Track(FRONT_END_CD_TRACK, 1);
            Rotate_Slab_On(NULL);
            Start_CD_Audio();
            restart_cd_audio = 0;
            changed = true;
        } else {
            Draw_Slab();
            Print_Draw();
            Draw_Screen_Polys();
            Draw_Screen_Lines();
            Draw_All(1);
        }
    }
}

/* ---- FRONTEND.BIN: race mode and race type ---- */


/* A slab menu: its lists (the items' quads first), the ring, the items and where left and
 * right lead from each. */
typedef struct {
    ScreenPoly *polys;
    ScreenText *text;
    const ScreenLine *lines;
    Sprite *ring;
    const SubMenuItem *items;
    const uint8_t (*moves)[4];   /* left, (unused), right, (unused) */
    const char *const *sprites;
    const char *const *pressed_sprites;
} SubMenu;

/* The loop Choose_Race_Mode, Choose_Race_Type and Choose_DD_Race_Type each have: left and right
 * move the ring, Cross runs the item, Triangle backs out. Returns 1 when an item's select
 * finished the selection, 0 when backed out. */
static int run_sub_menu(const SubMenu *m)
{
    int item = 0;
    bool changed = false;
    m->ring->x = m->items[0].x;
    m->ring->y = m->items[0].y;
    m->items[0].caption();
    Setup_Screen_Polys(m->polys);
    Setup_Screen_Text(m->text);
    Setup_Screen_Lines(m->lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = m->moves[item][0];
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = m->moves[item][2];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            m->polys[item].name = m->pressed_sprites[item];
            Setup_Screen_Polys(m->polys);
            Button_Pressed(7, NULL);
            m->polys[item].name = m->sprites[item];
            Setup_Screen_Polys(m->polys);
            Button_Pressed(2, NULL);
            Rotate_Slab_Off(NULL);
            if (m->items[item].select())
                return 1;
            changed = true;
            m->items[item].caption();
            Setup_Screen_Polys(m->polys);
            Setup_Screen_Text(m->text);
            Setup_Screen_Lines(m->lines);
            Rotate_Slab_On(NULL);
            continue;
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(NULL);
            return 0;
        }
        if (changed) {
            changed = false;
            m->ring->x = m->items[item].x;
            m->ring->y = m->items[item].y;
            Setup_Screen_Polys(m->polys);
            m->items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

enum {
    RM_WRECKIN, RM_STOCK, RM_DD, RM_RING, RM_PAD2, RM_TCANCEL, RM_PAD1, RM_TSELECT, RM_SIDEONLY,
    RM_TMOVE, RM_IRACMODE, RACE_MODE_SPRITES
};

static Sprite race_mode_sprites[RACE_MODE_SPRITES] = { /* 0x801D7720 */
    [RM_WRECKIN] = BUTTON_SPRITE(32, 96),
    [RM_STOCK] = BUTTON_SPRITE(128, 96),
    [RM_DD] = BUTTON_SPRITE(224, 96),
    [RM_RING] = TRANS_SPRITE(31, 94),
    [RM_PAD2] = BUTTON_SPRITE(260, 24),
    [RM_TCANCEL] = BUTTON_SPRITE(278, 29),
    [RM_PAD1] = BUTTON_SPRITE(260, 41),
    [RM_TSELECT] = BUTTON_SPRITE(278, 46),
    [RM_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [RM_TMOVE] = BUTTON_SPRITE(282, 67),
    [RM_IRACMODE] = BUTTON_SPRITE(16, 24),
};

static ScreenPoly race_mode_polys[] = { /* 0x801D7828 */
    {2, "WRECKIN", SCREEN_GREY, &race_mode_sprites[RM_WRECKIN]},
    {2, "STOCK", SCREEN_GREY, &race_mode_sprites[RM_STOCK]},
    {2, "DD", SCREEN_GREY, &race_mode_sprites[RM_DD]},
    {2, "IRACMODE", SCREEN_GREY, &race_mode_sprites[RM_IRACMODE]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &race_mode_sprites[RM_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &race_mode_sprites[RM_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &race_mode_sprites[RM_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &race_mode_sprites[RM_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &race_mode_sprites[RM_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &race_mode_sprites[RM_TMOVE]},
    {1, "RING", SCREEN_HIGHLIGHT, &race_mode_sprites[RM_RING]},
    {0, NULL, 0, NULL},
};

static ScreenText race_mode_text[] = { /* 0x801D7924 */
    {1, 0, TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Race Mode"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Race Mode"},
    {1, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine race_mode_lines[] = { /* 0x801D78E8 */
    {1, 54, 52, 134, 0xc8, 0xff, 0xff, 0},
    {2, 57, 55, 134, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_race_mode_caption(const char *caption)
{
    race_mode_text[2].text = caption;
    race_mode_text[3].text = caption;
    Setup_Screen_Text(race_mode_text);
}

void Show_Wrecking_Racing_Caption(void)
{
    set_race_mode_caption("%R%JC%T/Wrecking Racing");
}

void Show_Stock_Car_Caption(void)
{
    set_race_mode_caption("%R%JC%T/Stock Car");
}

void Show_Destruction_Derby_Caption(void)
{
    set_race_mode_caption("%R%JC%T/Destruction Derby");
}

int Select_Wrecking(void)
{
    int done = Choose_Race_Type();
    if (done)
        race_mode = MODE_WRECKING_RACING;
    return done != 0;
}

int Select_Stock(void)
{
    int done = Choose_Race_Type();
    if (done)
        race_mode = MODE_STOCK_CAR;
    return done != 0;
}

int Select_DD(void)
{
    int done = Choose_DD_Race_Type();
    if (done)
        race_mode = MODE_DESTRUCTION_DERBY;
    return done != 0;
}

static const SubMenuItem race_mode_items[3] = { /* 0x801D798C */
    {31, 94, Show_Wrecking_Racing_Caption, Select_Wrecking, Null_Menu_Callback, 0},
    {129, 94, Show_Stock_Car_Caption, Select_Stock, Null_Menu_Callback, 0},
    {225, 94, Show_Destruction_Derby_Caption, Select_DD, Null_Menu_Callback, 0x10000},
};

static const uint8_t race_mode_moves[3][4] = {{0, 0, 1, 0}, {0, 1, 2, 1}, {1, 2, 2, 2}}; /* 0x801D79C4 */
static const char *const race_mode_item_sprites[3] = {"WRECKIN", "STOCK", "DD"}; /* 0x801D790C */
static const char *const race_mode_pressed_sprites[3] = {"WRECKINP", "STOCKP", "DDP"}; /* 0x801D7918 */

/* Wrecking racing, stock car or destruction derby, then the race type for it. Returns 1 when a
 * race was chosen. */
int Choose_Race_Mode(void)
{
    static const SubMenu menu = {
        race_mode_polys, race_mode_text, race_mode_lines, &race_mode_sprites[RM_RING],
        race_mode_items, race_mode_moves, race_mode_item_sprites, race_mode_pressed_sprites,
    };
    return run_sub_menu(&menu);
}

enum {
    RT_CHAMP, RT_PRACTICE, RT_TMETRIAL, RT_MULTI, RT_RING, RT_PAD2, RT_TCANCEL, RT_PAD1, RT_TSELECT,
    RT_IRACMODE, RT_SIDEONLY, RT_TMOVE, RT_DDPRACT, RT_TOTLDEST, RACE_TYPE_SPRITES
};

static Sprite race_type_sprites[RACE_TYPE_SPRITES] = { /* 0x801D7C58 */
    [RT_CHAMP] = BUTTON_SPRITE(32, 96),
    [RT_PRACTICE] = BUTTON_SPRITE(96, 96),
    [RT_TMETRIAL] = BUTTON_SPRITE(160, 96),
    [RT_MULTI] = TRANS_SPRITE(224, 96),
    [RT_RING] = TRANS_SPRITE(31, 94),
    [RT_PAD2] = BUTTON_SPRITE(260, 24),
    [RT_TCANCEL] = BUTTON_SPRITE(278, 29),
    [RT_PAD1] = BUTTON_SPRITE(260, 41),
    [RT_TSELECT] = BUTTON_SPRITE(278, 46),
    [RT_IRACMODE] = BUTTON_SPRITE(16, 24),
    [RT_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [RT_TMOVE] = BUTTON_SPRITE(282, 67),
    [RT_DDPRACT] = BUTTON_SPRITE(64, 96),   /* 0x801D7F68 */
    [RT_TOTLDEST] = TRANS_SPRITE(192, 96),
};

static ScreenPoly race_type_polys[] = { /* 0x801D7D90 */
    {2, "CHAMP", SCREEN_GREY, &race_type_sprites[RT_CHAMP]},
    {2, "PRACTICE", SCREEN_GREY, &race_type_sprites[RT_PRACTICE]},
    {2, "TMETRIAL", SCREEN_GREY, &race_type_sprites[RT_TMETRIAL]},
    {2, "MULTI", SCREEN_GREY, &race_type_sprites[RT_MULTI]},
    {2, "IRACMODE", SCREEN_GREY, &race_type_sprites[RT_IRACMODE]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &race_type_sprites[RT_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &race_type_sprites[RT_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &race_type_sprites[RT_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &race_type_sprites[RT_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &race_type_sprites[RT_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &race_type_sprites[RT_TMOVE]},
    {1, "RING", SCREEN_HIGHLIGHT, &race_type_sprites[RT_RING]},
    {0, NULL, 0, NULL},
};

static ScreenPoly dd_race_type_polys[] = { /* 0x801D7F98 */
    {2, "DDPRACT", SCREEN_GREY, &race_type_sprites[RT_DDPRACT]},
    {2, "TOTLDEST", SCREEN_GREY, &race_type_sprites[RT_TOTLDEST]},
    {2, "IRACMODE", SCREEN_GREY, &race_type_sprites[RT_IRACMODE]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &race_type_sprites[RT_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &race_type_sprites[RT_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &race_type_sprites[RT_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &race_type_sprites[RT_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &race_type_sprites[RT_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &race_type_sprites[RT_TMOVE]},
    {1, "RING", SCREEN_HIGHLIGHT, &race_type_sprites[RT_RING]},
    {0, NULL, 0, NULL},
};

static ScreenText race_type_text[] = { /* 0x801D7EA4 */
    {1, 0, TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Race Type"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Race Type"},
    {1, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine race_type_lines[] = { /* 0x801D7E60 */
    {1, 54, 52, 129, 0xc8, 0xff, 0xff, 0},
    {2, 57, 55, 129, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_race_type_caption(const char *caption)
{
    race_type_text[2].text = caption;
    race_type_text[3].text = caption;
    Setup_Screen_Text(race_type_text);
}

void Show_Championship_Caption(void)
{
    set_race_type_caption("%R%JC%T/Championship");
}

void Show_Practice_Caption(void)
{
    set_race_type_caption("%R%JC%T/Practice");
}

void Show_Time_Trials_Caption(void)
{
    set_race_type_caption("%R%JC%T/Time Trials");
}

void Show_Multiplayer_Caption(void)
{
    set_race_type_caption("%R%JC%T/Multiplayer");
}

void Show_Total_Destruction_Caption(void)
{
    set_race_type_caption("%R%JC%T/Total Destruction");
}

/* A championship for one player; the name entry returns the season length. */
int Select_Champ(void)
{
    int races = Enter_Driver_Names(1, 1);
    if (races) {
        num_cars = 20;
        race_type = RACE_CHAMPIONSHIP;
        race_track = 0;
        human_players = races;
    }
    return races;
}

/* A championship for up to ten players taking turns. */
int Select_Multi(void)
{
    int races = Enter_Driver_Names(1, 10);
    if (races) {
        num_cars = 20;
        race_type = RACE_MULTIPLAYER;
        race_track = 0;
        human_players = races;
    }
    return races;
}

static const SubMenuItem race_type_items[4] = { /* 0x801D7F0C */
    {31, 94, Show_Championship_Caption, Select_Champ, Null_Menu_Callback, 0},
    {95, 94, Show_Practice_Caption, Select_Pract, Null_Menu_Callback, 0},
    {159, 94, Show_Time_Trials_Caption, Select_TimeT, Null_Menu_Callback, 0},
    {223, 94, Show_Multiplayer_Caption, Select_Multi, Null_Menu_Callback, 0x10000},
};

static const uint8_t race_type_moves[4][4] = {{0, 0, 1, 0}, {0, 1, 2, 1}, {1, 2, 3, 2}, {2, 3, 3, 3}}; /* 0x801D7F58 */
static const char *const race_type_item_sprites[4] = {"CHAMP", "PRACTICE", "TMETRIAL", "MULTI"}; /* 0x801D7E84 */
static const char *const race_type_pressed_sprites[4] = {"CHAMPP", "PRACTICP", "TMETRILP", "MULTIP"}; /* 0x801D7E94 */

/* Championship, practice, time trials or multiplayer, for wrecking racing and stock car. */
int Choose_Race_Type(void)
{
    static const SubMenu menu = {
        race_type_polys, race_type_text, race_type_lines, &race_type_sprites[RT_RING],
        race_type_items, race_type_moves, race_type_item_sprites, race_type_pressed_sprites,
    };
    return run_sub_menu(&menu);
}

static const SubMenuItem dd_race_type_items[2] = { /* 0x801D805C */
    {63, 94, Show_Practice_Caption, Select_DDPract, Null_Menu_Callback, 0},
    {191, 94, Show_Total_Destruction_Caption, Select_Total, Null_Menu_Callback, 0x10000},
};

static const uint8_t dd_race_type_moves[2][4] = {{0, 0, 1, 0}, {0, 1, 1, 1}}; /* 0x801D8080 */
static const char *const dd_race_type_item_sprites[2] = {"DDPRACT", "TOTLDEST"}; /* 0x801D8048 */
static const char *const dd_race_type_pressed_sprites[2] = {"DDPRACTP", "TOTLDSTP"}; /* 0x801D8050 */

/* Practice or total destruction in the arenas. */
int Choose_DD_Race_Type(void)
{
    static const SubMenu menu = {
        dd_race_type_polys, race_type_text, race_type_lines, &race_type_sprites[RT_RING],
        dd_race_type_items, dd_race_type_moves, dd_race_type_item_sprites, dd_race_type_pressed_sprites,
    };
    return run_sub_menu(&menu);
}

/* ---- FRONTEND.BIN: track select ---- */

#define TRACK_LEVELS 11

/* The level of each race track (0x80070FF0). */
const int32_t track_levels[TRACK_LEVELS] = {1, 2, 7, 5, 3, 6, 4, 10, 8, 9, 11};

enum {
    TS_ITRACK, TS_PAD2, TS_TCANCEL, TS_PAD1, TS_TACCEPT, TS_TRACKPIC, TS_TRACKLOG, TS_SIDEONLY, TS_TMOVE,
    TRACK_SELECT_SPRITES
};

static Sprite track_select_sprites[TRACK_SELECT_SPRITES] = { /* 0x801D8088 */
    [TS_ITRACK] = BUTTON_SPRITE(16, 24),
    [TS_PAD2] = BUTTON_SPRITE(260, 24),
    [TS_TCANCEL] = BUTTON_SPRITE(278, 29),
    [TS_PAD1] = BUTTON_SPRITE(260, 41),
    [TS_TACCEPT] = BUTTON_SPRITE(278, 46),
    [TS_TRACKPIC] = BUTTON_SPRITE(16, 85),
    [TS_TRACKLOG] = BUTTON_SPRITE(176, 95),
    [TS_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [TS_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly track_select_polys[] = { /* 0x801D8184 */
    {3, "ITRACK", SCREEN_GREY, &track_select_sprites[TS_ITRACK]},
    {3, "TRACKPIC", SCREEN_GREY, &track_select_sprites[TS_TRACKPIC]},
    {3, "TRACKLOG", SCREEN_GREY, &track_select_sprites[TS_TRACKLOG]},
    {3, "ZZZZPAD1", SCREEN_GREY_TRANS, &track_select_sprites[TS_PAD1]},
    {3, "TACCEPT", SCREEN_GREY_TRANS, &track_select_sprites[TS_TACCEPT]},
    {3, "ZZZZPAD2", SCREEN_GREY_TRANS, &track_select_sprites[TS_PAD2]},
    {3, "TCANCEL", SCREEN_GREY_TRANS, &track_select_sprites[TS_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &track_select_sprites[TS_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &track_select_sprites[TS_TMOVE]},
    {0, NULL, 0, NULL},
};

static ScreenText track_select_text[] = { /* 0x801D8224 */
    {1, 0, TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Track Select"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Track Select"},
    {1, 1, TITLE_INK, 0, 66, SCREEN_Z, ""},
    {2, 4, {0, 0, 0, 0}, 3, 69, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine track_select_lines[] = { /* 0x801D8160 */
    {1, 54, 52, 158, 0xc8, 0xff, 0xff, 0},
    {2, 57, 55, 158, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* Where the TRACKLOG and TRACKPIC sprites are in VRAM, and each level's picture and logo CLUTs
 * (the TRACKnn and TRACKnnL sprites). */
static struct {
    int16_t x, y, clut_x, clut_y;
} track_logo_area, track_picture_area;          /* 0x801D8D40 */
static struct {
    int16_t picture_clut_x, picture_clut_y, logo_clut_x, logo_clut_y;
} track_level_cluts[TRACK_LEVELS];              /* 0x801D8D50 */
static int32_t track_select_level;              /* 0x801D8DA8 */
static int32_t entry_race_track;                /* 0x801D8DAC */
static int32_t track_locked;                    /* 0x801D87B0 */

/* LEV0\TRACKS.BIN: per level two offsets, to a 64x80-word picture and a 64x64-word logo. */
static const u_long *track_image(int level, int which)
{
    int32_t offset;
    memcpy(&offset, track_data + level * 8 + which * 4, sizeof(offset));
    return (const u_long *)(track_data + offset);
}

/* The selected track's picture and logo into the TRACKPIC and TRACKLOG sprites, with their CLUTs. */
void Show_Track_Picture(void)
{
    track_select_level = track_levels[race_track];
    const u_long *picture = track_image(track_select_level, 0);
    const u_long *logo = track_image(track_select_level, 1);
    const int level = track_select_level - 1;
    DrawSync(0);
    RECT area = {track_picture_area.x, track_picture_area.y, 0x40, 0x50};
    LoadImage(&area, (u_long *)picture);
    DrawSync(0);
    area = (RECT){track_level_cluts[level].picture_clut_x, track_level_cluts[level].picture_clut_y, 0x100, 1};
    MoveImage(&area, track_picture_area.clut_x, track_picture_area.clut_y);
    DrawSync(0);
    area = (RECT){track_logo_area.x, track_logo_area.y, 0x40, 0x40};
    LoadImage(&area, (u_long *)logo);
    DrawSync(0);
    area = (RECT){track_level_cluts[level].logo_clut_x, track_level_cluts[level].logo_clut_y, 0x100, 1};
    MoveImage(&area, track_logo_area.clut_x, track_logo_area.clut_y);
    DrawSync(0);
}

void Set_Track_Picture_Names(void)
{
    const SpriteInfo *s = Search_For_Sprite("TRACKLOG");
    track_logo_area.x = s->vram_x;
    track_logo_area.y = s->vram_y;
    track_logo_area.clut_x = s->clut_x;
    track_logo_area.clut_y = s->clut_y;
    s = Search_For_Sprite("TRACKPIC");
    track_picture_area.x = s->vram_x;
    track_picture_area.y = s->vram_y;
    track_picture_area.clut_x = s->clut_x;
    track_picture_area.clut_y = s->clut_y;
    for (int i = 0; i < TRACK_LEVELS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "TRACK%02d", i + 1);
        s = Search_For_Sprite(name);
        track_level_cluts[i].picture_clut_x = s->clut_x;
        track_level_cluts[i].picture_clut_y = s->clut_y;
        snprintf(name, sizeof(name), "TRACK%02dL", i + 1);
        s = Search_For_Sprite(name);
        track_level_cluts[i].logo_clut_x = s->clut_x;
        track_level_cluts[i].logo_clut_y = s->clut_y;
    }
    Show_Track_Picture();
}

/* Like Toggle_Track over all seven races or the four arenas, then whether the track is still
 * locked (past race_track_count or arena_count) and the hint for unlocking it. */
void Toggle_Track_With_Unlocks(int forward)
{
    if (race_type != RACE_CHAMPIONSHIP && race_type != RACE_MULTIPLAYER) {
        if (!forward) {
            if (race_mode == MODE_DESTRUCTION_DERBY)
                race_track = race_track == 7 ? 10 : race_track - 1;
            else
                race_track = race_track == 0 ? 6 : race_track - 1;
        } else if (race_mode == MODE_DESTRUCTION_DERBY) {
            race_track = race_track == 10 ? 7 : race_track + 1;
        } else {
            race_track = race_track == 6 ? 0 : race_track + 1;
        }
    }
    if (race_mode == MODE_DESTRUCTION_DERBY)
        track_locked = race_track >= arena_count + 7;
    else
        track_locked = race_track >= race_track_count;
    const char *hint = "";
    if (track_locked == 1) {
        if (race_track == 4 || race_track == 8)
            hint = "%R%JC%T/Reach division 3 to unlock !";
        else if (race_track == 5 || race_track == 9)
            hint = "%R%JC%T/Reach division 2 to unlock !";
        else if (race_track == 6 || race_track == 10)
            hint = "%R%JC%T/Reach division 1 to unlock !";
    }
    track_select_text[2].text = hint;
    track_select_text[3].text = hint;
}

int Step_Track_Select_Next(void)
{
    Toggle_Track_With_Unlocks(1);
    Show_Track_Picture();
    return 0;
}

int Step_Track_Select_Prev(void)
{
    Toggle_Track_With_Unlocks(0);
    Show_Track_Picture();
    return 0;
}

/* Left and right step the track (showing locked ones with a hint), Cross takes an unlocked
 * one, Triangle puts the old one back. Returns 1 when a track was picked. */
int Run_Track_Select(void)
{
    bool changed = false;
    int result;
    entry_race_track = race_track;
    track_select_level = track_levels[race_track];
    track_select_text[2].text = "";
    track_select_text[3].text = "";
    Set_Track_Picture_Names();
    Setup_Screen_Text(track_select_text);
    Setup_Screen_Polys(track_select_polys);
    Setup_Screen_Lines(track_select_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Step_Track_Select_Prev();
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Step_Track_Select_Next();
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            if (track_locked != 1) {
                result = 1;
                break;
            }
            Play_Warning_FX();
        } else if (pad.pressed & PAD_TRIANGLE) {
            race_track = entry_race_track;
            result = 0;
            break;
        }
        if (changed) {
            changed = false;
            Setup_Screen_Text(track_select_text);
            Setup_Screen_Polys(track_select_polys);
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
    Rotate_Slab_Off(NULL);
    return result;
}

/* ---- FRONTEND.BIN: CD player ---- */

#define CD_PLAYER_TRACKS 18

enum {
    CP_LASTTRCK, CP_PLAY, CP_STOP, CP_NEXTTRCK, CP_ILOAD, CP_PAD2, CP_TCANCEL, CP_PAD1, CP_TSELECT, CP_RINGS,
    CP_SIDEONLY, CP_TMOVE, CD_PLAYER_SPRITES
};

static Sprite cd_player_sprites[CD_PLAYER_SPRITES] = { /* 0x801D649C */
    [CP_LASTTRCK] = BUTTON_SPRITE(118, 192),
    [CP_PLAY] = BUTTON_SPRITE(166, 192),
    [CP_STOP] = BUTTON_SPRITE(214, 192),
    [CP_NEXTTRCK] = BUTTON_SPRITE(262, 192),
    [CP_ILOAD] = BUTTON_SPRITE(16, 24),
    [CP_PAD2] = BUTTON_SPRITE(260, 24),
    [CP_TCANCEL] = BUTTON_SPRITE(278, 29),
    [CP_PAD1] = BUTTON_SPRITE(260, 41),
    [CP_TSELECT] = BUTTON_SPRITE(278, 46),
    [CP_RINGS] = TRANS_SPRITE(261, 191),
    [CP_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [CP_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly cd_player_polys[] = { /* 0x801D66BC */
    {3, "LASTTRCK", SCREEN_GREY, &cd_player_sprites[CP_LASTTRCK]},
    {3, "PLAY", SCREEN_GREY, &cd_player_sprites[CP_PLAY]},
    {3, "STOP", SCREEN_GREY, &cd_player_sprites[CP_STOP]},
    {3, "NEXTTRCK", SCREEN_GREY, &cd_player_sprites[CP_NEXTTRCK]},
    {2, "ILOAD", SCREEN_GREY, &cd_player_sprites[CP_ILOAD]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &cd_player_sprites[CP_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &cd_player_sprites[CP_TCANCEL]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &cd_player_sprites[CP_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &cd_player_sprites[CP_TSELECT]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &cd_player_sprites[CP_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &cd_player_sprites[CP_TMOVE]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &cd_player_sprites[CP_RINGS]},
    {0, NULL, 0, NULL},
};

static char cd_track_text[32]; /* 0x801D8B80, "Track N - " */

#define WHITE_INK {0xff, 0xff, 0xff, 0}

static ScreenText cd_player_text[] = { /* 0x801D65BC */
    {2, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/CD Player"},
    {3, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/CD Player"},
    {3, 1, GREY_INK, -140, 91, SCREEN_Z, ""},
    {4, 4, GREY_INK, -137, 95, SCREEN_Z, ""},
    {3, 1, GREY_INK, -90, -30, SCREEN_Z, cd_track_text},
    {3, 1, GREY_INK, -90, -10, SCREEN_Z, "%R%JL%T/Title -"},
    {3, 1, GREY_INK, -90, 10, SCREEN_Z, "%R%JL%T/Group -"},
    {4, 5, WHITE_INK, -20, -30, SCREEN_Z, "%R%JL%T/Liberty City"},
    {4, 5, WHITE_INK, -20, -10, SCREEN_Z, "%R%JL%T/Pork Sword"},
    {4, 5, WHITE_INK, -20, 10, SCREEN_Z, "%R%JL%T/Jug"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine cd_player_lines[] = { /* 0x801D6698 */
    {2, 54, 52, 123, 0xff, 0xc8, 0xc8, 0},
    {3, 57, 55, 123, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* Per music track: where it plays, its title and which band recorded it. */
static const char *const cd_track_uses[CD_PLAYER_TRACKS] = { /* 0x801D63C4 */
    "%R%JL%T/Pine Hills Raceway", "%R%JL%T/Chalk Canyon", "%R%JL%T/Black Sail Valley",
    "%R%JL%T/Ultimate Destruction", "%R%JL%T/Caprio County Raceway", "%R%JL%T/Liberty City",
    "%R%JL%T/SCA Motorplex", "%R%JL%T/The Colosseum", "%R%JL%T/The Pit", "%R%JL%T/Red Pike Arena",
    "%R%JL%T/Death Bowl", "%R%JL%T/Title Screens", "%R%JL%T/Results Screens", "%R%JL%T/End Of Season",
    "%R%JL%T/Bonus Track 1", "%R%JL%T/Bonus Track 2", "%R%JL%T/Bonus Track 3", "%R%JL%T/Bonus Track 4",
};
static const char *const cd_track_titles[CD_PLAYER_TRACKS] = { /* 0x801D640C */
    "%R%JL%T/Slapshot", "%R%JL%T/Rejected", "%R%JL%T/Dead Happy", "%R%JL%T/Footsteps",
    "%R%JL%T/How Do You Know", "%R%JL%T/Better", "%R%JL%T/Pushed Away", "%R%JL%T/Telekenesis",
    "%R%JL%T/Pinnacle", "%R%JL%T/Little Pig", "%R%JL%T/Soundcheck", "%R%JL%T/Joyrider",
    "%R%JL%T/Direction", "%R%JL%T/Jade", "%R%JL%T/Breed", "%R%JL%T/Sun Dive", "%R%JL%T/Burn Out",
    "%R%JL%T/Crawl",
};
static const int32_t cd_track_by_tuscan[CD_PLAYER_TRACKS] = { /* 0x801D6454: 0 Jug, 1 Tuscan */
    0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0,
};

static int32_t cd_player_track = 11; /* 0x801D857C, 0-based: plays Set_CD_Track(n + 1) */

void Set_CD_Track_Texts(void)
{
    snprintf(cd_track_text, sizeof(cd_track_text), "%%R%%JL%%T/Track %d - ", cd_player_track + 1);
    cd_player_text[7].text = cd_track_uses[cd_player_track];
    cd_player_text[8].text = cd_track_titles[cd_player_track];
    cd_player_text[9].text = cd_track_by_tuscan[cd_player_track] ? "%R%JL%T/Tuscan" : "%R%JL%T/Jug";
}

static void set_cd_player_caption(const char *caption)
{
    cd_player_text[2].text = caption;
    cd_player_text[3].text = caption;
}

void Show_CD_Prev_Track_Caption(void)
{
    set_cd_player_caption("%R%JL%T/Prev. Track");
}

void Show_CD_Play_Caption(void)
{
    set_cd_player_caption("%R%JL%T/Play");
}

void Show_CD_Stop_Caption(void)
{
    set_cd_player_caption("%R%JL%T/Stop");
}

void Show_CD_Next_Track_Caption(void)
{
    set_cd_player_caption("%R%JL%T/Next Track");
}

int CD_Player_Previous_Track(void)
{
    if (cd_player_track > 0) {
        cd_player_track--;
        Set_CD_Track_Texts();
    }
    return 0;
}

int CD_Player_Play_Track(void)
{
    Set_CD_Track(cd_player_track + 1, 1);
    Start_CD_Audio();
    return 0;
}

int CD_Player_Stop(void)
{
    CD_Stop();
    return 0;
}

int CD_Player_Next_Track(void)
{
    if (cd_player_track < CD_PLAYER_TRACKS - 1) {
        cd_player_track++;
        Set_CD_Track_Texts();
    }
    return 0;
}

static const SubMenuItem cd_player_items[4] = { /* 0x801D67B0 */
    {118, 190, Show_CD_Prev_Track_Caption, CD_Player_Previous_Track, Null_Menu_Callback, 0},
    {166, 190, Show_CD_Play_Caption, CD_Player_Play_Track, Null_Menu_Callback, 0},
    {214, 190, Show_CD_Stop_Caption, CD_Player_Stop, Null_Menu_Callback, 0},
    {262, 190, Show_CD_Next_Track_Caption, CD_Player_Next_Track, Null_Menu_Callback, 0x10000},
};

static const uint8_t cd_player_moves[4][4] = {{0, 0, 1, 0}, {0, 1, 2, 1}, {1, 2, 3, 2}, {2, 3, 3, 3}}; /* 0x801D67FC */
static const char *const cd_player_item_sprites[4] = {"LASTTRCK", "PLAY", "STOP", "NEXTTRCK"}; /* 0x801D678C */
static const char *const cd_player_pressed_sprites[4] = {"LASTTRKP", "PLAYP", "STOPP", "NEXTTRKP"}; /* 0x801D679C */

/* Previous, play, stop and next on the slab; Cross works the button and stays, Triangle leaves.
 * The menu music starts again afterwards (restart_cd_audio). */
int CD_Player_Screen(void)
{
    Sprite *ring = &cd_player_sprites[CP_RINGS];
    int item = 0;
    bool changed = false;
    int result;
    ring->x = cd_player_items[0].x;
    ring->y = cd_player_items[0].y;
    cd_player_items[0].caption();
    Set_CD_Track_Texts();
    Setup_Screen_Polys(cd_player_polys);
    Setup_Screen_Text(cd_player_text);
    Setup_Screen_Lines(cd_player_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = cd_player_moves[item][0];
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = cd_player_moves[item][2];
            changed = true;
        } else if (pad.pressed & PAD_TRIANGLE) {
            result = 0;
            break;
        } else if (pad.pressed & PAD_CROSS) {
            cd_player_polys[item].name = cd_player_pressed_sprites[item];
            Setup_Screen_Polys(cd_player_polys);
            Button_Pressed(7, NULL);
            cd_player_polys[item].name = cd_player_item_sprites[item];
            Setup_Screen_Polys(cd_player_polys);
            Button_Pressed(2, NULL);
            result = cd_player_items[item].select();
            if (result)
                break;
            ring->x = cd_player_items[item].x;
            ring->y = cd_player_items[item].y;
            cd_player_items[item].caption();
            Setup_Screen_Polys(cd_player_polys);
            Setup_Screen_Text(cd_player_text);
            Setup_Screen_Lines(cd_player_lines);
            Set_Highlight_Grey();
            continue;
        }
        if (changed) {
            changed = false;
            DrawSync(0);
            ring->x = cd_player_items[item].x;
            ring->y = cd_player_items[item].y;
            cd_player_items[item].caption();
            Setup_Screen_Text(cd_player_text);
            Setup_Screen_Polys(cd_player_polys);
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
    restart_cd_audio = 1;
    Rotate_Slab_Off(NULL);
    return result;
}

/* ---- FRONTEND.BIN: controller set-up ---- */

/* Sprites shared by the controller screens. */
enum {
    PC_ICONTROL, PC_PAD2, PC_TCANCEL, PC_PAD1, PC_TSELECT, PC_PAD5, PC_TACCEPT, PC_RARROW, PC_RINGS, PC_SIDEONLY,
    PC_TMOVE, PAD_CONFIG_SPRITES
};

static Sprite pad_config_sprites[PAD_CONFIG_SPRITES] = { /* 0x801D747C */
    [PC_ICONTROL] = BUTTON_SPRITE(16, 24),
    [PC_PAD2] = BUTTON_SPRITE(260, 24),
    [PC_TCANCEL] = BUTTON_SPRITE(278, 29),
    [PC_PAD1] = BUTTON_SPRITE(260, 41),
    [PC_TSELECT] = BUTTON_SPRITE(278, 46),
    [PC_PAD5] = BUTTON_SPRITE(260, 58),
    [PC_TACCEPT] = BUTTON_SPRITE(278, 63),
    [PC_RARROW] = BUTTON_SPRITE(262, 192),
    [PC_RINGS] = TRANS_SPRITE(261, 191),
    [PC_SIDEONLY] = BUTTON_SPRITE(256, 71),
    [PC_TMOVE] = BUTTON_SPRITE(282, 80),
};

static Sprite digital_left_arrow = BUTTON_SPRITE(214, 192); /* 0x801D5528 */

static const ScreenLine pad_config_lines[] = { /* 0x801D7584 */
    {3, 54, 52, 124, 0xff, 0xc8, 0xc8, 0},
    {4, 57, 55, 124, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const char *const pad_layout_names[4] = { /* 0x801D75A8 */
    "%R%JL%T/Type 1", "%R%JL%T/Type 2", "%R%JL%T/Type 3", "%R%JL%T/Type 4"};

static ScreenPoly no_pad_polys[] = { /* 0x801D75B8 */
    {4, "ICONTROL", SCREEN_GREY, &pad_config_sprites[PC_ICONTROL]},
    {4, "ZZZZPAD2", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD2]},
    {4, "TCANCEL", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TCANCEL]},
    {4, "ZZZZPAD1", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD1]},
    {4, "TSELECT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TSELECT]},
    {4, "ZZZZPAD5", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD5]},
    {4, "TACCEPT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TACCEPT]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &pad_config_sprites[PC_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TMOVE]},
    {0, NULL, 0, NULL},
};

#define CONTROLLER_TITLE_INK {0x80, 0x64, 0x64, 0}

static ScreenText no_pad_text[] = { /* 0x801D7658 */
    {3, 0, CONTROLLER_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Controller"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Controller"},
    {3, 1, GREY_INK, 0, 0, SCREEN_Z, "%R%JC%T/No Controller"},
    {4, 4, {0, 0, 0, 0}, 3, 3, SCREEN_Z, "%R%JC%T/No Controller"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

/* The digital pad screen: the pad model with the button layout's labels and pointer lines. */
static ScreenPoly digital_polys[] = { /* 0x801D5644 */
    {3, "L_LARROW", SCREEN_GREY, &digital_left_arrow},
    {3, "RARROW", SCREEN_GREY, &pad_config_sprites[PC_RARROW]},
    {2, "ICONTROL", SCREEN_GREY, &pad_config_sprites[PC_ICONTROL]},
    {2, "TRIANGLE", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TCANCEL]},
    {2, "CROSS", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TSELECT]},
    {2, "START", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD5]},
    {2, "TACCEPT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TACCEPT]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &pad_config_sprites[PC_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TMOVE]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &pad_config_sprites[PC_RINGS]},
    {0, NULL, 0, NULL},
};

#define LABEL_INK {0xff, 0xff, 0, 0}
#define DIGITAL_LABELS 6 /* the record that ends the list while the labels are hidden */

static ScreenText digital_text[] = { /* 0x801D5540 */
    {3, 0, CONTROLLER_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Controller"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Controller"},
    {3, 1, GREY_INK, -140, 73, SCREEN_Z, ""},
    {4, 4, GREY_INK, -137, 76, SCREEN_Z, ""},
    {3, 1, GREY_INK, -140, 91, SCREEN_Z, ""},
    {4, 4, GREY_INK, -137, 94, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, ""},
    {4, 2, LABEL_INK, 80, 0, 256, "%JLAccelerate"},
    {4, 2, LABEL_INK, 80, -16, 256, "%JLBrake"},
    {4, 2, LABEL_INK, -15, 36, 256, "%JLSteer"},
    {4, 2, LABEL_INK, -34, -62, 256, "%JLView Toggle"},
    {4, 2, LABEL_INK, -30, -52, 256, "%JLFast Steer"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

/* Per layout: where the Accelerate, Brake and Steer labels go, and the lines to their buttons. */
static const int16_t digital_label_positions[4][3][2] = { /* 0x801D5754 */
    {{82, 3}, {76, -10}, {-120, -10}},
    {{76, -10}, {82, 3}, {-120, -10}},
    {{87, -10}, {81, -23}, {-120, -10}},
    {{81, -23}, {87, -10}, {-120, -10}},
};

static const LabelLine digital_label_lines[4][3] = { /* 0x801D5784 */
    {{70, 121, 92, 121}, {202, 121, 234, 121}, {216, 134, 240, 134}},
    {{70, 121, 92, 121}, {202, 121, 234, 121}, {216, 134, 240, 134}},
    {{70, 121, 92, 121}, {217, 108, 239, 108}, {229, 122, 245, 122}},
    {{70, 121, 92, 121}, {217, 108, 239, 108}, {229, 122, 245, 122}},
};

/* The pad model, turned to face the camera, and its slide in and out. */
static int16_t pad_model_angles[3] = {HALF_TURN + QUARTER_TURN, 0, 0}; /* 0x801D8A18 */
static int32_t pad_model_position[3] = {0, 0, 220};      /* 0x801D5518 */
static const MATRIX pad_model_light = {{{0, 0, -0x1000}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}};           /* 0x801D57E4 */
static const MATRIX pad_model_colour = {{{0x1000, 0, 0}, {0x1000, 0, 0}, {0x1000, 0, 0}}, {0, 0, 0}}; /* 0x801D5804 */
static MATRIX pad_model_matrix;       /* 0x801D8DB8 */
static MATRIX pad_model_light_matrix; /* 0x801D8A20 */
static LINE_F2 label_lines[2][10];    /* 0x801D8C00 */

/* Up to `count` yellow lines from a label to its button, on top of everything (OT slot 1). */
void Draw_Label_Lines(const LabelLine *lines, int count)
{
    int16_t dy = pal_mode == 0 ? -8 : 0;
    for (int i = 0; i < count; i++) {
        LINE_F2 *l = &label_lines[buffer_num][i];
        setLineF2(l);
        setRGB0(l, 0xff, 0xff, 0);
        l->x0 = lines[i].x0;
        l->y0 = (short)(lines[i].y0 + dy);
        l->x1 = lines[i].x1;
        l->y1 = (short)(lines[i].y1 + dy);
        addPrim(&cdb->ot[1], l);
    }
}

void Draw_Digital_Pad_Model(void)
{
    SetBackColor(0x60, 0x60, 0x60);
    Draw_Tile(&back_tile[buffer_num], otsize - 1, 0, 0, SCREEN_W, SCREEN_H, back_colour);
    Calc_Object_MatrixYXZ(&pad_model_matrix, pad_model_angles);
    MulMatrix0(&pad_model_light, &pad_model_matrix, &pad_model_light_matrix);
    SetLightMatrix(&pad_model_light_matrix);
    SetColorMatrix(&pad_model_colour);
    Set_Object_View(&pad_model_matrix, pad_model_position);
    Update_Object(&object_801f2cf8);
    Update_Object(&dpadbuttons_object);
    Update_Object(&dpadlabels_object);
    SetBackColor(0, 0, 0);
}

static void draw_digital_frame(void)
{
    Draw_Slab();
    Print_Draw();
    Draw_Screen_Polys();
    Draw_Screen_Lines();
    Draw_Label_Lines(digital_label_lines[pad_layout_setting], 3);
    Draw_Digital_Pad_Model();
    Draw_All(1);
}

static void place_digital_labels(void)
{
    for (int i = 0; i < 3; i++) {
        digital_text[7 + i].x = digital_label_positions[pad_layout_setting][i][0];
        digital_text[7 + i].y = digital_label_positions[pad_layout_setting][i][1];
    }
}

int Next_Digital_Layout(void)
{
    pad_layout_setting = (pad_layout_setting + 1) & 3;
    place_digital_labels();
    return 0;
}

int Prev_Digital_Layout(void)
{
    pad_layout_setting = (pad_layout_setting - 1) & 3;
    place_digital_labels();
    return 0;
}

void Show_Digital_Layout_Name(void)
{
    digital_text[2].text = pad_layout_names[pad_layout_setting];
    digital_text[3].text = pad_layout_names[pad_layout_setting];
}

void Show_Next_Digital_Layout_Caption(void)
{
    digital_text[4].text = "%R%JL%T/Next Option";
    digital_text[5].text = "%R%JL%T/Next Option";
    Show_Digital_Layout_Name();
}

void Show_Prev_Digital_Layout_Caption(void)
{
    digital_text[4].text = "%R%JL%T/Prev. Option";
    digital_text[5].text = "%R%JL%T/Prev. Option";
    Show_Digital_Layout_Name();
}

static const SubMenuItem digital_items[2] = { /* 0x801D5728 */
    {214, 190, Show_Prev_Digital_Layout_Caption, Prev_Digital_Layout, Null_Menu_Callback, 0},
    {262, 190, Show_Next_Digital_Layout_Caption, Next_Digital_Layout, Null_Menu_Callback, 0x10000},
};

static const uint8_t digital_moves[2][4] = {{0, 0, 1, 0}, {0, 1, 1, 1}}; /* 0x801D574C */
static const char *const digital_item_sprites[2] = {"L_LARROW", "RARROW"};  /* 0x801D5714 */
static const char *const digital_pressed_sprites[2] = {"L_LARROP", "RARROWP"}; /* 0x801D571C */

/* Button_Pressed for the pad screen: also draws the label lines and the pad. */
void Digital_Button_Pressed(int frames)
{
    if (frames != 2)
        Play_Select_FX();
    for (int n = 0;; n++) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (!(pad.held & PAD_CROSS) && n >= frames)
            break;
        draw_digital_frame();
    }
}

static void draw_pad_slide_frame(void)
{
    Debug_Stub();
    Swap_Draw_Buffers();
    Poll_Pad_Buttons();
    Draw_Slab();
    Print_Draw();
    Draw_Screen_Polys();
    Draw_Screen_Lines();
}

/* The pad model slides in from the left with its labels hidden. */
void Slide_Digital_Pad_In(void)
{
    pad_model_position[0] = -0x100;
    pad_model_angles[0] = HALF_TURN + QUARTER_TURN;
    pad_model_angles[1] = 0;
    pad_model_angles[2] = 0;
    digital_text[DIGITAL_LABELS].text = NULL;
    digital_items[1].caption();
    Setup_Screen_Polys(digital_polys);
    Setup_Screen_Text(digital_text);
    Setup_Screen_Lines(pad_config_lines);
    while (pad_model_position[0] != 0) {
        draw_pad_slide_frame();
        pad_model_position[0] += 0x10;
        Draw_Digital_Pad_Model();
        Draw_All(1);
    }
}

/* And out to the right. */
void Slide_Digital_Pad_Out(void)
{
    pad_model_position[0] = 0;
    pad_model_angles[0] = HALF_TURN + QUARTER_TURN;
    pad_model_angles[1] = 0;
    pad_model_angles[2] = 0;
    digital_text[DIGITAL_LABELS].text = NULL;
    Setup_Screen_Text(digital_text);
    while (pad_model_position[0] != 0x100) {
        draw_pad_slide_frame();
        pad_model_position[0] += 0x10;
        Draw_Digital_Pad_Model();
        Draw_All(1);
    }
}

void Show_Digital_Pad_Screen(void)
{
    digital_text[DIGITAL_LABELS].text = NULL;
    digital_items[1].caption();
    Setup_Screen_Polys(digital_polys);
    Setup_Screen_Text(digital_text);
    Setup_Screen_Lines(pad_config_lines);
    Rotate_Slab_On(NULL);
}

/* The button layout for a digital pad: the arrows step through four layouts, Start keeps one,
 * Triangle leaves it as it was. Returns 1 when accepted, 0 when cancelled, -1 when another kind
 * of pad was plugged in. */
int Digital_Pad_Config(void)
{
    Sprite *ring = &pad_config_sprites[PC_RINGS];
    int item = 1;
    bool changed = false;
    int result;
    Slide_Digital_Pad_In();
    digital_text[DIGITAL_LABELS].text = "";
    pad_model_angles[0] = HALF_TURN + QUARTER_TURN;
    pad_model_angles[1] = 0;
    pad_model_angles[2] = 0;
    place_digital_labels();
    ring->x = digital_items[1].x;
    ring->y = digital_items[1].y;
    digital_items[1].caption();
    Setup_Screen_Polys(digital_polys);
    Setup_Screen_Text(digital_text);
    Setup_Screen_Lines(pad_config_lines);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.type != PAD_DIGITAL) {
            result = -1;
            break;
        }
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = digital_moves[item][0];
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = digital_moves[item][2];
            changed = true;
        } else if (pad.pressed & PAD_TRIANGLE) {
            result = 0;
            break;
        } else if (pad.pressed & PAD_START) {
            Play_Select_FX();
            result = 1;
            break;
        } else if (pad.pressed & PAD_CROSS) {
            digital_polys[item].name = digital_pressed_sprites[item];
            Setup_Screen_Polys(digital_polys);
            Digital_Button_Pressed(7);
            digital_polys[item].name = digital_item_sprites[item];
            Setup_Screen_Polys(digital_polys);
            Digital_Button_Pressed(2);
            if (digital_items[item].select() == -1) {
                result = -1;
                break;
            }
            DrawSync(0);
            pad_model_angles[0] = HALF_TURN + QUARTER_TURN;
            ring->x = digital_items[item].x;
            ring->y = digital_items[item].y;
            digital_items[item].caption();
            Setup_Screen_Polys(digital_polys);
            Setup_Screen_Text(digital_text);
            Setup_Screen_Lines(pad_config_lines);
            Set_Highlight_Grey();
            continue;
        }
        if (changed) {
            changed = false;
            DrawSync(0);
            ring->x = digital_items[item].x;
            ring->y = digital_items[item].y;
            digital_items[item].caption();
            Setup_Screen_Text(digital_text);
            Setup_Screen_Polys(digital_polys);
        }
        draw_digital_frame();
    }
    if (result == 1)
        Setup_Pad(1, pad_layout_setting, 0, 0);
    Slide_Digital_Pad_Out();
    return result;
}

/* Waits for a pad to be plugged in (-1) or Triangle (0). */
int No_Pad_Screen(void)
{
    Setup_Screen_Polys(no_pad_polys);
    Setup_Screen_Text(no_pad_text);
    Setup_Screen_Lines(pad_config_lines);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.type != PAD_NONE)
            return -1;
        if (pad.pressed & PAD_TRIANGLE)
            return 0;
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

/* ---- the NeGcon and analog pad screens ----
 * Both are the same menu: the previous and next button layouts, the twist's dead zone ("Set Centre")
 * and its full-lock range ("Set Limits"), with the pad's model turned to show the settings. The
 * original has a copy of every function for each pad; they differ only in their tables and models. */

#define TWIST_LABELS 6     /* the text record that ends the list while the labels are hidden */
#define WOBBLE_END 0x7777  /* ends the centre screen's wobble table */

static const char *const centre_names[4] = { /* 0x801D745C (NeGcon), 0x801D6CCC (analog) */
    "%R%JL%T/1 Degree", "%R%JL%T/3 Degrees", "%R%JL%T/6 Degrees", "%R%JL%T/10 Degrees"};
static const char *const negcon_limit_names[4] = { /* 0x801D746C */
    "%R%JL%T/22 Degrees", "%R%JL%T/45 Degrees", "%R%JL%T/90 Degrees", "%R%JL%T/135 Degrees"};
static const char *const analog_limit_names[4] = { /* 0x801D6CDC */
    "%R%JL%T/22 Degrees", "%R%JL%T/45 Degrees", "%R%JL%T/90 Degrees", "%R%JL%T/120 Degrees"};

/* The centre screen rocks the model through this, scaled down by the dead zone (both pads). */
static const int16_t twist_wobble[] = { /* 0x801D7334, 0x801D6BA4 */
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 74, 78, 81, 84, 86, 88, 89, 90, 90, 90, 90, 90, 90,
    90, 90, 90, 90, 90, 90, 90, 90, 89, 88, 86, 84, 81, 78, 74, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15,
    10, 5, 0, -5, -10, -15, -20, -25, -30, -35, -40, -45, -50, -55, -60, -65, -70, -74, -78, -81, -84, -86, -88,
    -89, -90, -90, -90, -90, -90, -90, -90, -90, -90, -90, -90, -90, -90, -90, -89, -88, -86, -84, -81, -78, -74,
    -70, -65, -60, -55, -50, -45, -40, -35, -30, -25, -20, -15, -10, -5, WOBBLE_END,
};

static const MATRIX twist_model_light = {{{0, 0, -0x1000}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}};           /* 0x801D741C */
static const MATRIX twist_model_colour = {{{0x1000, 0, 0}, {0x1000, 0, 0}, {0x1000, 0, 0}}, {0, 0, 0}}; /* 0x801D743C */
static MATRIX twist_light_matrix; /* 0x801D8BE0, 0x801D8BB0 */

typedef struct {
    uint8_t type;                         /* PAD_NEGCON or PAD_ANALOG */
    ScreenText *text;
    ScreenPoly *polys;
    const SubMenuItem *items;
    const uint8_t (*moves)[4];
    const char *const *sprites, *const *pressed_sprites;
    const int16_t (*label_positions)[4][2];
    const LabelLine (*label_lines)[5];
    const char *const *limit_names;
    void (*draw_model)(void);
    void (*slide_in)(void);
    void (*slide_out)(void);
    void (*straighten)(void);             /* after a setting screen */
} TwistScreen;

/* One model part's matrices, then the part. */
static void draw_twist_part(const int16_t angles[3], int32_t position[3], Object *a, Object *b, Object *c)
{
    Calc_Object_MatrixYXZ(&pad_model_matrix, (int16_t *)angles);
    MulMatrix0((MATRIX *)&twist_model_light, &pad_model_matrix, &twist_light_matrix);
    SetLightMatrix(&twist_light_matrix);
    SetColorMatrix((MATRIX *)&twist_model_colour);
    Set_Object_View(&pad_model_matrix, position);
    Update_Object(a);
    Update_Object(b);
    if (c)
        Update_Object(c);
}

static void draw_twist_background(void)
{
    SetBackColor(0x60, 0x60, 0x60);
    Draw_Tile(&back_tile[buffer_num], otsize - 1, 0, 0, SCREEN_W, SCREEN_H, back_colour);
}

/* The NeGcon: its left half with the I and II buttons, its right half with A, twisted apart. */
static int16_t negcon_left_angles[3] = {HALF_TURN + QUARTER_TURN, 0, 0}; /* 0x801D8BD0 */
static int16_t negcon_right_angles[3] = {HALF_TURN + QUARTER_TURN, 0, 0}; /* 0x801D8BD8 */
static int32_t negcon_position[3] = {0, -20, 475};       /* 0x801D6F9C */

static void Draw_NeGcon_Model(void) /* 0x801D0420 */
{
    draw_twist_background();
    draw_twist_part(negcon_left_angles, negcon_position, &negleft_object, &negbut1_object, &negbut2_object);
    draw_twist_part(negcon_right_angles, negcon_position, &object_801f2cdc, &negbut3_object, NULL);
    SetBackColor(0, 0, 0);
}

/* The analog pad (a steering wheel): the wheel on its base, and the buttons turning with it. */
static int16_t analog_wheel_angles[3] = {0x700, 0, 0};   /* 0x801D8BA0 */
static int16_t analog_button_angles[3] = {0x6a4, 0, 0};  /* 0x801D8BA8 */
static int32_t analog_position[3] = {0, -0x40, 0x380};   /* 0x801D680C */

static void Draw_Analog_Pad_Model(void) /* 0x801CDB18 */
{
    draw_twist_background();
    draw_twist_part(analog_wheel_angles, analog_position, &madjoy_object, &madbase_object, NULL);
    draw_twist_part(analog_button_angles, analog_position, &madbut_object, &object_801f2cb0, NULL);
    SetBackColor(0, 0, 0);
}

static void twist_frame_start(void)
{
    Debug_Stub();
    Swap_Draw_Buffers();
    Poll_Pad_Buttons();
}

static void twist_frame_end(const TwistScreen *s)
{
    Draw_Slab();
    Print_Draw();
    Draw_Screen_Polys();
    Draw_Screen_Lines();
    s->draw_model();
    Draw_All(1);
}

static void place_twist_labels(const TwistScreen *s)
{
    for (int i = 0; i < 4; i++) {
        s->text[7 + i].x = s->label_positions[pad_layout_setting][i][0];
        s->text[7 + i].y = s->label_positions[pad_layout_setting][i][1];
    }
}

static void set_twist_lines(const TwistScreen *s, int line, const char *text)
{
    s->text[line].text = text;
    s->text[line + 1].text = text;
}

/* Button_Pressed for these screens: also the label lines and the model. */
static void twist_button_pressed(const TwistScreen *s, int frames)
{
    if (frames != 2)
        Play_Select_FX();
    for (int n = 0;; n++) {
        twist_frame_start();
        if (!(pad.held & PAD_CROSS) && n >= frames)
            break;
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_Label_Lines(s->label_lines[pad_layout_setting], 5);
        s->draw_model();
        Draw_All(1);
    }
}

/* Left and right step `setting` (0..3); Cross or Start keeps it, Triangle puts it back. `animate` moves
 * the model for the setting each frame. 1 kept, 0 cancelled, -1 another pad plugged in. */
static int twist_setting(const TwistScreen *s, uint16_t *setting, const char *const *names,
                         void (*animate)(int *frame))
{
    uint16_t old = *setting;
    int frame = 0;
    bool changed = false;
    int result;
    for (;;) {
        twist_frame_start();
        if (pad.type != s->type) {
            result = -1;
            break;
        }
        if (pad.pressed & PAD_LEFT) {
            Play_Click_FX();
            if (*setting != 0)
                (*setting)--;
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Play_Click_FX();
            if (*setting < 3)
                (*setting)++;
            changed = true;
        } else if (pad.pressed & PAD_TRIANGLE) {
            result = 0;
            break;
        } else if (pad.pressed & (PAD_CROSS | PAD_START)) {
            Play_Select_FX();
            result = 1;
            break;
        }
        if (changed) {
            changed = false;
            DrawSync(0);
            set_twist_lines(s, 4, names[*setting]);
            Setup_Screen_Text(s->text);
            Setup_Pad(s->type, pad_layout_setting, pad_config[0], pad_config[1]);
        }
        animate(&frame);
        twist_frame_end(s);
    }
    return result == 1 ? result : (*setting = old, result);
}

/* The set-up menu itself (item 3, Next, first). Start keeps the settings. */
static int run_twist_menu(const TwistScreen *s)
{
    Sprite *ring = &pad_config_sprites[PC_RINGS];
    int item = 3;
    bool changed = false;
    int result;
    s->slide_in();
    s->text[TWIST_LABELS].text = "";
    s->straighten();
    place_twist_labels(s);
    ring->x = s->items[3].x;
    ring->y = s->items[3].y;
    s->items[3].caption();
    Setup_Screen_Polys(s->polys);
    Setup_Screen_Text(s->text);
    Setup_Screen_Lines(pad_config_lines);
    for (;;) {
        twist_frame_start();
        if (pad.type != s->type) {
            result = -1;
            break;
        }
        if (pad.pressed & (PAD_LEFT | PAD_RIGHT)) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = s->moves[item][(pad.pressed & PAD_LEFT) ? 0 : 2];
            changed = true;
        } else if (pad.pressed & PAD_TRIANGLE) {
            result = 0;
            break;
        } else if (pad.pressed & PAD_START) {
            Play_Select_FX();
            result = 1;
            break;
        } else if (pad.pressed & PAD_CROSS) {
            s->polys[item].name = s->pressed_sprites[item];
            Setup_Screen_Polys(s->polys);
            twist_button_pressed(s, 7);
            s->polys[item].name = s->sprites[item];
            Setup_Screen_Polys(s->polys);
            twist_button_pressed(s, 2);
            if (s->items[item].select() == -1) {
                result = -1;
                break;
            }
            DrawSync(0);
            s->straighten();
            ring->x = s->items[item].x;
            ring->y = s->items[item].y;
            s->items[item].caption();
            Setup_Screen_Polys(s->polys);
            Setup_Screen_Text(s->text);
            Setup_Screen_Lines(pad_config_lines);
            Set_Highlight_Grey();
            continue;
        }
        if (changed) {
            changed = false;
            DrawSync(0);
            ring->x = s->items[item].x;
            ring->y = s->items[item].y;
            s->items[item].caption();
            Setup_Screen_Text(s->text);
            Setup_Screen_Polys(s->polys);
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_Label_Lines(s->label_lines[pad_layout_setting], 5);
        s->draw_model();
        Draw_All(1);
    }
    if (result == 1)
        Setup_Pad(s->type, pad_layout_setting, pad_config[0], pad_config[1]);
    s->slide_out();
    return result;
}

/* ---- the NeGcon screen ---- */

static TwistScreen negcon_screen;

static Sprite negcon_buttons[3] = { /* 0x801D6FAC */
    BUTTON_SPRITE(118, 192), BUTTON_SPRITE(166, 192), BUTTON_SPRITE(214, 192),
};

static ScreenPoly negcon_polys[] = { /* 0x801D70E4 */
    {3, "N_LARROW", SCREEN_GREY, &negcon_buttons[0]},
    {3, "NEGCENT", SCREEN_GREY, &negcon_buttons[1]},
    {3, "NEGTWIST", SCREEN_GREY, &negcon_buttons[2]},
    {3, "RARROW", SCREEN_GREY, &pad_config_sprites[PC_RARROW]},
    {2, "ICONTROL", SCREEN_GREY, &pad_config_sprites[PC_ICONTROL]},
    {2, "NEG_B", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TCANCEL]},
    {2, "NEG_I", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TSELECT]},
    {2, "NEG_S", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD5]},
    {2, "TACCEPT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TACCEPT]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &pad_config_sprites[PC_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TMOVE]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &pad_config_sprites[PC_RINGS]},
    {0, NULL, 0, NULL},
};

static ScreenText negcon_text[] = { /* 0x801D6FF4 */
    {3, 0, CONTROLLER_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Controller"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Controller"},
    {3, 1, GREY_INK, -140, 73, SCREEN_Z, ""},
    {4, 4, GREY_INK, -137, 76, SCREEN_Z, ""},
    {3, 1, GREY_INK, -140, 91, SCREEN_Z, ""},
    {4, 4, GREY_INK, -137, 95, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, ""},
    {4, 2, LABEL_INK, -34, -62, 256, "%JLView Toggle"},
    {4, 2, LABEL_INK, 80, 0, 256, "%JLAccelerate"},
    {4, 2, LABEL_INK, 80, -16, 256, "%JLBrake"},
    {4, 2, LABEL_INK, -15, 36, 256, "%JLSteer"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

/* Per layout: View Toggle, Accelerate, Brake and Steer. */
static const int16_t negcon_label_positions[4][4][2] = { /* 0x801D7254 */
    {{-34, -62}, {88, -1}, {88, -16}, {-15, 36}},
    {{-34, -62}, {88, -16}, {88, -1}, {-15, 36}},
    {{-34, -62}, {88, -13}, {88, -28}, {-15, 36}},
    {{-34, -62}, {88, -28}, {88, -13}, {-15, 36}},
};

static const LabelLine negcon_label_lines[4][5] = { /* 0x801D7294 */
    {{123, 68, 114, 77}, {192, 68, 201, 77}, {158, 162, 158, 152}, {211, 130, 245, 130}, {202, 115, 245, 115}},
    {{123, 68, 114, 77}, {192, 68, 201, 77}, {158, 162, 158, 152}, {211, 130, 245, 130}, {202, 115, 245, 115}},
    {{123, 68, 114, 77}, {192, 68, 201, 77}, {158, 162, 158, 152}, {228, 118, 245, 118}, {219, 103, 245, 103}},
    {{123, 68, 114, 77}, {192, 68, 201, 77}, {158, 162, 158, 152}, {228, 118, 245, 118}, {219, 103, 245, 103}},
};

static int Next_NeGcon_Layout(void) /* 0x801CF084 */
{
    pad_layout_setting = (pad_layout_setting + 1) & 3;
    place_twist_labels(&negcon_screen);
    return 0;
}

static int Prev_NeGcon_Layout(void) /* 0x801CF1D8 */
{
    pad_layout_setting = (pad_layout_setting - 1) & 3;
    place_twist_labels(&negcon_screen);
    return 0;
}

static void negcon_turn(int16_t step)
{
    negcon_left_angles[0] = (int16_t)(negcon_left_angles[0] + step);
    negcon_right_angles[0] = (int16_t)(negcon_right_angles[0] + step);
}

/* The two halves twist against each other, less for a wider dead zone. */
static void negcon_wobble(int *frame)
{
    if (twist_wobble[*frame] == WOBBLE_END)
        *frame = 0;
    negcon_left_angles[0] = (int16_t)(twist_wobble[*frame] / (6 - pad_config[0]));
    negcon_right_angles[0] = (int16_t)(-twist_wobble[*frame] / (6 - pad_config[0]));
    (*frame)++;
}

/* The pad turns to face up for this one, and back afterwards. */
static int NeGcon_Set_Centre(void) /* 0x801CF32C */
{
    pad_config_sprites[PC_RINGS].x = OFF_SCREEN;
    negcon_text[TWIST_LABELS].text = NULL;
    Setup_Screen_Text(negcon_text);
    Setup_Screen_Polys(negcon_polys);
    while (negcon_left_angles[0] != TURN) {
        twist_frame_start();
        negcon_turn(0x40);
        twist_frame_end(&negcon_screen);
    }
    DrawSync(0);
    set_twist_lines(&negcon_screen, 2, "%R%JL%T/Set Centre");
    set_twist_lines(&negcon_screen, 4, centre_names[pad_config[0]]);
    Setup_Screen_Text(negcon_text);
    int result = twist_setting(&negcon_screen, &pad_config[0], centre_names, negcon_wobble);
    if (result != -1) {
        negcon_left_angles[0] = negcon_right_angles[0] = TURN;
        while (negcon_left_angles[0] != HALF_TURN + QUARTER_TURN) {
            twist_frame_start();
            negcon_turn(-0x40);
            twist_frame_end(&negcon_screen);
        }
    }
    negcon_text[TWIST_LABELS].text = "";
    return result;
}

/* The halves follow the twist, by more for a smaller range. */
static void negcon_follow_twist(int *frame)
{
    static const int16_t scale[4] = {2, 3, 4, 6};
    int16_t turn = (int16_t)((0x80 - pad.steer) * scale[pad_config[1]]);
    (void)frame;
    negcon_left_angles[0] = (int16_t)(0xc00 - turn);
    negcon_right_angles[0] = (int16_t)(0xc00 + turn);
}

static int NeGcon_Set_Limits(void) /* 0x801CF97C */
{
    pad_config_sprites[PC_RINGS].x = OFF_SCREEN;
    set_twist_lines(&negcon_screen, 2, "%R%JL%T/Set Limits");
    set_twist_lines(&negcon_screen, 4, negcon_limit_names[pad_config[1]]);
    negcon_text[TWIST_LABELS].text = NULL;
    Setup_Screen_Text(negcon_text);
    int result = twist_setting(&negcon_screen, &pad_config[1], negcon_limit_names, negcon_follow_twist);
    negcon_text[TWIST_LABELS].text = "";
    return result;
}

static void Show_NeGcon_Layout_Name(void) /* 0x801CFEE4 */
{
    set_twist_lines(&negcon_screen, 2, pad_layout_names[pad_layout_setting]);
}

static void Show_NeGcon_Centre_Caption(void) /* 0x801CFF6C */
{
    set_twist_lines(&negcon_screen, 4, "%R%JL%T/Set Centre");
    Show_NeGcon_Layout_Name();
}

static void Show_NeGcon_Limits_Caption(void) /* 0x801CFFBC */
{
    set_twist_lines(&negcon_screen, 4, "%R%JL%T/Set Limits");
    Show_NeGcon_Layout_Name();
}

static void Show_NeGcon_Next_Caption(void) /* 0x801D000C */
{
    set_twist_lines(&negcon_screen, 4, "%R%JL%T/Next Option");
    Show_NeGcon_Layout_Name();
}

static void Show_NeGcon_Prev_Caption(void) /* 0x801D005C */
{
    set_twist_lines(&negcon_screen, 4, "%R%JL%T/Prev. Option");
    Show_NeGcon_Layout_Name();
}

static const SubMenuItem negcon_items[4] = { /* 0x801D71F8 */
    {118, 190, Show_NeGcon_Prev_Caption, Prev_NeGcon_Layout, Null_Menu_Callback, 0},
    {166, 190, Show_NeGcon_Centre_Caption, NeGcon_Set_Centre, Null_Menu_Callback, 0},
    {214, 190, Show_NeGcon_Limits_Caption, NeGcon_Set_Limits, Null_Menu_Callback, 0},
    {262, 190, Show_NeGcon_Next_Caption, Next_NeGcon_Layout, Null_Menu_Callback, 0x10000},
};

static const uint8_t twist_moves[4][4] = {{0, 0, 1, 0}, {0, 1, 2, 1}, {1, 2, 3, 2}, {2, 3, 3, 3}}; /* 0x801D7244, 0x801D6AB4 */
static const char *const twist_item_sprites[4] = {"N_LARROW", "NEGCENT", "NEGTWIST", "RARROW"};      /* 0x801D71D4, 0x801D6A44 */
static const char *const twist_pressed_sprites[4] = {"N_LARROP", "NEGCENTP", "NEGTWSTP", "RARROWP"}; /* 0x801D71E4, 0x801D6A54 */

static void negcon_slide_frame(void)
{
    twist_frame_start();
    Draw_Slab();
    Print_Draw();
    Draw_Screen_Polys();
    Draw_Screen_Lines();
    negcon_position[0] += 0x20;
    Draw_NeGcon_Model();
    Draw_All(1);
}

static void NeGcon_Straight(void)
{
    negcon_left_angles[0] = negcon_right_angles[0] = HALF_TURN + QUARTER_TURN;
}

/* The model slides in from the left with its labels hidden. */
static void Slide_NeGcon_In(void) /* 0x801D01D0 */
{
    negcon_position[0] = -0x1c0;
    negcon_left_angles[0] = negcon_right_angles[0] = HALF_TURN + QUARTER_TURN;
    negcon_left_angles[1] = negcon_right_angles[1] = 0;
    negcon_left_angles[2] = negcon_right_angles[2] = 0;
    negcon_text[TWIST_LABELS].text = NULL;
    negcon_items[3].caption();
    Setup_Screen_Polys(negcon_polys);
    Setup_Screen_Text(negcon_text);
    Setup_Screen_Lines(pad_config_lines);
    while (negcon_position[0] != 0)
        negcon_slide_frame();
}

/* And out to the right. */
static void Slide_NeGcon_Out(void) /* 0x801D0318 */
{
    negcon_position[0] = 0;
    negcon_left_angles[0] = negcon_right_angles[0] = HALF_TURN + QUARTER_TURN;
    negcon_left_angles[1] = negcon_right_angles[1] = 0;
    negcon_left_angles[2] = negcon_right_angles[2] = 0;
    negcon_text[TWIST_LABELS].text = NULL;
    Setup_Screen_Text(negcon_text);
    while (negcon_position[0] != 0x200)
        negcon_slide_frame();
}

static TwistScreen negcon_screen = {
    PAD_NEGCON, negcon_text, negcon_polys, negcon_items, twist_moves, twist_item_sprites, twist_pressed_sprites,
    negcon_label_positions, negcon_label_lines, negcon_limit_names, Draw_NeGcon_Model, Slide_NeGcon_In,
    Slide_NeGcon_Out, NeGcon_Straight,
};

/* The NeGcon set-up. Returns 1 when accepted, 0 when cancelled, -1 when another kind of pad was
 * plugged in. */
int Control_Method_Menu(void) /* 0x801CE8D0 */
{
    return run_twist_menu(&negcon_screen);
}

/* ---- the analog pad screen ---- */

static TwistScreen analog_screen;

static Sprite analog_buttons[3] = { /* 0x801D681C */
    BUTTON_SPRITE(118, 192), BUTTON_SPRITE(166, 192), BUTTON_SPRITE(214, 192),
};

static ScreenPoly analog_polys[] = { /* 0x801D6954 */
    {3, "N_LARROW", SCREEN_GREY, &analog_buttons[0]},
    {3, "NEGCENT", SCREEN_GREY, &analog_buttons[1]},
    {3, "NEGTWIST", SCREEN_GREY, &analog_buttons[2]},
    {3, "RARROW", SCREEN_GREY, &pad_config_sprites[PC_RARROW]},
    {2, "ICONTROL", SCREEN_GREY, &pad_config_sprites[PC_ICONTROL]},
    {2, "MAD_B", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TCANCEL]},
    {2, "MAD_I", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TSELECT]},
    {2, "MAD_S", SCREEN_GREY_TRANS, &pad_config_sprites[PC_PAD5]},
    {2, "TACCEPT", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TACCEPT]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &pad_config_sprites[PC_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &pad_config_sprites[PC_TMOVE]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &pad_config_sprites[PC_RINGS]},
    {0, NULL, 0, NULL},
};

static ScreenText analog_text[] = { /* 0x801D6864 */
    {3, 0, CONTROLLER_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Controller"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Controller"},
    {3, 1, GREY_INK, -140, 73, SCREEN_Z, ""},
    {4, 4, GREY_INK, -137, 76, SCREEN_Z, ""},
    {3, 1, GREY_INK, -140, 91, SCREEN_Z, ""},
    {4, 4, GREY_INK, -137, 95, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, ""},
    {4, 2, LABEL_INK, -34, -62, 256, "%JLView Toggle"},
    {4, 2, LABEL_INK, 80, 0, 256, "%JRAccelerate"},
    {4, 2, LABEL_INK, 80, -16, 256, "%JRBrake"},
    {4, 2, LABEL_INK, -15, 36, 256, "%JLSteer"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const int16_t analog_label_positions[4][4][2] = { /* 0x801D6AC4 */
    {{-34, -62}, {-57, -41}, {-57, -26}, {-15, 16}},
    {{-34, -62}, {-57, -26}, {-57, -41}, {-15, 16}},
    {{-34, -62}, {110, -41}, {85, -26}, {-15, 16}},
    {{-34, -62}, {110, -26}, {85, -41}, {-15, 16}},
};

static const LabelLine analog_label_lines[4][5] = { /* 0x801D6B04 */
    {{123, 68, 118, 80}, {192, 68, 197, 80}, {158, 142, 158, 132}, {105, 90, 127, 90}, {105, 105, 130, 95}},
    {{123, 68, 118, 80}, {192, 68, 197, 80}, {158, 142, 158, 132}, {105, 90, 127, 90}, {105, 105, 130, 95}},
    {{123, 68, 118, 80}, {192, 68, 197, 80}, {158, 142, 158, 132}, {211, 90, 189, 90}, {211, 105, 186, 95}},
    {{123, 68, 118, 80}, {192, 68, 197, 80}, {158, 142, 158, 132}, {211, 90, 189, 90}, {211, 105, 186, 95}},
};

static int Next_Analog_Layout(void) /* 0x801CCA0C */
{
    pad_layout_setting = (pad_layout_setting + 1) & 3;
    place_twist_labels(&analog_screen);
    return 0;
}

static int Prev_Analog_Layout(void) /* 0x801CCB60 */
{
    pad_layout_setting = (pad_layout_setting - 1) & 3;
    place_twist_labels(&analog_screen);
    return 0;
}

/* The buttons rock about the wheel's axis. */
static void analog_wobble(int *frame)
{
    if (twist_wobble[*frame] == WOBBLE_END)
        *frame = 0;
    analog_button_angles[2] = (int16_t)(twist_wobble[*frame] / (6 - pad_config[0]));
    (*frame)++;
}

static int Analog_Set_Centre(void) /* 0x801CCCB4 */
{
    pad_config_sprites[PC_RINGS].x = OFF_SCREEN;
    set_twist_lines(&analog_screen, 2, "%R%JL%T/Set Centre");
    set_twist_lines(&analog_screen, 4, centre_names[pad_config[0]]);
    analog_text[TWIST_LABELS].text = NULL;
    DrawSync(0);
    Setup_Screen_Text(analog_text);
    Setup_Screen_Polys(analog_polys);
    int result = twist_setting(&analog_screen, &pad_config[0], centre_names, analog_wobble);
    analog_text[TWIST_LABELS].text = "";
    return result;
}

static void analog_follow_twist(int *frame)
{
    static const int16_t scale[4] = {3, 6, 8, 10};
    (void)frame;
    analog_button_angles[2] = (int16_t)((0x80 - pad.steer) * scale[pad_config[1]]);
}

static int Analog_Set_Limits(void) /* 0x801CD118 */
{
    pad_config_sprites[PC_RINGS].x = OFF_SCREEN;
    set_twist_lines(&analog_screen, 2, "%R%JL%T/Set Limits");
    set_twist_lines(&analog_screen, 4, analog_limit_names[pad_config[1]]);
    analog_text[TWIST_LABELS].text = NULL;
    Setup_Screen_Text(analog_text);
    int result = twist_setting(&analog_screen, &pad_config[1], analog_limit_names, analog_follow_twist);
    analog_text[TWIST_LABELS].text = "";
    return result;
}

static void Show_Analog_Layout_Name(void) /* 0x801CD5AC */
{
    set_twist_lines(&analog_screen, 2, pad_layout_names[pad_layout_setting]);
}

static void Show_Analog_Centre_Caption(void) /* 0x801CD634 */
{
    set_twist_lines(&analog_screen, 4, "%R%JL%T/Set Centre");
    Show_Analog_Layout_Name();
}

static void Show_Analog_Limits_Caption(void) /* 0x801CD684 */
{
    set_twist_lines(&analog_screen, 4, "%R%JL%T/Set Limits");
    Show_Analog_Layout_Name();
}

static void Show_Analog_Next_Caption(void) /* 0x801CD6D4 */
{
    set_twist_lines(&analog_screen, 4, "%R%JL%T/Next Option");
    Show_Analog_Layout_Name();
}

static void Show_Analog_Prev_Caption(void) /* 0x801CD724 */
{
    set_twist_lines(&analog_screen, 4, "%R%JL%T/Prev. Option");
    Show_Analog_Layout_Name();
}

static const SubMenuItem analog_items[4] = { /* 0x801D6A68 */
    {118, 190, Show_Analog_Prev_Caption, Prev_Analog_Layout, Null_Menu_Callback, 0},
    {166, 190, Show_Analog_Centre_Caption, Analog_Set_Centre, Null_Menu_Callback, 0},
    {214, 190, Show_Analog_Limits_Caption, Analog_Set_Limits, Null_Menu_Callback, 0},
    {262, 190, Show_Analog_Next_Caption, Next_Analog_Layout, Null_Menu_Callback, 0x10000},
};

static void analog_reset_model(int16_t x)
{
    analog_position[0] = x;
    analog_position[1] = -0x40;
    analog_position[2] = 0x380;
    analog_wheel_angles[0] = 0x700;
    analog_wheel_angles[1] = analog_wheel_angles[2] = 0;
    analog_button_angles[0] = 0x6a4;
    analog_button_angles[1] = analog_button_angles[2] = 0;
    analog_text[TWIST_LABELS].text = NULL;
}

static void analog_slide_frame(void)
{
    twist_frame_start();
    Draw_Slab();
    Print_Draw();
    Draw_Screen_Polys();
    Draw_Screen_Lines();
    analog_position[0] += 0x20;
    Draw_Analog_Pad_Model();
    Draw_All(1);
}

static void Slide_Analog_Pad_In(void) /* 0x801CD898 */
{
    analog_reset_model(-0x400);
    analog_items[3].caption();
    Setup_Screen_Polys(analog_polys);
    Setup_Screen_Text(analog_text);
    Setup_Screen_Lines(pad_config_lines);
    while (analog_position[0] != 0)
        analog_slide_frame();
}

static void Slide_Analog_Pad_Out(void) /* 0x801CD9F8 */
{
    analog_reset_model(0);
    Setup_Screen_Text(analog_text);
    while (analog_position[0] != 0x400)
        analog_slide_frame();
}

static void Analog_Straight(void)
{
    analog_button_angles[2] = 0;
}

static TwistScreen analog_screen = {
    PAD_ANALOG, analog_text, analog_polys, analog_items, twist_moves, twist_item_sprites, twist_pressed_sprites,
    analog_label_positions, analog_label_lines, analog_limit_names, Draw_Analog_Pad_Model, Slide_Analog_Pad_In,
    Slide_Analog_Pad_Out, Analog_Straight,
};

int Analog_Pad_Config(void) /* 0x801CC268 */
{
    return run_twist_menu(&analog_screen);
}

/* ---- which analog pad ---- */

static ScreenText analog_type_text[] = { /* 0x801D76BC */
    {3, 0, CONTROLLER_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Controller"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Controller"},
    {3, 1, GREY_INK, 0, 40, SCREEN_Z, "%R%JC%T/Analog Pad"},
    {4, 4, {0, 0, 0, 0}, 3, 43, SCREEN_Z, "%R%JC%T/Analog Pad"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

/* Both kinds of pad answer as a NeGcon; this asks which one it is, NeGcon (left) or analog (right), and
 * points the NeGcon answer at that one's set-up. 1 chosen, 0 Triangle; 1 too when the pad goes. */
int Ask_Analog_Pad_Type(void) /* 0x801D0EFC */
{
    static POLY_FT4 quads[4];
    Sprite panel = {.x = 96, .y = 96, .raw = 1};     /* 0x801C5510 */
    Sprite ring = {.x = 110, .y = 104, .semi_trans = 1, .blend = 1, .raw = 1};
    Sprite negcon = {.x = 110, .y = 106, .raw = 1};
    Sprite analog = {.x = 165, .y = 106, .raw = 1};
    int chosen = 0, analog_chosen = 0;
    Setup_Screen_Polys(no_pad_polys);
    Setup_Screen_Text(analog_type_text);
    Setup_Screen_Lines(pad_config_lines);
    Setup_Sprite_Quad(&quads[0], "CARD_ERR", &panel);
    Setup_Sprite_Quad(&quads[1], "RINGS", &ring);
    Setup_Sprite_Quad(&quads[2], "NEG", &negcon);
    Setup_Sprite_Quad(&quads[3], "MAD", &analog);
    for (;;) {
        twist_frame_start();
        if (pad.type != PAD_NEGCON && pad.type != PAD_ANALOG) {
            DrawSync(0);
            return 1;
        }
        if (pad.pressed & PAD_LEFT) {
            Play_Click_FX();
            analog_chosen = 0;
            ring.x = 0x6d;
            Setup_Sprite_Quad(&quads[1], "RINGS", &ring);
        } else if (pad.pressed & PAD_RIGHT) {
            Play_Click_FX();
            analog_chosen = 1;
            ring.x = 0xa4;
            Setup_Sprite_Quad(&quads[1], "RINGS", &ring);
        } else if (pad.pressed & (PAD_CROSS | PAD_START)) {
            Play_Select_FX();
            chosen = 1;
            break;
        } else if (pad.pressed & PAD_TRIANGLE) {
            break;
        }
        DrawSync(0);
        addPrim(&cdb->ot[2], &quads[0]);
        addPrim(&cdb->ot[1], &quads[1]);
        addPrim(&cdb->ot[1], &quads[2]);
        addPrim(&cdb->ot[1], &quads[3]);
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
    DrawSync(0);
    if (chosen) {
        pad_id_negcon = analog_chosen ? PAD_ID_ANALOG : PAD_ID_NEGCON;
        pad_id_analog = analog_chosen ? PAD_ID_NEGCON : PAD_ID_ANALOG;
        Poll_Pad_Buttons();
    }
    return chosen;
}

/* The Controller item: the set-up screen for the kind of pad that is plugged in, following it
 * when it is swapped. Analog pads are first asked which kind they are. A cancelled set-up puts
 * the layout and analog settings back. */
int Select_Control_Method(void)
{
    int32_t layout = pad_layout_setting;
    uint16_t config0 = pad_config[0], config1 = pad_config[1];
    int result = -1;
    Poll_Pad_Buttons();
    if (pad.type == PAD_NEGCON || pad.type == PAD_ANALOG)
        pad.type = PAD_NONE;
    if (pad.type == PAD_DIGITAL) {
        Show_Digital_Pad_Screen();
    } else if (pad.type == PAD_NONE) {
        Setup_Screen_Polys(no_pad_polys);
        Setup_Screen_Text(no_pad_text);
        Setup_Screen_Lines(pad_config_lines);
        Rotate_Slab_On(NULL);
    }
    for (;;) {
        if ((pad.type == PAD_NEGCON || pad.type == PAD_ANALOG) && !Ask_Analog_Pad_Type()) {
            result = 0;
            break;
        }
        if (pad.type == PAD_DIGITAL)
            result = Digital_Pad_Config();
        else if (pad.type == PAD_NONE)
            result = No_Pad_Screen();
        else if (pad.type == PAD_NEGCON)
            result = Control_Method_Menu();
        else if (pad.type == PAD_ANALOG)
            result = Analog_Pad_Config();
        if (result != -1)
            break;
        Poll_Pad_Buttons();
    }
    if (result == 0)
        Setup_Pad(pad.type, layout, config0, config1);
    Rotate_Slab_Off(NULL);
    return 0;
}

/* ---- FRONTEND.BIN: text helpers ---- */

/* A heading: grey text with a dark copy three pixels down and right. */
void Print_Shadowed_Text(int x, int y, const char *text)
{
    Print_Font(1);
    Print_Locate(x, y, 0x100);
    Print_InkRGB(0x80, 0x80, 0x80);
    Print(1, text);
    Print_Font(4);
    Print_Locate(x + 3, y + 3, 0x100);
    Print_InkRGB(0x80, 0x80, 0x80);
    Print(2, text);
}

void Print_White_Text(int x, int y, const char *text)
{
    Print_Font(2);
    Print_Locate(x, y, 0x100);
    Print_InkRGB(0xff, 0xff, 0xff);
    Print(1, text);
}

/* ---- FRONTEND.BIN: information ---- */

#define INFO_TITLE_INK {0x80, 0x64, 0x64, 0}

int32_t stats_available; /* 0x80079770: a championship has been played */

enum {
    IN_STATS, IN_FASTLAP, IN_CREDITS, IN_RING, IN_PAD2, IN_TCANCEL, IN_PAD1, IN_TSELECT, IN_IVIEWSTA, IN_REDCROSS,
    IN_SIDEONLY, IN_TMOVE, INFO_SPRITES
};

static Sprite info_sprites[INFO_SPRITES] = { /* 0x801D60E0 */
    [IN_STATS] = BUTTON_SPRITE(32, 96),
    [IN_FASTLAP] = BUTTON_SPRITE(128, 96),
    [IN_CREDITS] = BUTTON_SPRITE(224, 96),
    [IN_RING] = TRANS_SPRITE(31, 94),
    [IN_PAD2] = BUTTON_SPRITE(260, 24),
    [IN_TCANCEL] = BUTTON_SPRITE(278, 29),
    [IN_PAD1] = BUTTON_SPRITE(260, 41),
    [IN_TSELECT] = BUTTON_SPRITE(278, 46),
    [IN_IVIEWSTA] = BUTTON_SPRITE(16, 24),
    [IN_REDCROSS] = {OFF_SCREEN, OFF_SCREEN, 0x80, 0x80, 0x80, 0, 0, 0, 1},
    [IN_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [IN_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly info_polys[] = { /* 0x801D6224 */
    {2, "STATS", SCREEN_GREY, &info_sprites[IN_STATS]},
    {2, "FASTLAP", SCREEN_GREY, &info_sprites[IN_FASTLAP]},
    {2, "CREDITS", SCREEN_GREY, &info_sprites[IN_CREDITS]},
    {2, "IVIEWSTA", SCREEN_GREY, &info_sprites[IN_IVIEWSTA]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &info_sprites[IN_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &info_sprites[IN_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &info_sprites[IN_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &info_sprites[IN_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &info_sprites[IN_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &info_sprites[IN_TMOVE]},
    {1, "RING", SCREEN_HIGHLIGHT, &info_sprites[IN_RING]},
    {1, "REDCROSS", SCREEN_GREY_TRANS, &info_sprites[IN_REDCROSS]},
    {0, NULL, 0, NULL},
};

static ScreenText info_text[] = { /* 0x801D630C */
    {1, 0, INFO_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Information"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Information"},
    {1, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine info_lines[] = { /* 0x801D6200 */
    {1, 54, 52, 144, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 144, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_info_caption(const char *caption)
{
    info_text[2].text = caption;
    info_text[3].text = caption;
    Setup_Screen_Text(info_text);
}

void Show_View_Statistics_Caption(void)
{
    set_info_caption("%R%JC%T/View Statistics");
}

void Show_View_Lap_Times_Caption(void)
{
    set_info_caption("%R%JC%T/View Lap Times");
}

void Show_View_Credits_Caption(void)
{
    set_info_caption("%R%JC%T/View Credits");
}

/* ---- best laps ---- */

#define BEST_LAP_TRACKS 7
#define BEST_LAPS 5
#define LAP_INK {0xf0, 0xf0, 0xf0, 0}

/* 0x8007152C: the five fastest laps of each race track (the defaults until races are run). */
BestLap best_laps[BEST_LAP_TRACKS][BEST_LAPS] = {
#define ANON(min, sec) {"Anon", min, sec, 0}
    {ANON(0, 25), ANON(0, 26), ANON(0, 27), ANON(0, 28), ANON(0, 29)},
    {ANON(1, 0), ANON(1, 1), ANON(1, 2), ANON(1, 3), ANON(1, 4)},
    {ANON(0, 50), ANON(0, 51), ANON(0, 52), ANON(0, 53), ANON(0, 54)},
    {ANON(0, 26), ANON(0, 27), ANON(0, 28), ANON(0, 29), ANON(0, 30)},
    {ANON(0, 40), ANON(0, 41), ANON(0, 42), ANON(0, 43), ANON(0, 44)},
    {ANON(0, 30), ANON(0, 31), ANON(0, 32), ANON(0, 33), ANON(0, 34)},
    {ANON(0, 45), ANON(0, 46), ANON(0, 47), ANON(0, 48), ANON(0, 49)},
#undef ANON
};

static Sprite best_laps_sprites[5] = { /* 0x801D5840 */
    BUTTON_SPRITE(260, 24), /* ZZZZPAD2 */
    BUTTON_SPRITE(278, 29), /* TCANCEL */
    BUTTON_SPRITE(16, 24),  /* IFASTLAP */
    BUTTON_SPRITE(256, 41), /* SIDEONLY */
    BUTTON_SPRITE(282, 50), /* TMOVE */
};

static ScreenPoly best_laps_polys[] = { /* 0x801D5900 */
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &best_laps_sprites[0]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &best_laps_sprites[1]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &best_laps_sprites[3]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &best_laps_sprites[4]},
    {2, "IFASTLAP", SCREEN_GREY, &best_laps_sprites[2]},
    {0, NULL, 0, NULL},
};

static char best_lap_names[BEST_LAPS][0x20]; /* 0x801D8A40 */
static char best_lap_times[BEST_LAPS][0x20]; /* 0x801D8AE0 */

static ScreenText best_laps_text[] = { /* 0x801D5960 */
    {1, 0, INFO_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Fastest Laps"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Fastest Laps"},
    {2, 5, LAP_INK, -114, -54, SCREEN_Z, ""},
    {2, 5, LAP_INK, -114, -34, SCREEN_Z, "%R%JL%T/Pos."},
    {2, 5, LAP_INK, -64, -34, SCREEN_Z, "%R%JL%T/Driver"},
    {2, 5, LAP_INK, 64, -34, SCREEN_Z, "%R%JL%T/Time"},
    {2, 5, LAP_INK, -114, -16, SCREEN_Z, "%R%JL%T/1st"},
    {2, 5, LAP_INK, -64, -16, SCREEN_Z, best_lap_names[0]},
    {2, 5, LAP_INK, 60, -16, SCREEN_Z, best_lap_times[0]},
    {2, 5, LAP_INK, -114, -2, SCREEN_Z, "%R%JL%T/2nd"},
    {2, 5, LAP_INK, -64, -2, SCREEN_Z, best_lap_names[1]},
    {2, 5, LAP_INK, 60, -2, SCREEN_Z, best_lap_times[1]},
    {2, 5, LAP_INK, -114, 12, SCREEN_Z, "%R%JL%T/3rd"},
    {2, 5, LAP_INK, -64, 12, SCREEN_Z, best_lap_names[2]},
    {2, 5, LAP_INK, 60, 12, SCREEN_Z, best_lap_times[2]},
    {2, 5, LAP_INK, -114, 26, SCREEN_Z, "%R%JL%T/4th"},
    {2, 5, LAP_INK, -64, 26, SCREEN_Z, best_lap_names[3]},
    {2, 5, LAP_INK, 60, 26, SCREEN_Z, best_lap_times[3]},
    {2, 5, LAP_INK, -114, 40, SCREEN_Z, "%R%JL%T/5th"},
    {2, 5, LAP_INK, -64, 40, SCREEN_Z, best_lap_names[4]},
    {2, 5, LAP_INK, 60, 40, SCREEN_Z, best_lap_times[4]},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine best_laps_lines[] = { /* 0x801D58B8 */
    {1, 54, 52, 162, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 162, 0, 0, 0, 1},
    {2, 44, 106, 223, 0x9d, 0xd4, 0xff, 2},
    {2, 90, 93, 88, 0x9d, 0xd2, 0xff, 3},
    {2, 214, 93, 88, 0x9d, 0xd2, 0xff, 3},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const char *const best_lap_track_names[BEST_LAP_TRACKS] = { /* 0x801D5824 */
    "%R%JL%T/Pine Hills Raceway", "%R%JL%T/Chalk Canyon", "%R%JL%T/S.C.A. Motorplex",
    "%R%JL%T/Caprio County Raceway", "%R%JL%T/Black Sail Valley", "%R%JL%T/Liberty City",
    "%R%JL%T/Ultimate Destruction",
};

static const int16_t best_laps_panel[4] = {-120, -38, 232, 95}; /* 0x801D842C */
static int32_t best_laps_track;                                 /* 0x801D8428 */

/* The selected track's table; the time parts pass through print_values like the original. */
void Fill_Best_Laps_Table(void)
{
    best_laps_text[2].text = best_lap_track_names[best_laps_track];
    for (int i = 0; i < BEST_LAPS; i++) {
        const BestLap *lap = &best_laps[best_laps_track][i];
        print_values['0'] = lap->minutes;
        print_values['1'] = lap->seconds;
        print_values['2'] = lap->fraction * 100 >> 16;
        snprintf(best_lap_names[i], sizeof(best_lap_names[i]), "%%R%%JL%%T/%s", lap->name);
        snprintf(best_lap_times[i], sizeof(best_lap_times[i]), "%%R%%JL%%T/%d:%02d:%02d", print_values['0'],
                 print_values['1'], print_values['2']);
    }
}

int Next_Best_Laps_Track(void)
{
    best_laps_track = best_laps_track < 1 ? 6 : best_laps_track - 1;
    Fill_Best_Laps_Table();
    Setup_Screen_Text(best_laps_text);
    return 0;
}

int Prev_Best_Laps_Track(void)
{
    best_laps_track = best_laps_track < 6 ? best_laps_track + 1 : 0;
    Fill_Best_Laps_Table();
    Setup_Screen_Text(best_laps_text);
    return 0;
}

/* The fastest laps per race track on a dark panel; left and right change the track. */
int View_BestLaps(void)
{
    Fill_Best_Laps_Table();
    Setup_Screen_Polys(best_laps_polys);
    Setup_Screen_Text(best_laps_text);
    Setup_Screen_Lines(best_laps_lines);
    Rotate_Slab_On(best_laps_panel);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Next_Best_Laps_Track();
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Prev_Best_Laps_Track();
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(best_laps_panel);
            return 0;
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_Semi_Trans_Poly(best_laps_panel[0], best_laps_panel[1], best_laps_panel[2], best_laps_panel[3]);
        Draw_All(1);
    }
}

/* ---- information menu ---- */

static const SubMenuItem info_items[3] = { /* 0x801D6374 */
    {32, 94, Show_View_Statistics_Caption, Run_Statistics_Menu, Null_Menu_Callback, 0},
    {128, 94, Show_View_Lap_Times_Caption, View_BestLaps, Null_Menu_Callback, 0},
    {225, 94, Show_View_Credits_Caption, Credits_Screen, Null_Menu_Callback, 0x10000},
};

/* With statistics, and without (the ring cannot reach them). */
static const uint8_t info_moves[3][4] = {{0, 0, 1, 0}, {0, 1, 2, 1}, {1, 2, 2, 2}};          /* 0x801D63AC */
static const uint8_t info_moves_no_stats[3][4] = {{1, 1, 1, 1}, {1, 1, 2, 1}, {1, 2, 2, 2}}; /* 0x801D63B8 */
static const char *const info_item_sprites[3] = {"STATS", "FASTLAP", "CREDITS"};    /* 0x801D62F4 */
static const char *const info_pressed_sprites[3] = {"STATSP", "FASTLAPP", "CREDITSP"}; /* 0x801D6300 */

/* Statistics (crossed out until a championship has been played), lap times and credits. */
int Run_Info_Menu(void)
{
    Sprite *ring = &info_sprites[IN_RING], *cross = &info_sprites[IN_REDCROSS];
    const uint8_t(*moves)[4] = stats_available == 1 ? info_moves : info_moves_no_stats;
    bool changed = false;
    if (stats_available) {
        cross->x = cross->y = OFF_SCREEN;
    } else {
        cross->x = 0x33;
        cross->y = 0x70;
    }
    int item = stats_available == 0;
    ring->x = info_items[item].x;
    ring->y = info_items[item].y;
    info_items[item].caption();
    Setup_Screen_Polys(info_polys);
    Setup_Screen_Text(info_text);
    Setup_Screen_Lines(info_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = moves[item][0];
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = moves[item][2];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            info_polys[item].name = info_pressed_sprites[item];
            Setup_Screen_Polys(info_polys);
            Button_Pressed(7, NULL);
            info_polys[item].name = info_item_sprites[item];
            Setup_Screen_Polys(info_polys);
            Button_Pressed(2, NULL);
            Rotate_Slab_Off(NULL);
            if (info_items[item].select())
                return 1;
            info_items[item].caption();
            Setup_Screen_Polys(info_polys);
            Setup_Screen_Text(info_text);
            Setup_Screen_Lines(info_lines);
            Rotate_Slab_On(NULL);
            continue;
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(NULL);
            return 0;
        }
        if (changed) {
            changed = false;
            ring->x = info_items[item].x;
            ring->y = info_items[item].y;
            Setup_Screen_Polys(info_polys);
            info_items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

/* ---- FRONTEND.BIN: name entry ---- */

#define NAME_COLUMNS 14
#define NAME_ROWS 5
#define NAME_LENGTH 8
#define NAME_PREFIX 8 /* "%R%JC%T/" in front of the name being typed */
#define KEY_DELETE (-1)
#define KEY_END (-2)

char lap_name_entry[16]; /* 0x800A716C: the name for a fastest lap */

enum { NE_ICHAMP, NE_PAD2, NE_TCANCEL, NE_PAD1, NE_TSELECT, NE_LETTERS, NE_CURSOR, NE_4WAY, NE_TMOVE, NAME_SPRITES };

static Sprite name_sprites[NAME_SPRITES] = { /* 0x801D6CEC */
    [NE_ICHAMP] = BUTTON_SPRITE(16, 24),
    [NE_PAD2] = BUTTON_SPRITE(260, 24),
    [NE_TCANCEL] = BUTTON_SPRITE(278, 29),
    [NE_PAD1] = BUTTON_SPRITE(260, 41),
    [NE_TSELECT] = BUTTON_SPRITE(278, 46),
    [NE_LETTERS] = BUTTON_SPRITE(32, 120),
    [NE_CURSOR] = BUTTON_SPRITE(48, 123),
    [NE_4WAY] = BUTTON_SPRITE(256, 58),
    [NE_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly name_polys[] = { /* 0x801D6DE8 */
    {2, "CURSOR", SCREEN_GREY, &name_sprites[NE_CURSOR]},
    {2, "LETTERS", SCREEN_GREY_TRANS, &name_sprites[NE_LETTERS]},
    {2, "ICHAMP", SCREEN_GREY, &name_sprites[NE_ICHAMP]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &name_sprites[NE_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &name_sprites[NE_TCANCEL]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &name_sprites[NE_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &name_sprites[NE_TSELECT]},
    {2, "4WAY", SCREEN_GREY_TRANS, &name_sprites[NE_4WAY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &name_sprites[NE_TMOVE]},
    {0, NULL, 0, NULL},
};

static char name_entry_text[] = "%R%JC%T/          "; /* 0x801C524C, shared by the name and its shadow */
static char name_prompt[40];

static ScreenText name_text[] = { /* 0x801D6E88 */
    {1, 0, TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Name Entry"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Name Entry"},
    {1, 0, TITLE_INK, 0, -45, SCREEN_Z, name_entry_text},
    {2, 3, {0, 0, 0, 0}, 3, -42, SCREEN_Z, name_entry_text},
    {1, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {1, 5, {0xff, 0xff, 0xff, 0}, 0, -60, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine name_lines[] = { /* 0x801D6DC4 */
    {1, 54, 52, 145, 0xc8, 0xff, 0xff, 0},
    {2, 57, 55, 145, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* The LETTERS sprite's keys; the last two are delete and end. */
static const int8_t name_keys[NAME_ROWS][NAME_COLUMNS] = { /* 0x801D6F54 */
    {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N'},
    {'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b'},
    {'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'},
    {'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2', '3', '4'},
    {'5', '6', '7', '8', '9', '0', '!', '?', '-', '.', ',', ' ', KEY_DELETE, KEY_END},
};

/* "ToNyPaRk": the outro, then the front end loaded again. */
void Name_Cheat_Outro(void)
{
    Play_Outro_And_Reload();
    DrawSync(0);
    Show_Camcorder_Loading_Screen();
    Setup_Screen_Polys(name_polys);
    Setup_Screen_Text(name_text);
    Setup_Screen_Lines(name_lines);
    Set_CD_Track(12, 1);
    Rotate_Slab_On(NULL);
    Start_CD_Audio();
}

/* "CREDITZ!": the team's faces, to the end-of-season music. */
void Restart_Name_Entry_Screen(void)
{
    Init_Face_Credit_Screen();
    Set_CD_Track(15, 1);
    Start_CD_Audio();
    Secret();
    DrawSync(0);
    Show_Camcorder_Loading_Screen();
    Setup_Screen_Polys(name_polys);
    Setup_Screen_Text(name_text);
    Setup_Screen_Lines(name_lines);
    Set_CD_Track(12, 1);
    Rotate_Slab_On(NULL);
    Start_CD_Audio();
}

/* Up to eight letters picked from the grid with the arrows (held, they repeat) and Cross.
 * `first` 0 asks for the fastest lap's name; otherwise names `first`.. for up to `count`
 * players, stopping at an empty name. Returns how many players there are (at least one), 0
 * when Triangle cancelled. Some names are cheats. */
int Enter_Driver_Names(int first, int count)
{
    int repeat = 0;
    bool changed = false;
    int player = 0;
    while (player < count) {
        name_text[6].text = first == 0 ? "%R%JC%T/Fastest Lap" : "";
        if (first < 2) {
            name_text[4].text = name_text[5].text = "%R%JC%T/Enter your name";
        } else {
            snprintf(name_prompt, sizeof(name_prompt), "%s player %d", "%R%JC%T/Enter your name", player + 1);
            name_text[4].text = name_text[5].text = name_prompt;
        }
        int column = 0, row = 0, length = 0;
        name_sprites[NE_CURSOR].x = 0x30;
        name_sprites[NE_CURSOR].y = 0x7b;
        name_entry_text[NAME_PREFIX] = '\0';
        Setup_Screen_Polys(name_polys);
        Setup_Screen_Text(name_text);
        Setup_Screen_Lines(name_lines);
        if (player == 0)
            Rotate_Slab_On(NULL);
        for (;;) {
            Debug_Stub();
            Swap_Draw_Buffers();
            Poll_Pad_Buttons();
            if (repeat > 0)
                repeat--;
            uint16_t held = pad.held;
            if (held & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) {
                if (repeat < 2) {
                    repeat = repeat == 1 ? 5 : 20;
                    if (held & PAD_LEFT)
                        column = column < 1 ? NAME_COLUMNS - 1 : column - 1;
                    else if (held & PAD_RIGHT)
                        column = column < NAME_COLUMNS - 1 ? column + 1 : 0;
                    else if (held & PAD_UP)
                        row = row < 1 ? NAME_ROWS - 1 : row - 1;
                    else
                        row = row < NAME_ROWS - 1 ? row + 1 : 0;
                    changed = true;
                }
            } else if (pad.pressed & PAD_TRIANGLE) {
                Rotate_Slab_Off(NULL);
                return 0;
            } else if (!(pad.pressed & PAD_CROSS)) {
                repeat = 0;
            } else {
                Play_Select_FX();
                int key = name_keys[row][column];
                if (key == KEY_END)
                    break;
                if (key == KEY_DELETE) {
                    if (length > 0) {
                        name_entry_text[NAME_PREFIX + length - 1] = '\0';
                        changed = true;
                        length--;
                    }
                } else if (length < NAME_LENGTH) {
                    name_entry_text[NAME_PREFIX + length] = (char)key;
                    name_entry_text[NAME_PREFIX + length + 1] = '\0';
                    changed = true;
                    length++;
                }
            }
            if (changed) {
                changed = false;
                Set_Highlight_Grey();
                Play_Click_FX();
                name_sprites[NE_CURSOR].x = (int16_t)(column * 16 + 0x30);
                name_sprites[NE_CURSOR].y = (int16_t)(row * 0x11 + 0x7b);
                Setup_Screen_Text(name_text);
                Setup_Screen_Polys(name_polys);
            }
            Draw_Slab();
            Print_Draw();
            Draw_Screen_Polys();
            Draw_Screen_Lines();
            Draw_All(1);
        }

        const char *typed = name_entry_text + NAME_PREFIX;
        char *name = player_names[player];
        if (first == 0) {
            snprintf(lap_name_entry, sizeof(lap_name_entry), "%s", length == 0 ? "Anonymous" : typed);
        } else if (length == 0) {
            snprintf(name, sizeof(player_names[0]), "Player %d", player + 1);
        } else {
            snprintf(name, sizeof(player_names[0]), "%s", typed);
        }
        if (strcmp(name, "CREDITZ!") == 0 && pad.type == PAD_DIGITAL)
            Restart_Name_Entry_Screen();
        if (strcmp(name, "MACSrPOO") == 0) {
            race_track_count = 7;
            arena_count = 4;
        }
        if (strcmp(name, "ToNyPaRk") == 0)
            Name_Cheat_Outro();
        if (length == 0 && player >= first - 1)
            break;
        player++;
    }
    Rotate_Slab_Off(NULL);
    return player < 1 ? 1 : player;
}

/* ---- FRONTEND.BIN: go ---- */

int Select_Go_Start_Race(void)
{
    Start_Selected_Race();
    return 0;
}

/* ---- FRONTEND.BIN: after a practice race ---- */

enum { PO_VIEWREP, PO_SAVREP, PO_RING, PO_RINGS, PO_PAD1, PO_TSELECT, PO_IMEMCARD, PO_GO, PO_4WAY, PO_TMOVE, PRACTICE_OVER_SPRITES };

static Sprite practice_over_sprites[PRACTICE_OVER_SPRITES] = { /* 0x801D79D0 */
    [PO_VIEWREP] = BUTTON_SPRITE(64, 96),
    [PO_SAVREP] = BUTTON_SPRITE(192, 96),
    [PO_RING] = TRANS_SPRITE(63, 94),
    [PO_RINGS] = TRANS_SPRITE(-1000, -1000),
    [PO_PAD1] = BUTTON_SPRITE(260, 24),
    [PO_TSELECT] = BUTTON_SPRITE(278, 29),
    [PO_IMEMCARD] = BUTTON_SPRITE(16, 24),
    [PO_GO] = BUTTON_SPRITE(262, 192),
    [PO_4WAY] = BUTTON_SPRITE(256, 41),
    [PO_TMOVE] = BUTTON_SPRITE(282, 50),
};

static ScreenPoly practice_over_polys[] = { /* 0x801D7AE4 */
    {4, "VIEWREP", SCREEN_GREY, &practice_over_sprites[PO_VIEWREP]},
    {4, "SAVREP", SCREEN_GREY, &practice_over_sprites[PO_SAVREP]},
    {4, "GO", SCREEN_GREY, &practice_over_sprites[PO_GO]},
    {4, "ZZZZPAD1", SCREEN_GREY_TRANS, &practice_over_sprites[PO_PAD1]},
    {4, "TSELECT", SCREEN_GREY_TRANS, &practice_over_sprites[PO_TSELECT]},
    {2, "4WAY", SCREEN_GREY_TRANS, &practice_over_sprites[PO_4WAY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &practice_over_sprites[PO_TMOVE]},
    {3, "RING", SCREEN_HIGHLIGHT, &practice_over_sprites[PO_RING]},
    {3, "RINGS", SCREEN_HIGHLIGHT, &practice_over_sprites[PO_RINGS]},
    {4, "IMEMCARD", SCREEN_GREY, &practice_over_sprites[PO_IMEMCARD]},
    {0, NULL, 0, NULL},
};

static ScreenText practice_over_text[] = { /* 0x801D7BAC */
    {3, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Race Over"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Race Over"},
    {3, 1, GREY_INK, 0, 95, SCREEN_Z, ""},
    {4, 4, GREY_INK, 3, 98, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine practice_over_lines[] = { /* 0x801D7AC0 */
    {1, 54, 52, 122, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 122, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_practice_over_caption(const char *caption)
{
    practice_over_text[2].text = caption;
    practice_over_text[3].text = caption;
    Setup_Screen_Text(practice_over_text);
}

void Show_Practice_View_Replay_Caption(void)
{
    set_practice_over_caption("%R%JC%T/View Replay");
}

void Show_Practice_Save_Replay_Caption(void)
{
    set_practice_over_caption("%R%JC%T/Save Replay");
}

void Show_Practice_Quit_Caption(void)
{
    set_practice_over_caption("%R%JC%T/Quit");
}

int Select_Practice_View_Replay(void)
{
    Loading_Screen_From_Slab(NULL);
    View_Frontend_Replay();
    Show_Camcorder_Loading_Screen();
    return 0;
}

static int Select_Practice_Save_Replay(void) /* 0x801D2474 */
{
    Save_Race_Replay();
    return 0;
}

static int Select_Practice_Quit(void) /* 0x801D24B0 */
{
    return 1;
}

static const MenuItem practice_over_items[3] = { /* 0x801D7C10 */
    {0, 66, 94, Show_Practice_View_Replay_Caption, Select_Practice_View_Replay, Null_Menu_Callback},
    {0, 192, 94, Show_Practice_Save_Replay_Caption, Select_Practice_Save_Replay, Null_Menu_Callback},
    {1, 262, 190, Show_Practice_Quit_Caption, Select_Practice_Quit, Null_Menu_Callback},
};

/* Left, up, right and down from each item (0x801D7C4C). */
static const uint8_t practice_over_moves[3][4] = {{0, 0, 1, 2}, {0, 1, 1, 2}, {2, 1, 2, 2}};
static const char *const practice_over_item_sprites[3] = {"VIEWREP", "SAVREP", "GO"};     /* 0x801D7B94 */
static const char *const practice_over_pressed_sprites[3] = {"VIEWREPP", "SAVREPP", "GOP"}; /* 0x801D7BA0 */

/* The ring on the top row (RING) or round GO (RINGS); the other one off the screen. */
static void practice_over_ring(int item)
{
    Sprite *on = &practice_over_sprites[practice_over_items[item].row == 0 ? PO_RING : PO_RINGS];
    Sprite *off = &practice_over_sprites[practice_over_items[item].row == 0 ? PO_RINGS : PO_RING];
    on->x = practice_over_items[item].x;
    on->y = practice_over_items[item].y;
    off->x = -1000;
    off->y = -1000;
}

/* After a practice race: view the replay, save it, or go back to the menus. */
int Practice_Over(void)
{
    int item = 0;
    bool changed = false;
    Show_Camcorder_Loading_Screen();
    practice_over_ring(0);
    practice_over_items[0].caption();
    Setup_Screen_Polys(practice_over_polys);
    Setup_Screen_Text(practice_over_text);
    Setup_Screen_Lines(practice_over_lines);
    Set_CD_Track(FRONT_END_CD_TRACK, 1);
    Rotate_Slab_On(NULL);
    Start_CD_Audio();
    restart_cd_audio = 0;
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        int move = -1;
        if (pad.pressed & PAD_LEFT)
            move = 0;
        else if (pad.pressed & PAD_RIGHT)
            move = 2;
        else if (pad.pressed & PAD_UP)
            move = 1;
        else if (pad.pressed & PAD_DOWN)
            move = 3;
        if (move >= 0) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = practice_over_moves[item][move];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            practice_over_polys[item].name = practice_over_pressed_sprites[item];
            Setup_Screen_Polys(practice_over_polys);
            Button_Pressed(7, NULL);
            practice_over_polys[item].name = practice_over_item_sprites[item];
            Setup_Screen_Polys(practice_over_polys);
            Button_Pressed(2, NULL);
            Rotate_Slab_Off(NULL);
            if (practice_over_items[item].enter())
                return 1;
            practice_over_items[item].caption();
            Setup_Screen_Polys(practice_over_polys);
            Setup_Screen_Text(practice_over_text);
            Setup_Screen_Lines(practice_over_lines);
            Rotate_Slab_On(NULL);
            changed = true;
            /* Rotate_Slab_On drew the last frame into this OT: drawing again before the next swap links the
             * slab into it twice, a loop DrawOTag never leaves. */
            continue;
        }
        if (changed) {
            changed = false;
            practice_over_ring(item);
            Setup_Screen_Polys(practice_over_polys);
            practice_over_items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

/* ---- RESULTS.BIN: between championship races ---- */

#define TABLE_ROWS 20

/* A table of the twenty drivers in two columns of ten, under a title: place, name and points. */
typedef struct {
    char names[TABLE_ROWS][0x1a];
    char points[TABLE_ROWS][0x10];
    ScreenText text[8 + TABLE_ROWS * 3 + 1];
} DriverTable;

static const char *const table_places[TABLE_ROWS] = {
    "%R%JL%T/1", "%R%JL%T/2", "%R%JL%T/3", "%R%JL%T/4", "%R%JL%T/5", "%R%JL%T/6", "%R%JL%T/7",
    "%R%JL%T/8", "%R%JL%T/9", "%R%JL%T/10", "%R%JL%T/11", "%R%JL%T/12", "%R%JL%T/13", "%R%JL%T/14",
    "%R%JL%T/15", "%R%JL%T/16", "%R%JL%T/17", "%R%JL%T/18", "%R%JL%T/19", "%R%JL%T/20",
};

#define RESULTS_INK {0xff, 0xff, 0xff, 0}

/* The text records of a driver table (0x801CABF0 for the race results, 0x801CA29C for the multi-player
 * league): the original lists them out; they follow this pattern. */
static void build_driver_table(DriverTable *t, const char *title)
{
    static const int16_t columns[2][3] = {{-138, -114, -28}, {4, 28, 116}};
    ScreenText *p = t->text;
    *p++ = (ScreenText){1, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, title};
    *p++ = (ScreenText){2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, title};
    for (int c = 0; c < 2; c++) {
        *p++ = (ScreenText){2, 2, RESULTS_INK, columns[c][0], -38, SCREEN_Z, "%R%JL%T/Ps."};
        *p++ = (ScreenText){2, 2, RESULTS_INK, columns[c][1], -38, SCREEN_Z, "%R%JL%T/Driver"};
        *p++ = (ScreenText){2, 2, RESULTS_INK, columns[c][2], -38, SCREEN_Z, "%R%JL%T/Pts."};
    }
    for (int r = 0; r < TABLE_ROWS; r++) {
        const int16_t *col = columns[r / 10];
        int16_t y = (int16_t)(-22 + r % 10 * 10);
        *p++ = (ScreenText){2, 2, RESULTS_INK, col[0], y, SCREEN_Z, table_places[r]};
        *p++ = (ScreenText){2, 2, RESULTS_INK, col[1], y, SCREEN_Z, t->names[r]};
        *p++ = (ScreenText){2, 2, RESULTS_INK, col[2], y, SCREEN_Z, t->points[r]};
    }
    *p = (ScreenText){0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL};
}

/* Back (Triangle) only, over a dark panel. */
static void show_driver_table(ScreenPoly *polys, DriverTable *t, const ScreenLine *lines, const int16_t *panel)
{
    Setup_Screen_Polys(polys);
    Setup_Screen_Text(t->text);
    Setup_Screen_Lines(lines);
    Rotate_Slab_On(panel);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_TRIANGLE)
            break;
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_Semi_Trans_Poly(panel[0], panel[1], panel[2], panel[3]);
        Draw_All(1);
    }
    Rotate_Slab_Off(panel);
}

enum { DT_PAD2, DT_TCANCEL, DT_IRESULTS, DT_SIDEONLY, DT_TMOVE, DRIVER_TABLE_SPRITES };

static Sprite driver_table_sprites[DRIVER_TABLE_SPRITES] = { /* 0x801CAAFC, 0x801CA1A8 */
    [DT_PAD2] = BUTTON_SPRITE(260, 24),
    [DT_TCANCEL] = BUTTON_SPRITE(278, 29),
    [DT_IRESULTS] = BUTTON_SPRITE(16, 24),
    [DT_SIDEONLY] = BUTTON_SPRITE(256, 41), /* the league's (0x801C9AE0) */
    [DT_TMOVE] = BUTTON_SPRITE(282, 50),
};

static ScreenPoly driver_table_polys[] = { /* 0x801CABB0, 0x801CA25C */
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &driver_table_sprites[DT_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &driver_table_sprites[DT_TCANCEL]},
    {2, "IRESULTS", SCREEN_GREY, &driver_table_sprites[DT_IRESULTS]},
    {0, NULL, 0, NULL},
};

static const ScreenLine results_lines[] = { /* 0x801CAB44 */
    {1, 54, 52, 96, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 96, 0, 0, 0, 1},
    {2, 21, 100, 131, 0x9d, 0xd2, 0xff, 2},
    {2, 163, 100, 132, 0x9d, 0xd2, 0xff, 2},
    {2, 41, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {2, 127, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {2, 183, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {2, 271, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const ScreenLine multi_league_lines[] = { /* 0x801CA1F0: the title is longer */
    {1, 54, 52, 177, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 177, 0, 0, 0, 1},
    {2, 21, 100, 131, 0x9d, 0xd2, 0xff, 2},
    {2, 163, 100, 132, 0x9d, 0xd2, 0xff, 2},
    {2, 41, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {2, 127, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {2, 183, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {2, 271, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const int16_t driver_table_panel[4] = {-142, -42, 282, 120}; /* 0x801CB6E0, 0x801CB664 */

static DriverTable results_table;      /* 0x801CBBA8, 0x801CBDB0 */
static DriverTable multi_league_table; /* 0x801CB860, 0x801CBA68 */

void Fill_Race_Results_Table(void)
{
    for (int i = 0; i < TABLE_ROWS; i++) {
        int row = champ_info[i].finish_position - 1;
        snprintf(results_table.names[row], sizeof(results_table.names[row]), "%s%s", "%R%JL%T/", champ_info[i].name);
        snprintf(results_table.points[row], sizeof(results_table.points[row]), "%s%d", "%R%JL%T/", champ_info[i].race_points);
    }
}

int View_Race_Results(void)
{
    Fill_Race_Results_Table();
    build_driver_table(&results_table, "%R%JL%T/Results");
    show_driver_table(driver_table_polys, &results_table, results_lines, driver_table_panel);
    return 0;
}

void Fill_MultiLeague_Table(void)
{
    for (int i = 0; i < TABLE_ROWS; i++) {
        int row = champ_info[i].rank;
        snprintf(multi_league_table.names[row], sizeof(multi_league_table.names[row]), "%s%s", "%R%JL%T/", champ_info[i].name);
        snprintf(multi_league_table.points[row], sizeof(multi_league_table.points[row]), "%s%d", "%R%JL%T/", champ_info[i].points);
    }
}

int View_MultiLeague(void)
{
    Fill_MultiLeague_Table();
    build_driver_table(&multi_league_table, "%R%JL%T/Championship");
    show_driver_table(driver_table_polys, &multi_league_table, multi_league_lines, driver_table_panel);
    return 0;
}

/* The single-player league, a division at a time (Left and Right). */
static ScreenPoly league_polys[] = { /* 0x801C9B58 */
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &driver_table_sprites[DT_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &driver_table_sprites[DT_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &driver_table_sprites[DT_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &driver_table_sprites[DT_TMOVE]},
    {2, "IRESULTS", SCREEN_GREY, &driver_table_sprites[DT_IRESULTS]},
    {0, NULL, 0, NULL},
};

static char league_names[DIVISION_SLOTS][0x1a];  /* 0x801CB78C */
static char league_points[DIVISION_SLOTS][0x10]; /* 0x801CB810 */

#define LEAGUE_INK {0xf0, 0xf0, 0xf0, 0}

static ScreenText league_text[] = { /* 0x801C9BB8 */
    {1, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Championship"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Championship"},
    {2, 5, LEAGUE_INK, -114, -54, SCREEN_Z, ""},
    {2, 5, LEAGUE_INK, -114, -34, SCREEN_Z, "%R%JL%T/Pos."},
    {2, 5, LEAGUE_INK, -64, -34, SCREEN_Z, "%R%JL%T/Driver"},
    {2, 5, LEAGUE_INK, 64, -34, SCREEN_Z, "%R%JL%T/Points"},
    {2, 5, LEAGUE_INK, -114, -16, SCREEN_Z, "%R%JL%T/1st"},
    {2, 5, LEAGUE_INK, -64, -16, SCREEN_Z, league_names[0]},
    {2, 5, LEAGUE_INK, 64, -16, SCREEN_Z, league_points[0]},
    {2, 5, LEAGUE_INK, -114, -2, SCREEN_Z, "%R%JL%T/2nd"},
    {2, 5, LEAGUE_INK, -64, -2, SCREEN_Z, league_names[1]},
    {2, 5, LEAGUE_INK, 64, -2, SCREEN_Z, league_points[1]},
    {2, 5, LEAGUE_INK, -114, 12, SCREEN_Z, "%R%JL%T/3rd"},
    {2, 5, LEAGUE_INK, -64, 12, SCREEN_Z, league_names[2]},
    {2, 5, LEAGUE_INK, 64, 12, SCREEN_Z, league_points[2]},
    {2, 5, LEAGUE_INK, -114, 26, SCREEN_Z, "%R%JL%T/4th"},
    {2, 5, LEAGUE_INK, -64, 26, SCREEN_Z, league_names[3]},
    {2, 5, LEAGUE_INK, 64, 26, SCREEN_Z, league_points[3]},
    {2, 5, LEAGUE_INK, -114, 40, SCREEN_Z, "%R%JL%T/5th"},
    {2, 5, LEAGUE_INK, -64, 40, SCREEN_Z, league_names[4]},
    {2, 5, LEAGUE_INK, 64, 40, SCREEN_Z, league_points[4]},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine league_lines[] = { /* 0x801C9B10 */
    {1, 54, 52, 177, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 177, 0, 0, 0, 1},
    {2, 44, 106, 223, 0x9d, 0xd4, 0xff, 2},
    {2, 90, 93, 88, 0x9d, 0xd2, 0xff, 3},
    {2, 218, 93, 88, 0x9d, 0xd2, 0xff, 3},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const int16_t league_panel[4] = {-120, -38, 232, 95}; /* 0x801CB5B0 */
static int32_t league_division_shown; /* 0x801CB5AC */

void Fill_League_Table(void)
{
    static const char *const titles[LEAGUE_DIVISIONS] = {
        "%R%JL%T/Division 1", "%R%JL%T/Division 2", "%R%JL%T/Division 3", "%R%JL%T/Division 4",
    };
    if (league_division_shown >= 0 && league_division_shown < LEAGUE_DIVISIONS)
        league_text[2].text = titles[league_division_shown];
    for (int i = 0; i < num_cars; i++) {
        int row = champ_info[i].rank;
        if (champ_info[i].division != league_division_shown)
            continue;
        snprintf(league_names[row], sizeof(league_names[row]), "%s%s", "%R%JL%T/", champ_info[i].name);
        snprintf(league_points[row], sizeof(league_points[row]), "%s%d", "%R%JL%T/", champ_info[i].points);
    }
}

int Prev_League_Division(void)
{
    league_division_shown = league_division_shown < 1 ? 3 : league_division_shown - 1;
    Fill_League_Table();
    DrawSync(0);
    Setup_Screen_Text(league_text);
    return 0;
}

int Next_League_Division(void)
{
    league_division_shown = league_division_shown < 3 ? league_division_shown + 1 : 0;
    Fill_League_Table();
    DrawSync(0);
    Setup_Screen_Text(league_text);
    return 0;
}

int View_League(void)
{
    Fill_League_Table();
    Setup_Screen_Polys(league_polys);
    Setup_Screen_Text(league_text);
    Setup_Screen_Lines(league_lines);
    Rotate_Slab_On(league_panel);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Prev_League_Division();
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Next_League_Division();
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(league_panel);
            return 0;
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_Semi_Trans_Poly(league_panel[0], league_panel[1], league_panel[2], league_panel[3]);
        Draw_All(1);
    }
}

/* The season status menu between races. */
enum {
    SS_RESULTS, SS_CHAMP, SS_REPLAY, SS_CARD, SS_STATS, SS_RING, SS_RINGS, SS_PAD2, SS_QUIT, SS_PAD1, SS_TSELECT,
    SS_IRESULTS, SS_GO, SS_SCROSS, SS_4WAY, SS_TMOVE, SEASON_STATUS_SPRITES
};

static Sprite season_status_sprites[SEASON_STATUS_SPRITES] = { /* 0x801CB164 */
    [SS_RESULTS] = BUTTON_SPRITE(32, 96),
    [SS_CHAMP] = BUTTON_SPRITE(128, 96),
    [SS_REPLAY] = BUTTON_SPRITE(224, 96),
    [SS_CARD] = BUTTON_SPRITE(88, 154),
    [SS_STATS] = BUTTON_SPRITE(184, 154),
    [SS_RING] = TRANS_SPRITE(31, 94),
    [SS_RINGS] = TRANS_SPRITE(OFF_SCREEN, OFF_SCREEN),
    [SS_PAD2] = BUTTON_SPRITE(260, 24),
    [SS_QUIT] = BUTTON_SPRITE(278, 29),
    [SS_PAD1] = BUTTON_SPRITE(260, 41),
    [SS_TSELECT] = BUTTON_SPRITE(278, 46),
    [SS_IRESULTS] = BUTTON_SPRITE(16, 24),
    [SS_GO] = BUTTON_SPRITE(262, 192),
    [SS_SCROSS] = {184, 154, 128, 128, 128, 0, 0, 0, 1, 0},
    [SS_4WAY] = BUTTON_SPRITE(256, 58),
    [SS_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly season_status_polys[] = { /* 0x801CB308 */
    {2, "RESULTS", SCREEN_GREY, &season_status_sprites[SS_RESULTS]},
    {2, "CHAMP", SCREEN_GREY, &season_status_sprites[SS_CHAMP]},
    {2, "REPLAY", SCREEN_GREY, &season_status_sprites[SS_REPLAY]},
    {2, "CARD", SCREEN_GREY, &season_status_sprites[SS_CARD]},
    {2, "STATS", SCREEN_GREY, &season_status_sprites[SS_STATS]},
    {2, "GO", SCREEN_GREY, &season_status_sprites[SS_GO]},
    {1, "RING", SCREEN_HIGHLIGHT, &season_status_sprites[SS_RING]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &season_status_sprites[SS_RINGS]},
    {1, "SCROSS", SCREEN_GREY_TRANS, &season_status_sprites[SS_SCROSS]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &season_status_sprites[SS_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &season_status_sprites[SS_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &season_status_sprites[SS_PAD2]},
    {2, "QUIT", SCREEN_GREY_TRANS, &season_status_sprites[SS_QUIT]},
    {2, "4WAY", SCREEN_GREY_TRANS, &season_status_sprites[SS_4WAY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &season_status_sprites[SS_TMOVE]},
    {2, "IRESULTS", SCREEN_GREY, &season_status_sprites[SS_IRESULTS]},
    {0, NULL, 0, NULL},
};

static ScreenText season_status_text[] = { /* 0x801CB448 */
    {3, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Race Over"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Race Over"},
    {3, 1, GREY_INK, 0, 95, SCREEN_Z, ""},
    {4, 4, GREY_INK, 3, 98, SCREEN_Z, ""},
    {1, 2, {0, 0, 0, 0}, -23, -13, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine season_status_lines[] = { /* 0x801CB2E4 */
    {3, 54, 52, 124, 0xff, 0xc8, 0xc8, 0},
    {4, 57, 55, 124, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* ---- the save options: the championship or the last race's replay ---- */

static int32_t season_end_kind; /* 0x801CB568: 0 another season, 1 back to the title, 2 the end sequence */

enum { SV_RING, SV_RINGS, SV_PAD2, SV_TCANCEL, SV_PAD1, SV_TSELECT, SV_IMEMCARD, SV_REDCROSS, SV_SIDEONLY, SV_TMOVE,
       SV_SAVEGAME, SV_SAVREPLY, SAVE_SPRITES };

static Sprite save_sprites[SAVE_SPRITES] = { /* 0x801C97E0 */
    [SV_SAVEGAME] = BUTTON_SPRITE(64, 96),
    [SV_SAVREPLY] = BUTTON_SPRITE(192, 96),
    [SV_RING] = TRANS_SPRITE(63, 94),
    [SV_RINGS] = TRANS_SPRITE(OFF_SCREEN, OFF_SCREEN),
    [SV_PAD2] = BUTTON_SPRITE(260, 24),
    [SV_TCANCEL] = BUTTON_SPRITE(278, 29),
    [SV_PAD1] = BUTTON_SPRITE(260, 41),
    [SV_TSELECT] = BUTTON_SPRITE(278, 46),
    [SV_IMEMCARD] = BUTTON_SPRITE(16, 24),
    [SV_REDCROSS] = {OFF_SCREEN, OFF_SCREEN, 128, 128, 128, 0, 0, 0, 1, 0},
    [SV_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [SV_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly save_polys[] = { /* 0x801C9924 */
    {2, "SAVEGAME", SCREEN_GREY, &save_sprites[SV_SAVEGAME]},
    {2, "SAVREPLY", SCREEN_GREY, &save_sprites[SV_SAVREPLY]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &save_sprites[SV_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &save_sprites[SV_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &save_sprites[SV_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &save_sprites[SV_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &save_sprites[SV_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &save_sprites[SV_TMOVE]},
    {1, "RING", SCREEN_HIGHLIGHT, &save_sprites[SV_RING]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &save_sprites[SV_RINGS]},
    {2, "IMEMCARD", SCREEN_GREY, &save_sprites[SV_IMEMCARD]},
    {1, "REDCROSS", SCREEN_GREY_TRANS, &save_sprites[SV_REDCROSS]},
    {0, NULL, 0, NULL},
};

static ScreenText save_text[] = { /* 0x801C9A04 */
    {1, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Save Options"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Save Options"},
    {1, 1, GREY_INK, 0, 95, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 98, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine save_lines[] = { /* 0x801C9900 */
    {1, 54, 52, 165, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 165, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_save_caption(const char *caption)
{
    save_text[2].text = caption;
    save_text[3].text = caption;
    Setup_Screen_Text(save_text);
}

void Show_File_Save_Game_Caption(void)
{
    set_save_caption("%R%JC%T/Save Game");
}

void Show_File_Save_Replay_Caption(void)
{
    set_save_caption("%R%JC%T/Save Replay");
}

int Select_File_Save_Game(void)
{
    Save_Game_State_To_Card();
    return 0;
}

int Select_File_Save_Replay(void)
{
    Save_Race_Replay();
    return 0;
}

static MenuItem save_items[2] = { /* 0x801C9A68 */
    {0, 66, 94, Show_File_Save_Game_Caption, Select_File_Save_Game, Null_Menu_Callback},
    {0, 192, 94, Show_File_Save_Replay_Caption, Select_File_Save_Replay, Null_Menu_Callback},
};

static uint8_t save_moves[2][4] = {{0, 0, 1, 0}, {0, 1, 1, 1}}; /* 0x801C9A90: left, up, right, down */
static const char *const save_item_sprites[2] = {"SAVEGAME", "SAVREPLY"};    /* 0x801C99F4 */
static const char *const save_pressed_sprites[2] = {"SAVGAMEP", "SAVRPLYP"}; /* 0x801C99FC */

static void save_menu_ring(int item)
{
    Sprite *on = &save_sprites[save_items[item].row == 0 ? SV_RING : SV_RINGS];
    Sprite *off = &save_sprites[save_items[item].row == 0 ? SV_RINGS : SV_RING];
    on->x = save_items[item].x;
    on->y = save_items[item].y;
    off->x = off->y = OFF_SCREEN;
}

/* Saving the championship or the replay. With no next season to save the game is crossed out and the
 * ring starts on the replay. Triangle returns 0. */
int Save_Game(void) /* 0x801C4934 */
{
    bool changed = false;
    int item = season_end_kind != 0;
    if (season_end_kind == 0) {
        save_sprites[SV_REDCROSS].x = save_sprites[SV_REDCROSS].y = OFF_SCREEN;
    } else {
        save_sprites[SV_REDCROSS].x = 0x53;
        save_sprites[SV_REDCROSS].y = 0x71;
    }
    /* The original only ever writes the 1; RESULTS.BIN being loaded again puts the 0 back. */
    save_moves[1][0] = (uint8_t)(season_end_kind != 0);
    save_menu_ring(item);
    save_items[item].caption();
    Setup_Screen_Polys(save_polys);
    Setup_Screen_Text(save_text);
    Setup_Screen_Lines(save_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        int move = -1;
        if (pad.pressed & PAD_LEFT)
            move = 0;
        else if (pad.pressed & PAD_RIGHT)
            move = 2;
        else if (pad.pressed & PAD_UP)
            move = 1;
        else if (pad.pressed & PAD_DOWN)
            move = 3;
        if (move >= 0) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = save_moves[item][move];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            save_polys[item].name = save_pressed_sprites[item];
            Setup_Screen_Polys(save_polys);
            Button_Pressed(7, NULL);
            save_polys[item].name = save_item_sprites[item];
            Setup_Screen_Polys(save_polys);
            Button_Pressed(2, NULL);
            Rotate_Slab_Off(NULL);
            if (save_items[item].enter())
                return 1;
            save_items[item].caption();
            Setup_Screen_Polys(save_polys);
            Setup_Screen_Text(save_text);
            Setup_Screen_Lines(save_lines);
            Rotate_Slab_On(NULL);
            continue;
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(NULL);
            return 0;
        }
        if (changed) {
            changed = false;
            save_menu_ring(item);
            Setup_Screen_Polys(save_polys);
            save_items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

static int Select_View_Results_Replay(void) /* 0x801C5180 */
{
    current_level = LEVEL_FRONT_END;
    Loading_Screen_From_Slab(NULL);
    View_Results_Replay();
    Show_Camcorder_Loading_Screen();
    return 0;
}

static int Select_Status_Next_Race(void) /* 0x801C8F80 */
{
    return 1;
}

static MenuItem season_status_items[6] = { /* 0x801CB4C0 */
    {0, 33, 94, Show_Status_View_Results_Caption, View_Race_Results, Null_Menu_Callback},
    {0, 129, 94, Show_Status_View_League_Caption, View_League, Null_Menu_Callback},
    {0, 225, 94, Show_Status_View_Replay_Caption, Select_View_Results_Replay, Null_Menu_Callback},
    {1, 89, 152, Show_Status_File_Options_Caption, Save_Game, Null_Menu_Callback},
    {1, 185, 152, Show_Status_View_Statistics_Caption, Run_Statistics_Menu, Null_Menu_Callback},
    {1, 262, 190, Show_Status_Next_Race_Caption, Select_Status_Next_Race, Null_Menu_Callback},
};

/* Left, up, right and down from each item; in a multi-player championship the statistics button is
 * crossed out and skipped (0x801CB538, 0x801CB550). */
static const uint8_t season_status_moves[2][6][4] = {
    {{0, 0, 1, 3}, {0, 1, 2, 4}, {1, 2, 2, 4}, {3, 0, 4, 5}, {3, 1, 4, 5}, {5, 4, 5, 5}},
    {{0, 0, 1, 3}, {0, 1, 2, 3}, {1, 2, 2, 3}, {3, 0, 3, 5}, {3, 3, 3, 3}, {5, 3, 5, 5}},
};
static const char *const season_status_item_sprites[6] = {"RESULTS", "CHAMP", "REPLAY", "CARD", "STATS", "GO"}; /* 0x801CB418 */
static const char *const season_status_pressed_sprites[6] = {"RESULTSP", "CHAMPP", "REPLAYP", "CARDP", "STATSP", "GOP"}; /* 0x801CB430 */

/* The multi-player league replaces the divisions, and the statistics are crossed out. */
static void Set_Season_Status_Mode(void) /* 0x801C9754 */
{
    if (race_type == RACE_MULTIPLAYER) {
        season_status_items[1].enter = View_MultiLeague;
        season_status_sprites[SS_SCROSS].x = 0xc6;
        season_status_sprites[SS_SCROSS].y = 0xa5;
    } else {
        season_status_items[1].enter = View_League;
        season_status_sprites[SS_SCROSS].x = OFF_SCREEN;
        season_status_sprites[SS_SCROSS].y = OFF_SCREEN;
    }
}

static void set_season_status_caption(const char *caption)
{
    Set_Season_Status_Mode();
    season_status_text[2].text = caption;
    season_status_text[3].text = caption;
    Setup_Screen_Text(season_status_text);
}

void Show_Status_View_Results_Caption(void)
{
    set_season_status_caption("%R%JC%T/View Results");
}

void Show_Status_View_League_Caption(void)
{
    set_season_status_caption("%R%JC%T/View League");
}

void Show_Status_View_Replay_Caption(void)
{
    set_season_status_caption("%R%JC%T/View Replay");
}

void Show_Status_File_Options_Caption(void)
{
    set_season_status_caption("%R%JC%T/Memory card Options");
}

void Show_Status_View_Statistics_Caption(void)
{
    set_season_status_caption("%R%JC%T/View Statistics");
}

void Show_Status_Next_Race_Caption(void)
{
    set_season_status_caption("%R%JC%T/Next Race");
}

/* "Quit season?" with a tick and a cross, on line 4 of the screen's text: Left the tick, Right the
 * cross, Cross confirms, Triangle is no. The quads go straight into the OT (the original keeps them
 * on the stack). Each RESULTS menu has its own copy of this (0x801C8FAC, 0x801C63C0, 0x801C7A24). */
static int ask_quit_season(ScreenText *text)
{
    static POLY_FT4 quads[5];
    Sprite error = {.x = 96, .y = 96, .raw = 1};
    Sprite yes_no = {.x = 102, .y = 101, .raw = 1};
    Sprite ring = {.x = 163, .y = 130, .semi_trans = 1, .blend = 1, .raw = 1};
    Sprite tick = {.x = 134, .y = 130, .raw = 1};
    Sprite cross = {.x = 163, .y = 130, .raw = 1};
    int answer = 0;
    ring.x = 0xa3;
    Setup_Sprite_Quad(&quads[0], "CARD_ERR", &error);
    Setup_Sprite_Quad(&quads[1], "CARD_YN", &yes_no);
    Setup_Sprite_Quad(&quads[2], "CARDTICK", &tick);
    Setup_Sprite_Quad(&quads[3], "CARDCROS", &cross);
    Setup_Sprite_Quad(&quads[4], "SMALRING", &ring);
    text[4].text = "%R%JL%T/Quit Season?";
    Setup_Screen_Text(text);
    Play_Warning_FX();
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            answer = 1;
            ring.x = 0x86;
            Setup_Sprite_Quad(&quads[4], "SMALRING", &ring);
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            answer = 0;
            ring.x = 0xa3;
            Setup_Sprite_Quad(&quads[4], "SMALRING", &ring);
        } else if (pad.pressed & (PAD_CROSS | PAD_TRIANGLE)) {
            if (pad.pressed & PAD_CROSS)
                Play_Select_FX();
            else
                answer = 0;
            DrawSync(0);
            text[4].text = "";
            Setup_Screen_Text(text);
            return answer;
        }
        DrawSync(0);
        addPrim(&cdb->ot[2], &quads[0]);
        addPrim(&cdb->ot[1], &quads[4]);
        addPrim(&cdb->ot[1], &quads[1]);
        addPrim(&cdb->ot[1], &quads[2]);
        addPrim(&cdb->ot[1], &quads[3]);
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

int Ask_Quit_Season_Status(void)
{
    return ask_quit_season(season_status_text);
}

/* A RESULTS menu: buttons on two rows with a ring round the chosen one, moved with the pad. */
typedef struct {
    ScreenPoly *polys;
    ScreenText *text;
    const ScreenLine *lines;
    Sprite *ring, *rings;          /* the top row's ring, the bottom row's */
    MenuItem *items;
    const uint8_t (*moves)[4];     /* left, up, right, down from each item */
    const char *const *sprites, *const *pressed_sprites;
    int cd_track;
    void (*retitle)(void);         /* sets the title again after a choice (or NULL) */
    int (*blank)(int item);        /* nonzero: the choice starts with the screen off, not the slab turned */
    int (*ask_quit)(void);
} ResultsMenu;

static void results_menu_ring(const ResultsMenu *m, int item)
{
    Sprite *on = m->items[item].row == 0 ? m->ring : m->rings;
    Sprite *off = m->items[item].row == 0 ? m->rings : m->ring;
    on->x = m->items[item].x;
    on->y = m->items[item].y;
    off->x = OFF_SCREEN;
    off->y = OFF_SCREEN;
}

/* Returns 1 when a choice ends the menu (on to the next race or season), 0 when the season is given
 * up (Triangle, then yes). */
static int run_results_menu(const ResultsMenu *m)
{
    int item = 0;
    bool changed = false;
    Show_Camcorder_Loading_Screen();
    results_menu_ring(m, 0);
    m->items[0].caption();
    Setup_Screen_Polys(m->polys);
    Setup_Screen_Text(m->text);
    Setup_Screen_Lines(m->lines);
    Set_CD_Track(m->cd_track, 1);
    Rotate_Slab_On(NULL);
    Start_CD_Audio();
    restart_cd_audio = 0;
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        int move = -1;
        if (pad.pressed & PAD_LEFT)
            move = 0;
        else if (pad.pressed & PAD_RIGHT)
            move = 2;
        else if (pad.pressed & PAD_UP)
            move = 1;
        else if (pad.pressed & PAD_DOWN)
            move = 3;
        if (move >= 0) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = m->moves[item][move];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            m->polys[item].name = m->pressed_sprites[item];
            Setup_Screen_Polys(m->polys);
            Button_Pressed(7, NULL);
            m->polys[item].name = m->sprites[item];
            Setup_Screen_Polys(m->polys);
            Button_Pressed(2, NULL);
            if (m->blank != NULL && m->blank(item))
                SetDispMask(0);
            else
                Rotate_Slab_Off(NULL);
            if (m->items[item].enter()) {
                Set_Pad_Change_Callback(NULL);
                return 1;
            }
            if (m->retitle != NULL)
                m->retitle();
            m->items[item].caption();
            Setup_Screen_Polys(m->polys);
            Setup_Screen_Text(m->text);
            Setup_Screen_Lines(m->lines);
            if (restart_cd_audio != 0)
                Set_CD_Track(m->cd_track, 1);
            Rotate_Slab_On(NULL);
            if (restart_cd_audio != 0)
                Start_CD_Audio();
            restart_cd_audio = 0;
            changed = true;
            continue;
        } else if (pad.pressed & PAD_TRIANGLE) {
            m->ring->x = m->ring->y = OFF_SCREEN;
            m->rings->x = m->rings->y = OFF_SCREEN;
            if (m->ask_quit()) {
                Rotate_Slab_Off(NULL);
                current_level = LEVEL_FRONT_END;
                Loading_Screen_From_Slab(NULL);
                Set_Pad_Change_Callback(NULL);
                return 0;
            }
            changed = true;
        }
        if (changed) {
            changed = false;
            results_menu_ring(m, item);
            Setup_Screen_Polys(m->polys);
            m->items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

/* Between championship races: results, the league, the replay, saving, statistics, or on to the next
 * race. */
int Display_Season_Status(void)
{
    const ResultsMenu menu = {
        season_status_polys, season_status_text, season_status_lines, &season_status_sprites[SS_RING],
        &season_status_sprites[SS_RINGS], season_status_items, season_status_moves[race_type == RACE_MULTIPLAYER],
        season_status_item_sprites, season_status_pressed_sprites, 13, NULL, NULL, Ask_Quit_Season_Status,
    };
    Set_Season_Status_Mode();
    return run_results_menu(&menu);
}

/* ---- the end of a season ---- */

enum {
    SE_RESULTS, SE_CHAMP, SE_REPLAY, SE_CARD, SE_STATS, SE_RING, SE_RINGS, SE_PAD2, SE_QUIT, SE_PAD1, SE_TSELECT,
    SE_IRESULTS, SE_GO, SE_SCROSS_STATS, SE_SCROSS_CARD, SE_4WAY, SE_TMOVE, SEASON_END_SPRITES
};

static Sprite season_end_sprites[SEASON_END_SPRITES] = { /* 0x801C9D7C */
    [SE_RESULTS] = BUTTON_SPRITE(32, 96),
    [SE_CHAMP] = BUTTON_SPRITE(128, 96),
    [SE_REPLAY] = BUTTON_SPRITE(224, 96),
    [SE_CARD] = BUTTON_SPRITE(88, 154),
    [SE_STATS] = BUTTON_SPRITE(184, 154),
    [SE_RING] = TRANS_SPRITE(31, 94),
    [SE_RINGS] = TRANS_SPRITE(OFF_SCREEN, OFF_SCREEN),
    [SE_PAD2] = BUTTON_SPRITE(260, 24),
    [SE_QUIT] = BUTTON_SPRITE(278, 29),
    [SE_PAD1] = BUTTON_SPRITE(260, 41),
    [SE_TSELECT] = BUTTON_SPRITE(278, 46),
    [SE_IRESULTS] = BUTTON_SPRITE(16, 24),
    [SE_GO] = BUTTON_SPRITE(262, 192),
    [SE_SCROSS_STATS] = {184, 154, 128, 128, 128, 0, 0, 0, 1, 0},
    [SE_SCROSS_CARD] = {184, 154, 128, 128, 128, 0, 0, 0, 1, 0},
    [SE_4WAY] = BUTTON_SPRITE(256, 58),
    [SE_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly season_end_polys[] = { /* 0x801C9F38 */
    {2, "RESULTS", SCREEN_GREY, &season_end_sprites[SE_RESULTS]},
    {2, "CHAMP", SCREEN_GREY, &season_end_sprites[SE_CHAMP]},
    {2, "REPLAY", SCREEN_GREY, &season_end_sprites[SE_REPLAY]},
    {2, "CARD", SCREEN_GREY, &season_end_sprites[SE_CARD]},
    {2, "STATS", SCREEN_GREY, &season_end_sprites[SE_STATS]},
    {2, "GO", SCREEN_GREY, &season_end_sprites[SE_GO]},
    {1, "RING", SCREEN_HIGHLIGHT, &season_end_sprites[SE_RING]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &season_end_sprites[SE_RINGS]},
    {1, "SCROSS", SCREEN_GREY_TRANS, &season_end_sprites[SE_SCROSS_STATS]},
    {1, "SCROSS", SCREEN_GREY_TRANS, &season_end_sprites[SE_SCROSS_CARD]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &season_end_sprites[SE_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &season_end_sprites[SE_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &season_end_sprites[SE_PAD2]},
    {2, "QUIT", SCREEN_GREY_TRANS, &season_end_sprites[SE_QUIT]},
    {2, "4WAY", SCREEN_GREY_TRANS, &season_end_sprites[SE_4WAY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &season_end_sprites[SE_TMOVE]},
    {2, "IRESULTS", SCREEN_GREY, &season_end_sprites[SE_IRESULTS]},
    {0, NULL, 0, NULL},
};

static ScreenText season_end_text[] = { /* 0x801CA088 */
    {3, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, ""},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, ""},
    {3, 1, GREY_INK, 0, 95, SCREEN_Z, ""},
    {4, 4, GREY_INK, 3, 98, SCREEN_Z, ""},
    {1, 2, {0, 0, 0, 0}, -23, -13, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static ScreenLine season_end_lines[] = { /* 0x801C9F14: the title's underline fits the title */
    {3, 54, 52, 124, 0xff, 0xc8, 0xc8, 0},
    {4, 57, 55, 124, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static MenuItem season_end_items[6];

/* The multi-player league in place of the divisions; the statistics and the memory card crossed out. */
static void Set_Season_End_Mode(void) /* 0x801C6DA8 */
{
    if (race_type == RACE_MULTIPLAYER) {
        season_end_items[1].enter = View_MultiLeague;
        season_end_sprites[SE_SCROSS_STATS].x = 0xc6;
        season_end_sprites[SE_SCROSS_STATS].y = 0xa5;
        season_end_sprites[SE_SCROSS_CARD].x = 0x66;
        season_end_sprites[SE_SCROSS_CARD].y = 0xa5;
    } else {
        season_end_items[1].enter = View_League;
        season_end_sprites[SE_SCROSS_STATS].x = season_end_sprites[SE_SCROSS_STATS].y = OFF_SCREEN;
        season_end_sprites[SE_SCROSS_CARD].x = season_end_sprites[SE_SCROSS_CARD].y = OFF_SCREEN;
    }
}

static void set_season_end_title(const char *title, int16_t underline, int kind)
{
    season_end_text[0].text = title;
    season_end_text[1].text = title;
    season_end_lines[0].length = underline;
    season_end_lines[1].length = underline;
    season_end_kind = kind;
}

void Set_Season_End_Title(void)
{
    if (race_type == RACE_MULTIPLAYER) {
        set_season_end_title("%R%JL%T/Season Results", 200, 1);
        return;
    }
    switch (Check_League_Standing()) {
    case 1:
        set_season_end_title("%R%JL%T/Promoted", 0x78, 0);
        break;
    case 2:
        set_season_end_title("%R%JL%T/Season Results", 200, 0);
        break;
    case 3:
        set_season_end_title("%R%JL%T/Relegated", 0x7a, 0);
        break;
    case 4:
        set_season_end_title("%R%JL%T/Winner !", 0x67, 2);
        break;
    case 5:
        set_season_end_title("%R%JL%T/Relegated Out", 0xab, 1);
        break;
    }
}

static void set_season_end_caption(const char *caption)
{
    Set_Season_End_Mode();
    season_end_text[2].text = caption;
    season_end_text[3].text = caption;
    Setup_Screen_Text(season_end_text);
}

void Show_Season_View_Results_Caption(void)
{
    set_season_end_caption("%R%JC%T/View Results");
}

void Show_Season_View_League_Caption(void)
{
    set_season_end_caption("%R%JC%T/View League");
}

void Show_Season_View_Replay_Caption(void)
{
    set_season_end_caption("%R%JC%T/View Replay");
}

void Show_Season_File_Options_Caption(void)
{
    set_season_end_caption("%R%JC%T/Memory card Options");
}

void Show_Season_View_Stats_Caption(void)
{
    set_season_end_caption("%R%JC%T/View Statistics");
}

/* What GO does: the end sequence after winning the top division, back to the title when out (or in
 * a multi-player championship), otherwise the next season. */
void Show_Season_Go_Caption(void)
{
    const char *caption = "%R%JC%T/Next Season";
    if (race_type == RACE_MULTIPLAYER) {
        caption = "%R%JC%T/Return To Title Screen";
    } else {
        int standing = Check_League_Standing();
        if (standing == 4)
            caption = "%R%JC%T/End Sequence";
        else if (standing == 5)
            caption = "%R%JC%T/Return To Title Screen";
    }
    set_season_end_caption(caption);
}

int Select_Season_Go(void)
{
    return 1;
}

/* Saving at the season's end saves the next season: it is set up for the save, then put back. */
int Save_Game_At_Season_End(void)
{
    Keep_Season_Snapshot();
    Do_End_Of_Season_Stuff();
    Save_Game();
    Restore_Season_Snapshot();
    return 0;
}

int Ask_Quit_Season_End_Menu(void)
{
    return ask_quit_season(season_end_text);
}

static MenuItem season_end_items[6] = { /* 0x801CA100 */
    {0, 33, 94, Show_Season_View_Results_Caption, View_Race_Results, Null_Menu_Callback},
    {0, 129, 94, Show_Season_View_League_Caption, View_League, Null_Menu_Callback},
    {0, 225, 94, Show_Season_View_Replay_Caption, Select_View_Results_Replay, Null_Menu_Callback},
    {1, 89, 152, Show_Season_File_Options_Caption, Save_Game_At_Season_End, Null_Menu_Callback},
    {1, 185, 152, Show_Season_View_Stats_Caption, Run_Statistics_Menu, Null_Menu_Callback},
    {1, 262, 190, Show_Season_Go_Caption, Select_Season_Go, Null_Menu_Callback},
};

/* In a multi-player championship the bottom row but GO is crossed out (0x801CA178, 0x801CA190). */
static const uint8_t season_end_moves[2][6][4] = {
    {{0, 0, 1, 3}, {0, 1, 2, 4}, {1, 2, 2, 4}, {3, 0, 4, 5}, {3, 1, 4, 5}, {5, 4, 5, 5}},
    {{0, 0, 1, 5}, {0, 1, 2, 5}, {1, 2, 2, 5}, {0, 0, 0, 0}, {0, 0, 0, 0}, {5, 2, 5, 5}},
};

/* GO into the end sequence turns the screen off rather than the slab. */
static int season_end_blank(int item)
{
    return item == 5 && season_end_kind == 2;
}

/* The season's end. Returns 1 to go on (Do_End_Of_Season_Stuff decides where), 0 when given up. */
int End_Of_Season(void)
{
    const ResultsMenu menu = {
        season_end_polys, season_end_text, season_end_lines, &season_end_sprites[SE_RING],
        &season_end_sprites[SE_RINGS], season_end_items, season_end_moves[race_type == RACE_MULTIPLAYER],
        season_status_item_sprites, season_status_pressed_sprites, 14, Set_Season_End_Title, season_end_blank,
        Ask_Quit_Season_End_Menu,
    };
    Set_Season_End_Mode();
    Set_Season_End_Title();
    return run_results_menu(&menu);
}

/* ---- the multi-player race over: before the next player's turn ---- */

enum { RO_VIEWREP, RO_SAVREP, RO_RING, RO_RINGS, RO_PAD2, RO_QUIT, RO_PAD1, RO_TSELECT, RO_IMEMCARD, RO_GO, RO_4WAY, RO_TMOVE, RACE_OVER_SPRITES };

static Sprite race_over_sprites[RACE_OVER_SPRITES] = { /* 0x801CA810 */
    [RO_VIEWREP] = BUTTON_SPRITE(64, 96),
    [RO_SAVREP] = BUTTON_SPRITE(192, 96),
    [RO_RING] = TRANS_SPRITE(63, 94),
    [RO_RINGS] = TRANS_SPRITE(OFF_SCREEN, OFF_SCREEN),
    [RO_PAD2] = BUTTON_SPRITE(260, 24),
    [RO_QUIT] = BUTTON_SPRITE(278, 29),
    [RO_PAD1] = BUTTON_SPRITE(260, 41),
    [RO_TSELECT] = BUTTON_SPRITE(278, 46),
    [RO_IMEMCARD] = BUTTON_SPRITE(16, 24),
    [RO_GO] = BUTTON_SPRITE(262, 192),
    [RO_4WAY] = BUTTON_SPRITE(256, 58),
    [RO_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly race_over_polys[] = { /* 0x801CA954 */
    {4, "VIEWREP", SCREEN_GREY, &race_over_sprites[RO_VIEWREP]},
    {4, "SAVREP", SCREEN_GREY, &race_over_sprites[RO_SAVREP]},
    {4, "GO", SCREEN_GREY, &race_over_sprites[RO_GO]},
    {4, "ZZZZPAD1", SCREEN_GREY_TRANS, &race_over_sprites[RO_PAD1]},
    {4, "TSELECT", SCREEN_GREY_TRANS, &race_over_sprites[RO_TSELECT]},
    {4, "ZZZZPAD2", SCREEN_GREY_TRANS, &race_over_sprites[RO_PAD2]},
    {4, "QUIT", SCREEN_GREY_TRANS, &race_over_sprites[RO_QUIT]},
    {2, "4WAY", SCREEN_GREY_TRANS, &race_over_sprites[RO_4WAY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &race_over_sprites[RO_TMOVE]},
    {3, "RING", SCREEN_HIGHLIGHT, &race_over_sprites[RO_RING]},
    {3, "RINGS", SCREEN_HIGHLIGHT, &race_over_sprites[RO_RINGS]},
    {4, "IMEMCARD", SCREEN_GREY, &race_over_sprites[RO_IMEMCARD]},
    {0, NULL, 0, NULL},
};

static ScreenText race_over_text[] = { /* 0x801CAA3C */
    {3, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Race Over"},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Race Over"},
    {3, 1, GREY_INK, 0, 95, SCREEN_Z, ""},
    {4, 4, GREY_INK, 3, 98, SCREEN_Z, ""},
    {1, 2, {0, 0, 0, 0}, -23, -13, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine race_over_lines[] = { /* 0x801CA930 */
    {1, 54, 52, 122, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 122, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_race_over_caption(const char *caption)
{
    race_over_text[2].text = caption;
    race_over_text[3].text = caption;
    Setup_Screen_Text(race_over_text);
}

void Show_Race_Over_View_Replay_Caption(void)
{
    set_race_over_caption("%R%JC%T/View Replay");
}

void Show_Race_Over_Save_Replay_Caption(void)
{
    set_race_over_caption("%R%JC%T/Save Replay");
}

void Show_Race_Over_Next_Race_Caption(void)
{
    set_race_over_caption("%R%JC%T/Next Race");
}

int Select_Race_Over_Save_Replay(void)
{
    Save_Race_Replay();
    return 0;
}

int Select_Race_Over_Next_Race(void)
{
    return 1;
}

int Ask_Quit_Season_Race_Over(void)
{
    return ask_quit_season(race_over_text);
}

static MenuItem race_over_items[3] = { /* 0x801CAAB4 */
    {0, 66, 94, Show_Race_Over_View_Replay_Caption, Select_View_Results_Replay, Null_Menu_Callback},
    {0, 192, 94, Show_Race_Over_Save_Replay_Caption, Select_Race_Over_Save_Replay, Null_Menu_Callback},
    {1, 262, 190, Show_Race_Over_Next_Race_Caption, Select_Race_Over_Next_Race, Null_Menu_Callback},
};

static const uint8_t race_over_moves[3][4] = {{0, 0, 1, 2}, {0, 1, 1, 2}, {2, 1, 2, 2}}; /* 0x801CAAF0 */
static const char *const race_over_item_sprites[3] = {"VIEWREP", "SAVREP", "GO"};          /* 0x801CAA24 */
static const char *const race_over_pressed_sprites[3] = {"VIEWREPP", "SAVREPP", "GOP"};    /* 0x801CAA30 */

/* After one player's race in a multi-player championship: the replay, saving it, or the next turn. */
int Race_Over(void)
{
    const ResultsMenu menu = {
        race_over_polys, race_over_text, race_over_lines, &race_over_sprites[RO_RING], &race_over_sprites[RO_RINGS],
        race_over_items, race_over_moves, race_over_item_sprites, race_over_pressed_sprites, 13, NULL, NULL,
        Ask_Quit_Season_Race_Over,
    };
    return run_results_menu(&menu);
}
