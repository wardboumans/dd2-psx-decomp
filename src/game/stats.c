#include "game/stats.h"

#include "game/car.h"
#include "game/car_cluts.h"
#include "game/car_motion.h"
#include "game/frontend.h"
#include "game/frontend_init.h"
#include "game/league.h"
#include "game/loadsave.h"
#include "game/main_init.h"
#include "graphics/font.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libapi.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"
#include "sound/cd.h"
#include "sound/sound.h"
#include "system/controller.h"
#include "system/debug.h"
#include "system/file.h"
#include "system/mpe_heap.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---- FRONTEND.BIN: car select ---- */

#define STAT_ROWS 3
#define STAT_MARKS 5
#define SWATCH_CLUT_WIDTH 0x100

enum {
    CS_ICAR, CS_PAD2, CS_TCANCEL, CS_PAD1, CS_TACCEPT, CS_NOTCH,
    CS_SIDEONLY = CS_NOTCH + STAT_ROWS * STAT_MARKS, CS_TMOVE, CAR_SELECT_SPRITES
};

#define NOTCH_SPRITE(row, mark) [CS_NOTCH + (row) * STAT_MARKS + (mark)] = BUTTON_SPRITE(250 + (mark) * 10, 124 + (row) * 16)

static Sprite car_select_sprites[CAR_SELECT_SPRITES] = { /* 0x801D49BC */
    [CS_ICAR] = BUTTON_SPRITE(16, 24),
    [CS_PAD2] = BUTTON_SPRITE(260, 24),
    [CS_TCANCEL] = BUTTON_SPRITE(278, 29),
    [CS_PAD1] = BUTTON_SPRITE(260, 41),
    [CS_TACCEPT] = BUTTON_SPRITE(278, 46),
    NOTCH_SPRITE(0, 0), NOTCH_SPRITE(0, 1), NOTCH_SPRITE(0, 2), NOTCH_SPRITE(0, 3), NOTCH_SPRITE(0, 4),
    NOTCH_SPRITE(1, 0), NOTCH_SPRITE(1, 1), NOTCH_SPRITE(1, 2), NOTCH_SPRITE(1, 3), NOTCH_SPRITE(1, 4),
    NOTCH_SPRITE(2, 0), NOTCH_SPRITE(2, 1), NOTCH_SPRITE(2, 2), NOTCH_SPRITE(2, 3), NOTCH_SPRITE(2, 4),
    [CS_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [CS_TMOVE] = BUTTON_SPRITE(282, 67),
};

#define NOTCH_POLY(row, mark) {2, "SNOTCH", SCREEN_GREY, &car_select_sprites[CS_NOTCH + (row) * STAT_MARKS + (mark)]}

static ScreenPoly car_select_polys[] = { /* 0x801D4BF0 */
    {3, "ICAR", SCREEN_GREY, &car_select_sprites[CS_ICAR]},
    {3, "ZZZZPAD1", SCREEN_GREY_TRANS, &car_select_sprites[CS_PAD1]},
    {3, "TACCEPT", SCREEN_GREY_TRANS, &car_select_sprites[CS_TACCEPT]},
    {3, "ZZZZPAD2", SCREEN_GREY_TRANS, &car_select_sprites[CS_PAD2]},
    {3, "TCANCEL", SCREEN_GREY_TRANS, &car_select_sprites[CS_TCANCEL]},
    {3, "SIDEONLY", SCREEN_GREY_TRANS, &car_select_sprites[CS_SIDEONLY]},
    {3, "TMOVE", SCREEN_GREY_TRANS, &car_select_sprites[CS_TMOVE]},
    NOTCH_POLY(0, 0), NOTCH_POLY(0, 1), NOTCH_POLY(0, 2), NOTCH_POLY(0, 3), NOTCH_POLY(0, 4),
    NOTCH_POLY(1, 0), NOTCH_POLY(1, 1), NOTCH_POLY(1, 2), NOTCH_POLY(1, 3), NOTCH_POLY(1, 4),
    NOTCH_POLY(2, 0), NOTCH_POLY(2, 1), NOTCH_POLY(2, 2), NOTCH_POLY(2, 3), NOTCH_POLY(2, 4),
    {0, NULL, 0, NULL},
};

static ScreenText car_select_text[] = { /* 0x801D4D60 */
    {1, 0, TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Car Select"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Car Select"},
    {1, 0, GREY_INK, 0, -30, SCREEN_Z, ""},
    {2, 3, {0, 0, 0, 0}, 3, -27, SCREEN_Z, ""},
    {1, 1, GREY_INK, 0, -5, SCREEN_Z, "%R%JL%T/Acceleration"},
    {2, 4, GREY_INK, 3, -2, SCREEN_Z, "%R%JL%T/Acceleration"},
    {1, 1, GREY_INK, 0, 10, SCREEN_Z, "%R%JL%T/Top Speed"},
    {2, 4, GREY_INK, 3, 13, SCREEN_Z, "%R%JL%T/Top Speed"},
    {1, 1, GREY_INK, 0, 25, SCREEN_Z, "%R%JL%T/Grip"},
    {2, 4, GREY_INK, 3, 28, SCREEN_Z, "%R%JL%T/Grip"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine car_select_lines[] = { /* 0x801D4BCC */
    {1, 54, 52, 130, 0xc8, 0xff, 0xff, 0},
    {2, 57, 55, 130, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* Marks out of five for acceleration, top speed and grip, per car class. */
static const int32_t car_class_stats[3][STAT_ROWS] = {{1, 2, 5}, {3, 4, 2}, {5, 5, 1}}; /* 0x801D4E3C */

/* The car model turning on the slab (about its own axis, the third angle), lit from the camera. */
static int16_t car_model_angles[4] = {0x280, 0x780, 0x800, 0}; /* 0x801D8318 */
static const int32_t car_model_position[3] = {-72, 0, 240};   /* 0x801D4E60 */
static const MATRIX car_model_light = {{{0, 0, -0x1000}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}};           /* 0x801D4E70 */
static const MATRIX car_model_colour = {{{0x1000, 0, 0}, {0x1000, 0, 0}, {0x1000, 0, 0}}, {0, 0, 0}}; /* 0x801D4E90 */
static MATRIX car_model_matrix;       /* 0x801D87E0 */
static MATRIX car_model_light_matrix; /* 0x801D87C0 */

/* The car models' paint CLUTs: CAR1's row shows the selected class's colours (CAR3, CAR2 for the
 * others) and is put back on leaving. */
static struct {
    int16_t x, y;
} swatch_cluts[3];                                   /* 0x801D8800 */
static uint16_t swatch_clut_backup[SWATCH_CLUT_WIDTH]; /* 0x801D8818 */
static int32_t entry_race_car;                       /* 0x801D87BC */

void Init_Menu_Swatch_Cluts(void)
{
    static const char *const names[3] = {"CAR1", "CAR3", "CAR2"}; /* 0x801D8320 */
    for (int i = 0; i < 3; i++) {
        const SpriteInfo *s = Search_For_Sprite(names[i]);
        swatch_cluts[i].x = s->clut_x;
        swatch_cluts[i].y = s->clut_y;
        if (i == 0) {
            RECT row = {swatch_cluts[0].x, swatch_cluts[0].y, SWATCH_CLUT_WIDTH, 1};
            StoreImage(&row, (u_long *)swatch_clut_backup);
            DrawSync(0);
        }
    }
}

void Restore_Menu_Swatch_Clut(void)
{
    RECT row = {swatch_cluts[0].x, swatch_cluts[0].y, SWATCH_CLUT_WIDTH, 1};
    LoadImage(&row, (u_long *)swatch_clut_backup);
    DrawSync(0);
}

/* The stat notches for a class (the rest moved off screen), its name and its paint. */
void Show_Car_Stat_Markers(const int32_t *stats)
{
    for (int row = 0; row < STAT_ROWS; row++) {
        int mark = 0;
        for (; mark < stats[row]; mark++) {
            Sprite *notch = &car_select_sprites[CS_NOTCH + row * STAT_MARKS + mark];
            notch->x = (int16_t)(mark * 10 + 0xfa);
            notch->y = (int16_t)(0x7c + row * 0x10);
        }
        for (; mark < STAT_MARKS; mark++) {
            Sprite *notch = &car_select_sprites[CS_NOTCH + row * STAT_MARKS + mark];
            notch->x = OFF_SCREEN;
            notch->y = OFF_SCREEN;
        }
    }
    if (race_car == 1) {
        car_select_text[2].text = car_select_text[3].text = "%R%JL%T/Amateur";
    } else if (race_car == 0) {
        car_select_text[2].text = car_select_text[3].text = "%R%JL%T/Rookie";
    } else if (race_car == 2) {
        car_select_text[2].text = car_select_text[3].text = "%R%JL%T/Pro";
    }
    RECT row = {swatch_cluts[0].x, swatch_cluts[0].y, SWATCH_CLUT_WIDTH, 1};
    if (race_car == 0) {
        LoadImage(&row, (u_long *)swatch_clut_backup);
    } else {
        row.x = swatch_cluts[race_car].x;
        row.y = swatch_cluts[race_car].y;
        MoveImage(&row, swatch_cluts[0].x, swatch_cluts[0].y);
    }
}

int Select_Next_Car_Class_Stats(void)
{
    Toggle_Car(1);
    Show_Car_Stat_Markers(car_class_stats[race_car]);
    return 0;
}

int Select_Prev_Car_Class_Stats(void)
{
    Toggle_Car(0);
    Show_Car_Stat_Markers(car_class_stats[race_car]);
    return 0;
}

void Spin_Car_Select_Model(void)
{
    car_model_angles[2] -= 0x10;
    SetBackColor(0x60, 0x60, 0x60);
    Calc_Object_MatrixYXZ(&car_model_matrix, car_model_angles);
    MulMatrix0(&car_model_light, &car_model_matrix, &car_model_light_matrix);
    SetLightMatrix(&car_model_light_matrix);
    SetColorMatrix(&car_model_colour);
    Set_Object_View(&car_model_matrix, car_model_position);
    Update_Object(&carselect_object);
    SetBackColor(0, 0, 0);
}

/* Left and right step the car class, Cross keeps it, Triangle puts the old one back. Returns 1
 * when a class was picked. */
int Car_Select_Screen(void)
{
    bool changed = false;
    int result;
    entry_race_car = race_car;
    Init_Menu_Swatch_Cluts();
    Show_Car_Stat_Markers(car_class_stats[race_car]);
    Setup_Screen_Text(car_select_text);
    Setup_Screen_Polys(car_select_polys);
    Setup_Screen_Lines(car_select_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Select_Prev_Car_Class_Stats();
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            Select_Next_Car_Class_Stats();
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            Play_Select_FX();
            Setup_Screen_Polys(car_select_polys);
            result = 1;
            break;
        } else if (pad.pressed & PAD_TRIANGLE) {
            race_car = entry_race_car;
            result = 0;
            break;
        }
        if (changed) {
            changed = false;
            Setup_Screen_Text(car_select_text);
            Setup_Screen_Polys(car_select_polys);
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Spin_Car_Select_Model();
        Draw_All(1);
    }
    Rotate_Slab_Off(NULL);
    Restore_Menu_Swatch_Clut();
    return result;
}

/* ---- FRONTEND.BIN: sound volume ---- */


enum {
    VOL_RINGS = 1, VOL_MUSIC_NOTCH = 10, VOL_EFFECTS_NOTCH = 20, VOL_MUSIC_SHADOW = 30, VOL_EFFECTS_SHADOW = 40,
    VOL_LARROW = 50, VOL_ARROW = 51
};

static Sprite volume_sprites[58] = { /* 0x801D3FF4 */
    BUTTON_SPRITE(16, 24), /* 0 IVOLUME */
    TRANS_SPRITE(52, 88), /* 1 RINGS */
    BUTTON_SPRITE(260, 24), /* 2 ZZZZPAD2 */
    BUTTON_SPRITE(278, 29), /* 3 TCANCEL */
    BUTTON_SPRITE(260, 41), /* 4 ZZZZPAD1 */
    BUTTON_SPRITE(278, 46), /* 5 TACCEPT */
    BUTTON_SPRITE(52, 88), /* 6 MUSICVOL */
    BUTTON_SPRITE(56, 92), /* 7 not drawn */
    BUTTON_SPRITE(52, 147), /* 8 SFXVOL */
    BUTTON_SPRITE(56, 151), /* 9 not drawn */
    BUTTON_SPRITE(136, 102), BUTTON_SPRITE(148, 102), BUTTON_SPRITE(160, 102), BUTTON_SPRITE(172, 102),
    BUTTON_SPRITE(184, 102), BUTTON_SPRITE(196, 102), BUTTON_SPRITE(208, 102), BUTTON_SPRITE(220, 102),
    BUTTON_SPRITE(232, 102), BUTTON_SPRITE(244, 102), /* 10.. music NOTCH */
    BUTTON_SPRITE(136, 161), BUTTON_SPRITE(148, 161), BUTTON_SPRITE(160, 161), BUTTON_SPRITE(172, 161),
    BUTTON_SPRITE(184, 161), BUTTON_SPRITE(196, 161), BUTTON_SPRITE(208, 161), BUTTON_SPRITE(220, 161),
    BUTTON_SPRITE(232, 161), BUTTON_SPRITE(244, 161), /* 20.. effects NOTCH */
    BUTTON_SPRITE(140, 106), BUTTON_SPRITE(152, 106), BUTTON_SPRITE(164, 106), BUTTON_SPRITE(176, 106),
    BUTTON_SPRITE(188, 106), BUTTON_SPRITE(200, 106), BUTTON_SPRITE(212, 106), BUTTON_SPRITE(224, 106),
    BUTTON_SPRITE(236, 106), BUTTON_SPRITE(248, 106), /* 30.. music NOTCH shadow */
    BUTTON_SPRITE(140, 165), BUTTON_SPRITE(152, 165), BUTTON_SPRITE(164, 165), BUTTON_SPRITE(176, 165),
    BUTTON_SPRITE(188, 165), BUTTON_SPRITE(200, 165), BUTTON_SPRITE(212, 165), BUTTON_SPRITE(224, 165),
    BUTTON_SPRITE(236, 165), BUTTON_SPRITE(248, 165), /* 40.. effects NOTCH shadow */
    BUTTON_SPRITE(38, 106), /* 50 LARROW */
    BUTTON_SPRITE(258, 106), /* 51 ARROW */
    BUTTON_SPRITE(108, 99), /* 52 SPEAKER */
    BUTTON_SPRITE(108, 158), /* 53 SPEAKER */
    BUTTON_SPRITE(112, 103), /* 54 SPEAKER */
    BUTTON_SPRITE(112, 162), /* 55 SPEAKER */
    BUTTON_SPRITE(256, 58), /* 56 4WAY */
    BUTTON_SPRITE(282, 67), /* 57 TMOVE */
};

#define VOL_NOTCH(i, mode) {2, "NOTCH", mode, &volume_sprites[i]}
#define VOL_NOTCH_ROW(first, mode)                                                                          \
    VOL_NOTCH(first, mode), VOL_NOTCH(first + 1, mode), VOL_NOTCH(first + 2, mode), VOL_NOTCH(first + 3, mode), \
        VOL_NOTCH(first + 4, mode), VOL_NOTCH(first + 5, mode), VOL_NOTCH(first + 6, mode),                     \
        VOL_NOTCH(first + 7, mode), VOL_NOTCH(first + 8, mode), VOL_NOTCH(first + 9, mode)

static ScreenPoly volume_polys[] = { /* 0x801D4588 */
    {2, "IVOLUME", SCREEN_GREY, &volume_sprites[0]},
    {2, "MUSICVOL", SCREEN_GREY, &volume_sprites[6]},
    {2, "SFXVOL", SCREEN_GREY, &volume_sprites[8]},
    VOL_NOTCH_ROW(VOL_MUSIC_NOTCH, SCREEN_GREY),
    VOL_NOTCH_ROW(VOL_MUSIC_SHADOW, SCREEN_SPRITE_TRANS),
    VOL_NOTCH_ROW(VOL_EFFECTS_NOTCH, SCREEN_GREY),
    VOL_NOTCH_ROW(VOL_EFFECTS_SHADOW, SCREEN_SPRITE_TRANS),
    {2, "SPEAKER", SCREEN_GREY, &volume_sprites[52]},
    {2, "SPEAKER", SCREEN_GREY, &volume_sprites[53]},
    {2, "SPEAKER", SCREEN_SPRITE_TRANS, &volume_sprites[54]},
    {2, "SPEAKER", SCREEN_SPRITE_TRANS, &volume_sprites[55]},
    {2, "LARROW", SCREEN_SPRITE, &volume_sprites[VOL_LARROW]},
    {2, "ARROW", SCREEN_SPRITE, &volume_sprites[VOL_ARROW]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &volume_sprites[2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &volume_sprites[3]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &volume_sprites[4]},
    {2, "TACCEPT", SCREEN_GREY_TRANS, &volume_sprites[5]},
    {2, "4WAY", SCREEN_GREY_TRANS, &volume_sprites[56]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &volume_sprites[57]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &volume_sprites[VOL_RINGS]},
    {0, NULL, 0, NULL},
};

#define CONFIG_TITLE_INK {0x80, 0x64, 0x64, 0}

static ScreenText volume_text[] = { /* 0x801D4928 */
    {1, 0, CONFIG_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Audio Volume"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Audio Volume"},
    {1, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine volume_lines[] = { /* 0x801D4564 */
    {1, 54, 52, 168, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 168, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* The volumes being edited (applied as they change; kept by Cross). */
static int32_t edit_music_volume; /* 0x801D87B4 */
static int32_t edit_sound_volume; /* 0x801D87B8 */

static void set_volume_caption(const char *caption)
{
    volume_text[2].text = caption;
    volume_text[3].text = caption;
    Setup_Screen_Text(volume_text);
}

void Show_Music_Volume_Caption(void)
{
    set_volume_caption("%R%JC%T/Music");
}

void Show_Sound_Effects_Caption(void)
{
    set_volume_caption("%R%JC%T/Sound Effects");
}

int Select_Music_Volume(void)
{
    return 1;
}

int Select_Sound_Effects_Volume(void)
{
    return 1;
}

static void set_volume_bar(int notch, int shadow, int32_t volume)
{
    int marks = volume / VOLUME_STEP;
    int i = 0;
    for (; i < marks; i++) {
        volume_sprites[notch + i].x = (int16_t)(i * 12 + 0x88);
        volume_sprites[shadow + i].x = (int16_t)(i * 12 + 0x8c);
    }
    for (; i < VOLUME_NOTCHES; i++) {
        volume_sprites[notch + i].x = OFF_SCREEN;
        volume_sprites[shadow + i].x = OFF_SCREEN;
    }
}

/* The edited volumes take effect at once and show as notches. */
void Update_Volume_Bars(void)
{
    Set_CD_Volume((short)edit_music_volume);
    set_master_volume(edit_sound_volume);
    set_volume_bar(VOL_MUSIC_NOTCH, VOL_MUSIC_SHADOW, edit_music_volume);
    set_volume_bar(VOL_EFFECTS_NOTCH, VOL_EFFECTS_SHADOW, edit_sound_volume);
}

static const SubMenuItem volume_items[2] = { /* 0x801D4990 */
    {53, 86, Show_Music_Volume_Caption, Select_Music_Volume, Null_Menu_Callback, 0},
    {53, 145, Show_Sound_Effects_Caption, Select_Sound_Effects_Volume, Null_Menu_Callback, 0x1000000},
};

static const uint8_t volume_moves[2][4] = {{0, 0, 0, 1}, {1, 0, 1, 1}}; /* 0x801D49B4 */

/* Up and down pick music or effects; holding left or right turns it down or up a step a frame.
 * Cross keeps the new volumes, Triangle the old ones. */
int Adjust_Sound_Volume(void)
{
    Sprite *ring = &volume_sprites[VOL_RINGS];
    int item = 0;
    bool changed = false;
    edit_music_volume = music_volume;
    edit_sound_volume = sound_volume;
    Update_Volume_Bars();
    ring->x = volume_items[0].x;
    ring->y = volume_items[0].y;
    volume_items[0].caption();
    Setup_Screen_Polys(volume_polys);
    Setup_Screen_Text(volume_text);
    Setup_Screen_Lines(volume_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.held & PAD_LEFT) {
            if (item == 0) {
                if (edit_music_volume > 0)
                    edit_music_volume -= VOLUME_STEP;
            } else if (item == 1 && edit_sound_volume > 0) {
                edit_sound_volume -= VOLUME_STEP;
            }
            Update_Volume_Bars();
            changed = true;
        } else if (pad.held & PAD_RIGHT) {
            if (item == 0) {
                if (edit_music_volume < VOLUME_MAX)
                    edit_music_volume += VOLUME_STEP;
            } else if (item == 1 && edit_sound_volume < VOLUME_MAX) {
                edit_sound_volume += VOLUME_STEP;
            }
            Update_Volume_Bars();
            changed = true;
        } else if (pad.pressed & PAD_UP) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = volume_moves[item][1];
            changed = true;
        } else if (pad.pressed & PAD_DOWN) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = volume_moves[item][3];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            Play_Select_FX();
            sound_volume = edit_sound_volume;
            music_volume = edit_music_volume;
            break;
        } else if (pad.pressed & PAD_TRIANGLE) {
            break;
        }
        if (changed) {
            changed = false;
            ring->x = volume_items[item].x;
            ring->y = volume_items[item].y;
            volume_sprites[VOL_LARROW].y = (int16_t)(item * 0x3b + 0x6a);
            volume_sprites[VOL_ARROW].y = (int16_t)(item * 0x3b + 0x6a);
            Setup_Screen_Polys(volume_polys);
            volume_items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
    set_master_volume(sound_volume);
    Set_CD_Volume((short)music_volume);
    Rotate_Slab_Off(NULL);
    return 0;
}

/* ---- FRONTEND.BIN: centre screen ---- */

static Sprite centre_sprites[10] = { /* 0x801D4EB0 */
    BUTTON_SPRITE(16, 24),   /* 0 ICENTRE */
    BUTTON_SPRITE(141, 120), /* 1 LEFT */
    BUTTON_SPRITE(153, 108), /* 2 UP */
    BUTTON_SPRITE(164, 120), /* 3 RIGHT */
    BUTTON_SPRITE(153, 133), /* 4 DOWN */
    BUTTON_SPRITE(260, 24),  /* 5 ZZZZPAD2 */
    BUTTON_SPRITE(278, 29),  /* 6 TCANCEL */
    BUTTON_SPRITE(260, 41),  /* 7 ZZZZPAD1 */
    BUTTON_SPRITE(278, 46),  /* 8 TACCEPT */
    BUTTON_SPRITE(153, 149), /* 9 TMOVE */
};

static ScreenPoly centre_polys[] = { /* 0x801D4FC4 */
    {2, "ICENTRE", SCREEN_GREY, &centre_sprites[0]},
    {2, "UP", SCREEN_GREY, &centre_sprites[2]},
    {2, "DOWN", SCREEN_GREY, &centre_sprites[4]},
    {2, "LEFT", SCREEN_GREY, &centre_sprites[1]},
    {2, "RIGHT", SCREEN_GREY, &centre_sprites[3]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &centre_sprites[5]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &centre_sprites[6]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &centre_sprites[7]},
    {2, "TACCEPT", SCREEN_GREY_TRANS, &centre_sprites[8]},
    {2, "TMOVE", SCREEN_SPRITE_TRANS, &centre_sprites[9]},
    {0, NULL, 0, NULL},
};

static ScreenText centre_text[] = { /* 0x801D5074 */
    {1, 0, CONFIG_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Centre Screen"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Centre Screen"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine centre_lines[] = { /* 0x801D4FA0 */
    {1, 54, 52, 181, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 181, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* How far the picture may be moved on the TV, for NTSC and PAL (pal_mode). */
static const struct {
    int16_t min_x, min_y, max_x, max_y;
} screen_limits[2] = {{-16, -11, 16, 21}, {-12, 5, 19, 37}}; /* 0x801D50B0 */

/* Holding the arrows moves the picture on the TV (the display's screen position); Cross keeps
 * it for both PAL and NTSC, Triangle puts the current mode's back. The port does not move the
 * picture: its window shows the display area as is. */
int Centre_Screen_Page(void)
{
    RECT *screen0 = &db[0].disp.screen, *screen1 = &db[1].disp.screen;
    Setup_Screen_Polys(centre_polys);
    Setup_Screen_Text(centre_text);
    Setup_Screen_Lines(centre_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.held & PAD_LEFT) {
            if (screen_limits[pal_mode].min_x < screen0->x) {
                screen0->x--;
                screen1->x--;
            }
        } else if (pad.held & PAD_RIGHT) {
            if (screen0->x < screen_limits[pal_mode].max_x) {
                screen0->x++;
                screen1->x++;
            }
        } else if (pad.held & PAD_UP) {
            if (screen_limits[pal_mode].min_y < screen0->y) {
                screen0->y--;
                screen1->y--;
            }
        } else if (pad.held & PAD_DOWN) {
            if (screen0->y < screen_limits[pal_mode].max_y) {
                screen0->y++;
                screen1->y++;
            }
        } else if (pad.pressed & PAD_CROSS) {
            Play_Select_FX();
            video_modes[0].screen_x = video_modes[1].screen_x = screen0->x;
            video_modes[0].screen_y = video_modes[1].screen_y = screen0->y;
            break;
        } else if (pad.pressed & PAD_TRIANGLE) {
            const VideoMode *vm = pal_mode == 0 ? &video_modes[1] : &video_modes[0];
            screen1->x = vm->screen_x;
            screen1->y = vm->screen_y;
            screen0->x = screen1->x;
            screen0->y = screen1->y;
            break;
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
    Rotate_Slab_Off(NULL);
    return 0;
}

/* ---- FRONTEND.BIN: configuration ---- */

enum {
    CF_PADCONFG, CF_SOUNDVOL, CF_CNTRSCRN, CF_QWIKSAVE, CF_RING, CF_RINGS, CF_PAD2, CF_TCANCEL, CF_PAD1, CF_TSELECT,
    CF_ICONFIG, CF_4WAY, CF_TMOVE, CONFIG_SPRITES
};

static Sprite config_sprites[CONFIG_SPRITES] = { /* 0x801D50C0 */
    [CF_PADCONFG] = BUTTON_SPRITE(32, 96),
    [CF_SOUNDVOL] = BUTTON_SPRITE(128, 96),
    [CF_CNTRSCRN] = BUTTON_SPRITE(224, 96),
    [CF_QWIKSAVE] = BUTTON_SPRITE(184, 154),
    [CF_RING] = TRANS_SPRITE(31, 94),
    [CF_RINGS] = TRANS_SPRITE(OFF_SCREEN, OFF_SCREEN),
    [CF_PAD2] = BUTTON_SPRITE(260, 24),
    [CF_TCANCEL] = BUTTON_SPRITE(278, 29),
    [CF_PAD1] = BUTTON_SPRITE(260, 41),
    [CF_TSELECT] = BUTTON_SPRITE(278, 46),
    [CF_ICONFIG] = BUTTON_SPRITE(16, 24),
    [CF_4WAY] = BUTTON_SPRITE(256, 58),
    [CF_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly config_polys[] = { /* 0x801D521C */
    {2, "PADCONFG", SCREEN_GREY, &config_sprites[CF_PADCONFG]},
    {2, "SOUNDVOL", SCREEN_GREY, &config_sprites[CF_SOUNDVOL]},
    {2, "CNTRSCRN", SCREEN_GREY, &config_sprites[CF_CNTRSCRN]},
    {2, "QWIKSAVE", SCREEN_GREY, &config_sprites[CF_QWIKSAVE]},
    {2, "ICONFIG", SCREEN_GREY, &config_sprites[CF_ICONFIG]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &config_sprites[CF_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &config_sprites[CF_TCANCEL]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &config_sprites[CF_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &config_sprites[CF_TSELECT]},
    {2, "4WAY", SCREEN_GREY_TRANS, &config_sprites[CF_4WAY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &config_sprites[CF_TMOVE]},
    {1, "RING", SCREEN_HIGHLIGHT, &config_sprites[CF_RING]},
    {1, "RINGS", SCREEN_HIGHLIGHT, &config_sprites[CF_RINGS]},
    {0, NULL, 0, NULL},
};

static ScreenText config_text[] = { /* 0x801D531C */
    {1, 0, CONFIG_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Configuration"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Configuration"},
    {1, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine config_lines[] = { /* 0x801D51F8 */
    {1, 54, 52, 168, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 168, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_config_caption(const char *caption)
{
    config_text[2].text = caption;
    config_text[3].text = caption;
    Setup_Screen_Text(config_text);
}

void Show_Control_Method_Caption(void)
{
    set_config_caption("%R%JC%T/Select Controller");
}

void Show_Sound_Volume_Caption(void)
{
    set_config_caption("%R%JC%T/Sound Volume");
}

void Show_Centre_Screen_Caption(void)
{
    set_config_caption("%R%JC%T/Centre Screen");
}

void Show_Save_Config_Caption(void)
{
    set_config_caption("%R%JC%T/Save Configuration");
}

int Select_Save_Configuration(void)
{
    Save_Configuration();
    return 0;
}

static const MenuItem config_items[4] = { /* 0x801D5380 */
    {0, 34, 94, Show_Control_Method_Caption, Select_Control_Method, Null_Menu_Callback},
    {0, 128, 94, Show_Sound_Volume_Caption, Adjust_Sound_Volume, Null_Menu_Callback},
    {0, 224, 94, Show_Centre_Screen_Caption, Centre_Screen_Page, Null_Menu_Callback},
    {1, 184, 152, Show_Save_Config_Caption, Select_Save_Configuration, Null_Menu_Callback},
};

static const uint8_t config_moves[4][4] = {{0, 0, 1, 3}, {0, 1, 2, 3}, {1, 2, 2, 3}, {3, 1, 3, 3}}; /* 0x801D53D0 */
static const char *const config_item_sprites[4] = {"PADCONFG", "SOUNDVOL", "CNTRSCRN", "QWIKSAVE"}; /* 0x801D52FC */
static const char *const config_pressed_sprites[4] = {"PADCNFGP", "SOUNDVLP", "CNTRSCNP", "QWIKSAVP"}; /* 0x801D530C */

static void place_config_ring(int item)
{
    Sprite *ring = &config_sprites[CF_RING], *rings = &config_sprites[CF_RINGS];
    if (config_items[item].row == 0) {
        ring->x = config_items[item].x;
        ring->y = config_items[item].y;
        rings->x = rings->y = OFF_SCREEN;
    } else if (config_items[item].row == 1) {
        ring->x = ring->y = OFF_SCREEN;
        rings->x = config_items[item].x;
        rings->y = config_items[item].y;
    }
}

/* Controller, sound volume, centre screen (top row) and save configuration (below). */
int Config_Menu(void)
{
    int item = 0;
    bool changed = false;
    place_config_ring(0);
    config_items[0].caption();
    Setup_Screen_Polys(config_polys);
    Setup_Screen_Text(config_text);
    Setup_Screen_Lines(config_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = config_moves[item][0];
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = config_moves[item][2];
            changed = true;
        } else if (pad.pressed & PAD_UP) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = config_moves[item][1];
            changed = true;
        } else if (pad.pressed & PAD_DOWN) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = config_moves[item][3];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            config_polys[item].name = config_pressed_sprites[item];
            Setup_Screen_Polys(config_polys);
            Button_Pressed(7, NULL);
            config_polys[item].name = config_item_sprites[item];
            Setup_Screen_Polys(config_polys);
            Button_Pressed(2, NULL);
            Rotate_Slab_Off(NULL);
            if (config_items[item].enter())
                return 1;
            config_items[item].caption();
            Setup_Screen_Polys(config_polys);
            Setup_Screen_Text(config_text);
            Setup_Screen_Lines(config_lines);
            Rotate_Slab_On(NULL);
            continue;
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(NULL);
            return 0;
        }
        if (changed) {
            changed = false;
            place_config_ring(item);
            Setup_Screen_Polys(config_polys);
            config_items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}

/* ---- FRONTEND.BIN: credits ---- */

#define CREDITS_PAGE_FRAMES 300

static Sprite credits_sprites[5] = { /* 0x801D53E0 */
    BUTTON_SPRITE(260, 24), /* ZZZZPAD2 */
    BUTTON_SPRITE(278, 29), /* TCANCEL */
    BUTTON_SPRITE(16, 24),  /* IVIEWSTA */
    BUTTON_SPRITE(256, 41), /* UPONLY */
    BUTTON_SPRITE(282, 50), /* TMOVE */
};

static ScreenPoly credits_polys[] = { /* 0x801D547C */
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &credits_sprites[0]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &credits_sprites[1]},
    {2, "UPONLY", SCREEN_GREY_TRANS, &credits_sprites[3]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &credits_sprites[4]},
    {2, "IVIEWSTA", SCREEN_GREY, &credits_sprites[2]},
    {0, NULL, 0, NULL},
};

static ScreenText credits_text[] = { /* 0x801D54DC */
    {1, 0, CONFIG_TITLE_INK, -104, -96, SCREEN_Z, "%R%JL%T/Credits"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Credits"},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine credits_lines[] = { /* 0x801D5458 */
    {1, 54, 52, 91, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 91, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* One page of credits: headings with a shadow, names in white. Returns 0 past the last page. */
int Setup_Credits_Page(int page)
{
    Setup_Screen_Text(credits_text);
    switch (page) {
    case 0:
        Print_Shadowed_Text(0, -50, "%JC%T/Game Concept");
        Print_White_Text(0, -32, "%JC%T/Martin Edmondson");
        Print_Shadowed_Text(0, -10, "%JC%T/Head Programmer");
        Print_White_Text(0, 8, "%JC%T/Michael Troughton");
        Print_Shadowed_Text(0, 30, "%JC%T/Programmers");
        Print_White_Text(0, 48, "%JC%T/Jim Parr");
        Print_White_Text(0, 58, "%JC%T/Russell Lazzari");
        Print_White_Text(0, 68, "%JC%T/Tony M Roberts");
        Print_White_Text(0, 78, "%JC%T/Will Musson");
        break;
    case 1:
        Print_Shadowed_Text(0, -55, "%JC%T/Artists");
        Print_White_Text(0, -37, "%JC%T/David Taylor");
        Print_White_Text(0, -27, "%JC%T/Phil Baxter");
        Print_White_Text(0, -17, "%JC%T/Aidan Wilson");
        Print_White_Text(0, -7, "%JC%T/Richard Beston");
        Print_Shadowed_Text(0, 13, "%JC%T/Character Artist");
        Print_White_Text(0, 31, "%JC%T/Jonathan Steele");
        Print_Shadowed_Text(0, 53, "%JC%T/3D Artists");
        Print_White_Text(0, 71, "%JC%T/Shaun Stephenson");
        Print_White_Text(0, 81, "%JC%T/Chris Williams");
        Print_White_Text(0, 91, "%JC%T/Matt Gibson");
        break;
    case 2:
        Print_Shadowed_Text(0, -50, "%JC%T/Music");
        Print_White_Text(0, -32, "%JC%T/JUG");
        Print_White_Text(0, -22, "%JC%T/Richard Beston");
        Print_White_Text(0, -12, "%JC%T/Dean Liddle");
        Print_White_Text(0, -2, "%JC%T/Andrew Lewis");
        Print_White_Text(0, 20, "%JC%T/TUSCAN");
        Print_White_Text(0, 30, "%JC%T/Shaun Stephenson");
        Print_White_Text(0, 40, "%JC%T/Richard Mumford");
        Print_White_Text(0, 50, "%JC%T/Mark Fox");
        Print_White_Text(0, 60, "%JC%T/Mark McGowen");
        Print_White_Text(0, 70, "%JC%T/Alistair Dunn");
        break;
    case 3:
        Print_Shadowed_Text(0, -50, "%JC%T/Track Design");
        Print_White_Text(0, -32, "%JC%T/Phil Baxter");
        Print_Shadowed_Text(0, -10, "%JC%T/Sound Effects");
        Print_White_Text(0, 8, "%JC%T/Mike Clark");
        Print_Shadowed_Text(0, 30, "%JC%T/Commentary");
        Print_White_Text(0, 48, "%JC%T/Paul Page");
        break;
    case 4:
        Print_Shadowed_Text(0, -50, "%JC%T/Psygnosis Producer");
        Print_White_Text(0, -32, "%JC%T/Tony Parkes");
        Print_Shadowed_Text(0, -10, "%JC%T/Associate Producer");
        Print_White_Text(0, 8, "%JC%T/Paul Evason");
        Print_Shadowed_Text(0, 30, "%JC%T/Product Manager");
        Print_White_Text(0, 48, "%JC%T/Joanne Galvin");
        Print_Shadowed_Text(0, 70, "%JC%T/PR Manager");
        Print_White_Text(0, 88, "%JC%T/Glen O'Connel");
        break;
    default:
        return 0;
    }
    return 1;
}

/* The credit pages turn every five seconds, or with up and down; after the last, or on
 * Triangle, back to the menu. */
int Credits_Screen(void)
{
    int timer = CREDITS_PAGE_FRAMES;
    int page = 0;
    Setup_Screen_Polys(credits_polys);
    Setup_Screen_Text(credits_text);
    Setup_Screen_Lines(credits_lines);
    Rotate_Slab_On(NULL);
    Setup_Credits_Page(0);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_TRIANGLE)
            break;
        if (pad.pressed & PAD_DOWN) {
            Play_Click_FX();
            timer = CREDITS_PAGE_FRAMES;
            if (Setup_Credits_Page(page + 1))
                page++;
            else
                Setup_Credits_Page(page);
        } else if (pad.pressed & PAD_UP) {
            Play_Click_FX();
            timer = CREDITS_PAGE_FRAMES;
            if (Setup_Credits_Page(page - 1))
                page--;
            else
                Setup_Credits_Page(page);
        }
        if (timer == 0) {
            page++;
            timer = CREDITS_PAGE_FRAMES;
            if (!Setup_Credits_Page(page))
                break;
        }
        timer--;
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
    Setup_Screen_Text(credits_text);
    Rotate_Slab_Off(NULL);
    return 0;
}

/* ---- GENERAL.BIN: the team's faces ("CREDITZ!") ---- */

#define FACE_CREDITS 16
#define FACE_LEVEL 12           /* LEVC holds the faces */
#define FACE_SLIDE_END 500      /* two pixels a frame: a new face every 250 frames */

static const char *const face_sprites[FACE_CREDITS] = { /* 0x801ED940 */
    "AIDAN", "BEZ", "CHRIS", "DAVE", "JIM", "JOHN", "MARTIN", "MATT",
    "MIKE", "PAUL", "PHIL", "RUSS", "SHAUN", "TONY", "TONYR", "WILL",
};
static const char *const face_name_sprites[FACE_CREDITS] = { /* 0x801ED980 */
    "AIDCRED", "BEZCRED", "KRISCRED", "DAVCRED", "JIMCRED", "JONCRED", "MRTNCRED", "MATCRED",
    "MIKCRED", "PAULCRED", "FILCRED", "RUSCRED", "SHAUCRED", "TNYPCRED", "TNYRCRED", "WILCRED",
};

static Sprite face_sprite = {320, 120, 0, 0, 0, 0, 0, 0, 1};   /* 0x801ED9C0 */
static Sprite backd1_sprite = {0, 0, 0, 0, 0, 0, 0, 0, 1};      /* 0x801ED9D8 */
static Sprite backd2_sprite = {256, 0, 0, 0, 0, 0, 0, 0, 1};    /* 0x801ED9F0 */
static Sprite face_name_sprite = {-140, 90, 0, 0, 0, 0, 0, 0, 1}; /* 0x801EDA08 */

/* Per display buffer. */
static POLY_FT4 back_polys[2][2];  /* 0x801F2DBC: the two background halves */
static POLY_FT4 face_polys[2];     /* 0x801F2EB8 */
static POLY_FT4 face_name_polys[2]; /* 0x801F2F78 */

/* Loads the LEVC level (the faces and their backdrop) in place of the front end's. */
void Init_Face_Credit_Screen(void)
{
    FileRecord sprites = {0};
    InitHeap((u_long *)heap_base, HEAP_SIZE);
    Allocate_OT(0x400);
    current_level = FACE_LEVEL;
    Set_Load_Textures();
    Queue_Level_Data();
    Add_Buffer_Load("LEVC\\LEVEL.SPR", &sprites, FILE_PLAIN);
    Buffer_Load(NULL, NULL);
    Relocate_Level_Data();
    Init_Graphics();
    Load_Sprite_Info(sprites.data);
    for (int i = 0; i < 2; i++) {
        Setup_Sprite_Quad(&face_polys[i], "AIDAN", &face_sprite);
        Setup_Sprite_Quad(&face_name_polys[i], "AIDCRED", &face_name_sprite);
    }
    Setup_Sprite_Quad(&back_polys[0][0], "BACKD1", &backd1_sprite);
    Setup_Sprite_Quad(&back_polys[1][0], "BACKD1", &backd1_sprite);
    Setup_Sprite_Quad(&back_polys[0][1], "BACKD2", &backd2_sprite);
    Setup_Sprite_Quad(&back_polys[1][1], "BACKD2", &backd2_sprite);
    /* One pixel wider, to close the seam between the halves. */
    for (int b = 0; b < 2; b++) {
        for (int h = 0; h < 2; h++) {
            back_polys[b][h].x1++;
            back_polys[b][h].x3++;
        }
    }
}

/* Moves a quad to x (its width kept). */
static void place_quad_x(POLY_FT4 *q, int x)
{
    int16_t width = (int16_t)(q->x1 - q->x0);
    q->x0 = q->x2 = (short)x;
    q->x1 = q->x3 = (short)(width + x);
}

/* The team, one at a time: the face slides in from the right and the name from the left. Ends
 * on Cross or when the pad is not a digital one; the front end is loaded again. */
void Secret(void)
{
    int person = 0;
    int slide = 0;
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (slide == FACE_SLIDE_END) {
            if (++person >= FACE_CREDITS)
                person = 0;
            Setup_Sprite_Quad(&face_polys[0], face_sprites[person], &face_sprite);
            Setup_Sprite_Quad(&face_polys[1], face_sprites[person], &face_sprite);
            Setup_Sprite_Quad(&face_name_polys[1], face_name_sprites[person], &face_name_sprite);
            Setup_Sprite_Quad(&face_name_polys[0], face_name_sprites[person], &face_name_sprite);
            slide = 0;
            place_quad_x(&face_polys[buffer_num], SCREEN_W);
            place_quad_x(&face_name_polys[buffer_num], -0x8c);
        } else {
            slide += 2;
            place_quad_x(&face_polys[buffer_num], SCREEN_W - slide);
            place_quad_x(&face_name_polys[buffer_num], slide - 0x8c);
        }
        if (pad.type != PAD_DIGITAL || (pad.pressed & PAD_CROSS))
            break;
        addPrim(&cdb->ot[2], &back_polys[buffer_num][0]);
        addPrim(&cdb->ot[2], &back_polys[buffer_num][1]);
        addPrim(&cdb->ot[1], &face_polys[buffer_num]);
        addPrim(&cdb->ot[1], &face_name_polys[buffer_num]);
        Draw_All(1);
    }
    Poll_Pad_Buttons();
    DrawSync(0);
    Init_Front_End();
}

/* ---- time trials ---- */

TrackRecord track_records[TRACK_RECORD_LEVELS] = { /* 0x80070D78: 2:30.00 on the race tracks */
    {2, 30, 0}, {2, 30, 0}, {2, 30, 0}, {2, 30, 0}, {2, 30, 0}, {2, 30, 0}, {2, 30, 0},
};

/* Each race track's fastest lap becomes its level's record, the time to beat in a time trial. */
void Copy_Fastest_Laps_To_Track_Records(void)
{
    for (int i = 0; i < 7; i++) {
        TrackRecord *r = &track_records[track_levels[i] - 1];
        r->minutes = best_laps[i][0].minutes;
        r->seconds = best_laps[i][0].seconds;
        r->fraction = best_laps[i][0].fraction;
    }
}

SeasonStats season_stats[STAT_SEASONS];
int32_t stats_season;   /* 0x800798A4 */
int32_t seasons_played; /* 0x800798A8 */

/* The level's slot in the season's track table (0x80071500). */
static const int32_t track_stat_slots[TRACK_RECORD_LEVELS] = {0, 1, 4, 6, 3, 5, 2, 8, 9, 7, 10};

void Start_New_Season_Stats(void)
{
    stats_season = 0;
    seasons_played = 0;
    memset(season_stats, 0, sizeof(season_stats));
    stats_available = 1;
}

/* The next season's page; after five the oldest is dropped. */
void Start_Next_Season_Stats(void)
{
    stats_season++;
    seasons_played++;
    if (stats_season == STAT_SEASONS) {
        stats_season = STAT_SEASONS - 1;
        memmove(&season_stats[0], &season_stats[1], sizeof(season_stats[0]) * (STAT_SEASONS - 1));
    }
    memset(&season_stats[stats_season], 0, sizeof(season_stats[0]));
}

/* The race's winner, its wrecks and the cars wrecked or stuck before the end, on the level's line. */
void Update_Track_Stats(int level)
{
    TrackStat *t = &season_stats[stats_season].tracks[track_stat_slots[level]];
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        const RaceRecord *r = &race_records[i];
        if (r->position == 1)
            snprintf(t->winner, sizeof(t->winner), "%s", champ_info[i].name);
        if ((car_blocks[i].wrecked == 1 || car_blocks[i].motion_state == MOTION_STUCK) && r->finished == 0)
            t->wrecked++;
        t->wrecks = (int16_t)(t->wrecks + r->wrecks);
    }
}

/* Each driver's wins, wrecks dealt and times wrecked. */
void Update_Driver_Stats(void)
{
    DriverStat *d = season_stats[stats_season].drivers;
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        const RaceRecord *r = &race_records[i];
        if ((car_blocks[i].wrecked == 1 || car_blocks[i].motion_state == MOTION_STUCK) && r->finished == 0)
            d[i].wrecked++;
        if (r->position == 1)
            d[i].wins++;
        d[i].wrecks = (uint8_t)(d[i].wrecks + r->wrecks);
        snprintf(d[i].name, sizeof(d[i].name), "%s", champ_info[i].name);
    }
}

/* The final table: the drivers' names by league place. */
void Update_Championship_Stats(void)
{
    for (int i = 0; i < CHAMP_RECORDS; i++) {
        const ChampRecord *e = &champ_info[i];
        snprintf(season_stats[stats_season].standings[e->rank + e->division * DIVISION_SLOTS], 16, "%s", e->name);
    }
}

/* A lap faster than the track's best goes at the top of its table, under a name asked for. */
void Update_Jimmy_Spunk_Times(int track, int level)
{
    const TrackRecord *r = &track_records[level];
    BestLap *laps = best_laps[track];
    uint32_t lap = (uint32_t)r->minutes << 24 | (uint32_t)r->seconds << 16 | (uint32_t)r->fraction;
    uint32_t best = (uint32_t)laps[0].minutes << 24 | (uint32_t)laps[0].seconds << 16 | laps[0].fraction;
    if (lap < best && Enter_Driver_Names(0, 1) != 0) {
        memmove(&laps[1], &laps[0], sizeof(laps[0]) * 4);
        laps[0].minutes = (uint16_t)r->minutes;
        laps[0].seconds = (uint16_t)r->seconds;
        laps[0].fraction = (uint16_t)r->fraction;
        snprintf(laps[0].name, sizeof(laps[0].name), "%s", lap_name_entry);
    }
}

/* ---- GENERAL.BIN: the statistics menu ---- */

#define STAT_INK {0xff, 0xff, 0xff, 0}
#define TRACK_STAT_INK {0xf0, 0xf0, 0xf0, 0}

/* The seasons the pages show: the first is numbered 1, or after five seasons the number the original
 * gives (seasons_played + 1, where the oldest page kept is seasons_played - 3; kept as it is). */
int Get_First_Recorded_Season(void)
{
    return seasons_played > 4 ? seasons_played + 1 : 1;
}

int Get_Current_Recording_Season(void)
{
    return stats_season + 1;
}

/* Champ Stats: a season's final table, by league place. */
enum { CS2_PAD2, CS2_TCANCEL, CS2_IVIEWSTA, CS2_SIDEONLY, CS2_TMOVE, STATS_PAGE_SPRITES };

static Sprite champ_stats_sprites[STATS_PAGE_SPRITES] = { /* 0x801ECA84 */
    [CS2_PAD2] = BUTTON_SPRITE(260, 24),
    [CS2_TCANCEL] = BUTTON_SPRITE(278, 29),
    [CS2_IVIEWSTA] = BUTTON_SPRITE(16, 24),
    [CS2_SIDEONLY] = BUTTON_SPRITE(256, 41),
    [CS2_TMOVE] = BUTTON_SPRITE(282, 50),
};

static ScreenPoly champ_stats_polys[] = { /* 0x801ECB50 */
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &champ_stats_sprites[CS2_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &champ_stats_sprites[CS2_TCANCEL]},
    {2, "IVIEWSTA", SCREEN_GREY, &champ_stats_sprites[CS2_IVIEWSTA]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &champ_stats_sprites[CS2_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &champ_stats_sprites[CS2_TMOVE]},
    {0, NULL, 0, NULL},
};

static const ScreenLine champ_stats_lines[] = { /* 0x801ECAFC */
    {1, 54, 52, 159, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 159, 0, 0, 0, 1},
    {2, 21, 100, 131, 0x9d, 0xd2, 0xff, 2},
    {2, 163, 100, 132, 0x9d, 0xd2, 0xff, 2},
    {2, 41, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {2, 183, 89, 113, 0x9d, 0xd2, 0xff, 3},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const int16_t champ_stats_panel[4] = {-142, -42, 282, 120}; /* 0x801EDB08 */
static int32_t champ_stats_page;           /* 0x801EDB20 */
static char champ_stats_names[20][0x20];   /* 0x801F26E0 */
static char champ_stats_season[0x20];      /* 0x801F2960 */
static ScreenText champ_stats_text[5 + 20 * 2 + 1]; /* 0x801ECBB0 */

static const char *const stat_places[20] = {
    "%R%JL%T/1", "%R%JL%T/2", "%R%JL%T/3", "%R%JL%T/4", "%R%JL%T/5", "%R%JL%T/6", "%R%JL%T/7",
    "%R%JL%T/8", "%R%JL%T/9", "%R%JL%T/10", "%R%JL%T/11", "%R%JL%T/12", "%R%JL%T/13", "%R%JL%T/14",
    "%R%JL%T/15", "%R%JL%T/16", "%R%JL%T/17", "%R%JL%T/18", "%R%JL%T/19", "%R%JL%T/20",
};

/* The original lists the records out; they follow this pattern: two columns of ten. */
static void build_champ_stats_text(void)
{
    static const int16_t columns[2][2] = {{-138, -114}, {4, 28}};
    ScreenText *p = champ_stats_text;
    *p++ = (ScreenText){1, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Champ Stats"};
    *p++ = (ScreenText){2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Champ Stats"};
    *p++ = (ScreenText){1, 5, STAT_INK, -140, -56, SCREEN_Z, champ_stats_season};
    for (int c = 0; c < 2; c++) {
        *p++ = (ScreenText){2, 2, STAT_INK, columns[c][0], -37, SCREEN_Z, "%R%JL%T/Ps."};
        *p++ = (ScreenText){2, 2, STAT_INK, columns[c][1], -37, SCREEN_Z, "%R%JL%T/Driver"};
    }
    for (int r = 0; r < 20; r++) {
        int16_t y = (int16_t)(-22 + r % 10 * 10);
        *p++ = (ScreenText){2, 2, STAT_INK, columns[r / 10][0], y, SCREEN_Z, stat_places[r]};
        *p++ = (ScreenText){2, 2, STAT_INK, columns[r / 10][1], y, SCREEN_Z, champ_stats_names[r]};
    }
    *p = (ScreenText){0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL};
}

void Format_Champ_Stats_Rows(void)
{
    for (int i = 0; i < 20; i++)
        snprintf(champ_stats_names[i], sizeof(champ_stats_names[i]), "%%R%%JL%%T/%s",
                 season_stats[champ_stats_page].standings[i]);
    snprintf(champ_stats_season, sizeof(champ_stats_season), "%%R%%JL%%T/Season %d",
             Get_First_Recorded_Season() + champ_stats_page);
}

static void Prev_Stats_Season(void) /* 0x801E8584 */
{
    if (champ_stats_page > 0)
        champ_stats_page--;
    Format_Champ_Stats_Rows();
}

void Next_Stats_Season(void)
{
    if (champ_stats_page != Get_Current_Recording_Season() - 1)
        champ_stats_page++;
    Format_Champ_Stats_Rows();
}

/* A statistics page: Left and Right turn it, Triangle goes back. Returns `result`. */
static int stats_page(ScreenPoly *polys, ScreenText *text, const ScreenLine *lines, const int16_t *panel,
                      void (*prev)(void), void (*next)(void), int result)
{
    Setup_Screen_Polys(polys);
    Setup_Screen_Text(text);
    Setup_Screen_Lines(lines);
    Rotate_Slab_On(panel);
    for (;;) {
        bool changed = false;
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            prev();
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            next();
            changed = true;
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(panel);
            return result;
        }
        if (changed) {
            Setup_Screen_Text(text);
            Setup_Screen_Polys(polys);
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_Semi_Trans_Poly(panel[0], panel[1], panel[2], panel[3]);
        Draw_All(1);
    }
}

int Champ_Stats_Screen(void)
{
    build_champ_stats_text();
    Format_Champ_Stats_Rows();
    return stats_page(champ_stats_polys, champ_stats_text, champ_stats_lines, champ_stats_panel, Prev_Stats_Season,
                      Next_Stats_Season, 1);
}

/* Driver Stats: a driver's picture and their line of each season. */
enum { DS_PAD2, DS_TCANCEL, DS_IVIEWSTA, DS_DRIVER, DS_SIDEONLY, DS_TMOVE, DRIVER_STATS_SPRITES };

static Sprite driver_stats_sprites[DRIVER_STATS_SPRITES] = { /* 0x801ECF70 */
    [DS_PAD2] = BUTTON_SPRITE(260, 24),
    [DS_TCANCEL] = BUTTON_SPRITE(278, 29),
    [DS_IVIEWSTA] = BUTTON_SPRITE(16, 24),
    [DS_DRIVER] = TRANS_SPRITE(16, 72),
    [DS_SIDEONLY] = BUTTON_SPRITE(256, 41),
    [DS_TMOVE] = BUTTON_SPRITE(282, 50),
};

static ScreenPoly driver_stats_polys[] = { /* 0x801ED054 */
    {1, "DRIVER0", SCREEN_GREY, &driver_stats_sprites[DS_DRIVER]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &driver_stats_sprites[DS_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &driver_stats_sprites[DS_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &driver_stats_sprites[DS_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &driver_stats_sprites[DS_TMOVE]},
    {2, "IVIEWSTA", SCREEN_GREY, &driver_stats_sprites[DS_IVIEWSTA]},
    {0, NULL, 0, NULL},
};

static const ScreenLine driver_stats_lines[] = { /* 0x801ED000 */
    {1, 54, 52, 152, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 152, 0, 0, 0, 1},
    {2, 153, 113, 124, 0x9d, 0xd4, 0xff, 2},
    {2, 194, 103, 74, 0x9d, 0xd2, 0xff, 3},
    {2, 224, 103, 74, 0x9d, 0xd2, 0xff, 3},
    {2, 251, 103, 74, 0x9d, 0xd2, 0xff, 3},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const int16_t driver_stats_panel[4] = {-10, -28, 132, 81}; /* 0x801EDB24 */
static int32_t stats_driver;                 /* 0x801EDB44 */
static char driver_stats_name[0x20];         /* 0x801F2980 */
static char driver_stats_car[0x10];          /* 0x801F29A0 */
static char driver_stats_cells[4][STAT_SEASONS][0x10]; /* 0x801F29B0: season, wins, kills, did not finish */
static SpriteInfo *driver_picture;           /* 0x801F2AF0: DRIVER0, whose picture is replaced */
static int16_t driver_picture_clut[2];       /* 0x801F2AF4: its own colour table */

static ScreenText driver_stats_text[] = { /* 0x801ED0C4 */
    {1, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Driver Stats"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Driver Stats"},
    {2, 5, STAT_INK, 0, -44, SCREEN_Z, driver_stats_name},
    {2, 1, STAT_INK, -80, 75, SCREEN_Z, driver_stats_car},
    {2, 2, STAT_INK, -6, -23, SCREEN_Z, "%R%JL%T/Season"},
    {2, 2, STAT_INK, 38, -23, SCREEN_Z, "%R%JL%T/Wins"},
    {2, 2, STAT_INK, 68, -23, SCREEN_Z, "%R%JL%T/Kills"},
    {2, 2, STAT_INK, 95, -23, SCREEN_Z, "%R%JL%T/DNF"},
#define DRIVER_ROW(r) \
    {2, 2, STAT_INK, 12, -8 + (r) * 12, SCREEN_Z, driver_stats_cells[0][r]}, \
    {2, 2, STAT_INK, 38, -8 + (r) * 12, SCREEN_Z, driver_stats_cells[1][r]}, \
    {2, 2, STAT_INK, 68, -8 + (r) * 12, SCREEN_Z, driver_stats_cells[2][r]}, \
    {2, 2, STAT_INK, 95, -8 + (r) * 12, SCREEN_Z, driver_stats_cells[3][r]}
    DRIVER_ROW(0), DRIVER_ROW(1), DRIVER_ROW(2), DRIVER_ROW(3), DRIVER_ROW(4),
#undef DRIVER_ROW
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static void Find_Driver_Picture(void) /* 0x801E8888 */
{
    driver_picture = (SpriteInfo *)Search_For_Sprite("DRIVER0");
    driver_picture_clut[0] = driver_picture->clut_x;
    driver_picture_clut[1] = driver_picture->clut_y;
}

/* The driver's picture from DRIVERS.BIN over DRIVER0's, with their colour table. */
void Set_Stats_Driver_Sprite(void)
{
    RECT area = {driver_picture->vram_x, driver_picture->vram_y, 0x40, 0x80};
    int32_t offset;
    memcpy(&offset, drivers_data + stats_driver * 4, sizeof(offset));
    u_long *picture = MPE_malloc(17000);
    DecompressState d = {drivers_data + offset, (uint8_t *)picture, 0, 0x4000, 0};
    Decompress(&d);
    DrawSync(0);
    LoadImage(&area, picture);
    DrawSync(0);
    MPE_free(picture); /* PSX: libc free() on an MPE_malloc block */
    if (stats_driver == 0) {
        driver_picture->clut_x = driver_picture_clut[0];
        driver_picture->clut_y = driver_picture_clut[1];
    } else {
        char name[16];
        snprintf(name, sizeof(name), "DRIVER%d", stats_driver);
        const SpriteInfo *s = Search_For_Sprite(name);
        driver_picture->clut_x = s->clut_x;
        driver_picture->clut_y = s->clut_y;
    }
}

void Format_Driver_Stats_Title(void)
{
    snprintf(driver_stats_name, sizeof(driver_stats_name), "%%R%%JL%%T/%s",
             season_stats[0].drivers[stats_driver].name);
    snprintf(driver_stats_car, sizeof(driver_stats_car), "%%R%%JC%%T/Car %02d", car_textures[stats_driver]);
}

/* The driver's line of each recorded season, the rest of the five rows empty. The name is the first
 * season's, as on the PSX. */
void Format_Driver_Stats_Rows(void)
{
    int season = Get_First_Recorded_Season();
    int seasons = Get_Current_Recording_Season();
    int row = 0;
    for (; row < seasons; row++, season++) {
        const DriverStat *d = &season_stats[row].drivers[stats_driver];
        snprintf(driver_stats_cells[0][row], 0x10, "%%R%%JL%%T/%d", season);
        snprintf(driver_stats_cells[1][row], 0x10, "%%R%%JL%%T/%d", (int8_t)d->wins);
        snprintf(driver_stats_cells[2][row], 0x10, "%%R%%JL%%T/%d", (int8_t)d->wrecks);
        snprintf(driver_stats_cells[3][row], 0x10, "%%R%%JL%%T/%d", d->wrecked);
    }
    for (; row < STAT_SEASONS; row++)
        for (int c = 0; c < 4; c++)
            driver_stats_cells[c][row][0] = '\0';
    Format_Driver_Stats_Title();
}

int Next_Stats_Driver(void)
{
    stats_driver = stats_driver < 1 ? 0x13 : stats_driver - 1;
    Set_Stats_Driver_Sprite();
    Format_Driver_Stats_Rows();
    Format_Driver_Stats_Title();
    return 0;
}

static void Stats_Driver_Right(void) /* 0x801E8EDC */
{
    stats_driver = stats_driver < 0x13 ? stats_driver + 1 : 0;
    Set_Stats_Driver_Sprite();
    Format_Driver_Stats_Rows();
    Format_Driver_Stats_Title();
}

static void next_driver(void)
{
    Next_Stats_Driver();
}

/* Left and Right step through the cars; leaving gives DRIVER0 its own colours back. */
int View_Driver_Stats(void)
{
    Find_Driver_Picture();
    Set_Stats_Driver_Sprite();
    Format_Driver_Stats_Rows();
    int result = stats_page(driver_stats_polys, driver_stats_text, driver_stats_lines, driver_stats_panel, next_driver,
                            Stats_Driver_Right, 0);
    driver_picture->clut_x = driver_picture_clut[0];
    driver_picture->clut_y = driver_picture_clut[1];
    return result;
}

/* Track Stats: a track's winner, kills and cars that did not finish, each season. */
static Sprite track_stats_sprites[STATS_PAGE_SPRITES] = { /* 0x801ED5E4 */
    [CS2_PAD2] = BUTTON_SPRITE(260, 24),
    [CS2_TCANCEL] = BUTTON_SPRITE(278, 29),
    [CS2_IVIEWSTA] = BUTTON_SPRITE(16, 24),
    [CS2_SIDEONLY] = BUTTON_SPRITE(256, 41),
    [CS2_TMOVE] = BUTTON_SPRITE(282, 50),
};

static ScreenPoly track_stats_polys[] = { /* 0x801ED6B0 */
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &track_stats_sprites[CS2_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &track_stats_sprites[CS2_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &track_stats_sprites[CS2_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &track_stats_sprites[CS2_TMOVE]},
    {2, "IVIEWSTA", SCREEN_GREY, &track_stats_sprites[CS2_IVIEWSTA]},
    {0, NULL, 0, NULL},
};

static const ScreenLine track_stats_lines[] = { /* 0x801ED65C */
    {1, 54, 52, 146, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 146, 0, 0, 0, 1},
    {2, 53, 103, 210, 0x9d, 0xd4, 0xff, 2},
    {2, 93, 93, 74, 0x9d, 0xd2, 0xff, 3},
    {2, 192, 93, 74, 0x9d, 0xd2, 0xff, 3},
    {2, 219, 93, 74, 0x9d, 0xd2, 0xff, 3},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static const int16_t track_stats_panel[4] = {-110, -38, 218, 81}; /* 0x801EDB70 */
static int32_t stats_track;                         /* 0x801EDB8C */
static char track_stats_season[STAT_SEASONS][0xc];  /* 0x801F2AFC */
static char track_stats_winner[STAT_SEASONS][0x20]; /* 0x801F2B38 */
static char track_stats_kills[STAT_SEASONS][0xc];   /* 0x801F2BD8 */
static char track_stats_dnf[STAT_SEASONS][0xc];     /* 0x801F2C14 */

static ScreenText track_stats_text[] = { /* 0x801ED710 */
    {1, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Track Stats"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Track Stats"},
    {1, 5, TRACK_STAT_INK, -106, -56, SCREEN_Z, ""},
    {2, 2, STAT_INK, -106, -33, SCREEN_Z, "%R%JL%T/Season"},
    {2, 2, STAT_INK, -63, -33, SCREEN_Z, "%R%JL%T/Winner"},
    {2, 2, STAT_INK, 36, -33, SCREEN_Z, "%R%JL%T/Kills"},
    {2, 2, STAT_INK, 63, -33, SCREEN_Z, "%R%JL%T/DNF"},
#define TRACK_ROW(r) \
    {2, 2, TRACK_STAT_INK, -88, -18 + (r) * 12, SCREEN_Z, track_stats_season[r]}, \
    {2, 2, TRACK_STAT_INK, -63, -18 + (r) * 12, SCREEN_Z, track_stats_winner[r]}, \
    {2, 2, TRACK_STAT_INK, 36, -18 + (r) * 12, SCREEN_Z, track_stats_kills[r]}, \
    {2, 2, TRACK_STAT_INK, 63, -18 + (r) * 12, SCREEN_Z, track_stats_dnf[r]}
    TRACK_ROW(0), TRACK_ROW(1), TRACK_ROW(2), TRACK_ROW(3), TRACK_ROW(4),
#undef TRACK_ROW
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

/* The tracks in the order of the season pages (0x801ED5B8). */
static const char *const stat_track_names[TRACK_RECORD_LEVELS] = {
    "%R%JL%T/Pine Hills Raceway", "%R%JL%T/Chalk Canyon", "%R%JL%T/S.C.A. Motorplex",
    "%R%JL%T/Caprio County Raceway", "%R%JL%T/Black Sail Valley", "%R%JL%T/Liberty City",
    "%R%JL%T/Ultimate Destruction", "%R%JL%T/Red Pike Arena", "%R%JL%T/The Colosseum", "%R%JL%T/The Pit",
    "%R%JL%T/Death Bowl",
};

void Format_Track_Stats_Rows(void)
{
    int season = Get_First_Recorded_Season();
    int seasons = Get_Current_Recording_Season();
    int row = 0;
    for (; row < seasons; row++, season++) {
        const TrackStat *t = &season_stats[row].tracks[stats_track];
        snprintf(track_stats_season[row], 0xc, "%%R%%JC%%T/%d", season);
        snprintf(track_stats_winner[row], 0x20, "%%R%%JL%%T/%s", t->winner);
        snprintf(track_stats_kills[row], 0xc, "%%R%%JL%%T/%d", t->wrecks);
        snprintf(track_stats_dnf[row], 0xc, "%%R%%JL%%T/%d", t->wrecked);
    }
    for (; row < STAT_SEASONS; row++) {
        track_stats_season[row][0] = '\0';
        track_stats_winner[row][0] = '\0';
        track_stats_kills[row][0] = '\0';
        track_stats_dnf[row][0] = '\0';
    }
    track_stats_text[2].text = stat_track_names[stats_track];
}

static void Prev_Stats_Track(void) /* 0x801E9B44 */
{
    stats_track = stats_track < 1 ? 10 : stats_track - 1;
    Format_Track_Stats_Rows();
}

static void Next_Stats_Track(void) /* 0x801E9BC4 */
{
    stats_track = stats_track < 10 ? stats_track + 1 : 0;
    Format_Track_Stats_Rows();
}

int View_Track_Stats(void)
{
    stats_track = 0;
    Format_Track_Stats_Rows();
    return stats_page(track_stats_polys, track_stats_text, track_stats_lines, track_stats_panel, Prev_Stats_Track,
                      Next_Stats_Track, 0);
}

/* The statistics menu: drivers, tracks, championship. */
enum { SM_VIEWDRIV, SM_VIEWTRAK, SM_VIEWCHAM, SM_RING, SM_PAD2, SM_TCANCEL, SM_PAD1, SM_TSELECT, SM_IVIEWSTA, SM_SIDEONLY, SM_TMOVE, STATS_MENU_SPRITES };

static Sprite stats_menu_sprites[STATS_MENU_SPRITES] = { /* 0x801ED308 */
    [SM_VIEWDRIV] = BUTTON_SPRITE(32, 96),
    [SM_VIEWTRAK] = BUTTON_SPRITE(128, 96),
    [SM_VIEWCHAM] = BUTTON_SPRITE(224, 96),
    [SM_RING] = TRANS_SPRITE(31, 94),
    [SM_PAD2] = BUTTON_SPRITE(260, 24),
    [SM_TCANCEL] = BUTTON_SPRITE(278, 29),
    [SM_PAD1] = BUTTON_SPRITE(260, 41),
    [SM_TSELECT] = BUTTON_SPRITE(278, 46),
    [SM_IVIEWSTA] = BUTTON_SPRITE(16, 24),
    [SM_SIDEONLY] = BUTTON_SPRITE(256, 58),
    [SM_TMOVE] = BUTTON_SPRITE(282, 67),
};

static ScreenPoly stats_menu_polys[] = { /* 0x801ED434 */
    {2, "VIEWDRIV", SCREEN_GREY, &stats_menu_sprites[SM_VIEWDRIV]},
    {2, "VIEWTRAK", SCREEN_GREY, &stats_menu_sprites[SM_VIEWTRAK]},
    {2, "VIEWCHAM", SCREEN_GREY, &stats_menu_sprites[SM_VIEWCHAM]},
    {2, "IVIEWSTA", SCREEN_GREY, &stats_menu_sprites[SM_IVIEWSTA]},
    {2, "ZZZZPAD1", SCREEN_GREY_TRANS, &stats_menu_sprites[SM_PAD1]},
    {2, "TSELECT", SCREEN_GREY_TRANS, &stats_menu_sprites[SM_TSELECT]},
    {2, "ZZZZPAD2", SCREEN_GREY_TRANS, &stats_menu_sprites[SM_PAD2]},
    {2, "TCANCEL", SCREEN_GREY_TRANS, &stats_menu_sprites[SM_TCANCEL]},
    {2, "SIDEONLY", SCREEN_GREY_TRANS, &stats_menu_sprites[SM_SIDEONLY]},
    {2, "TMOVE", SCREEN_GREY_TRANS, &stats_menu_sprites[SM_TMOVE]},
    {1, "RING", SCREEN_HIGHLIGHT, &stats_menu_sprites[SM_RING]},
    {0, NULL, 0, NULL},
};

static ScreenText stats_menu_text[] = { /* 0x801ED50C */
    {1, 0, {0x80, 0x64, 0x64, 0}, -104, -96, SCREEN_Z, "%R%JL%T/Statistics"},
    {2, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, "%R%JL%T/Statistics"},
    {1, 1, GREY_INK, 0, 91, SCREEN_Z, ""},
    {2, 4, GREY_INK, 3, 94, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static const ScreenLine stats_menu_lines[] = { /* 0x801ED410 */
    {1, 54, 52, 122, 0xff, 0xc8, 0xc8, 0},
    {2, 57, 55, 122, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

static void set_stats_caption(const char *caption)
{
    stats_menu_text[2].text = caption;
    stats_menu_text[3].text = caption;
    Setup_Screen_Text(stats_menu_text);
}

void Show_View_Driver_Stats_Caption(void)
{
    set_stats_caption("%R%JC%T/View Driver Stats");
}

void Show_View_Track_Stats_Caption(void)
{
    set_stats_caption("%R%JC%T/View Track Stats");
}

void Show_View_Champ_Stats_Caption(void)
{
    set_stats_caption("%R%JC%T/View Championship Stats");
}

static const SubMenuItem stats_menu_items[3] = { /* 0x801ED574 */
    {32, 94, Show_View_Driver_Stats_Caption, View_Driver_Stats, Null_Menu_Callback, 0},
    {128, 94, Show_View_Track_Stats_Caption, View_Track_Stats, Null_Menu_Callback, 0},
    {225, 94, Show_View_Champ_Stats_Caption, Champ_Stats_Screen, Null_Menu_Callback, 0x10000},
};

static const uint8_t stats_menu_moves[3][4] = {{0, 0, 1, 0}, {0, 1, 2, 1}, {1, 2, 2, 2}}; /* 0x801ED5AC */
static const char *const stats_menu_item_sprites[3] = {"VIEWDRIV", "VIEWTRAK", "VIEWCHAM"};    /* 0x801ED4F4 */
static const char *const stats_menu_pressed_sprites[3] = {"VIEWDRIP", "VIEWTRAP", "VIEWCHAP"}; /* 0x801ED500 */

/* The pages' results are not used: only Triangle leaves. */
int Run_Statistics_Menu(void)
{
    int item = 0;
    bool changed = false;
    stats_menu_sprites[SM_RING].x = stats_menu_items[0].x;
    stats_menu_sprites[SM_RING].y = stats_menu_items[0].y;
    stats_menu_items[0].caption();
    Setup_Screen_Polys(stats_menu_polys);
    Setup_Screen_Text(stats_menu_text);
    Setup_Screen_Lines(stats_menu_lines);
    Rotate_Slab_On(NULL);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = stats_menu_moves[item][0];
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            item = stats_menu_moves[item][2];
            changed = true;
        } else if (pad.pressed & PAD_CROSS) {
            stats_menu_polys[item].name = stats_menu_pressed_sprites[item];
            Setup_Screen_Polys(stats_menu_polys);
            Button_Pressed(7, NULL);
            stats_menu_polys[item].name = stats_menu_item_sprites[item];
            Setup_Screen_Polys(stats_menu_polys);
            Button_Pressed(2, NULL);
            Rotate_Slab_Off(NULL);
            stats_menu_items[item].select();
            stats_menu_items[item].caption();
            Setup_Screen_Polys(stats_menu_polys);
            Setup_Screen_Text(stats_menu_text);
            Setup_Screen_Lines(stats_menu_lines);
            Rotate_Slab_On(NULL);
            continue;
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(NULL);
            return 0;
        }
        if (changed) {
            changed = false;
            stats_menu_sprites[SM_RING].x = stats_menu_items[item].x;
            stats_menu_sprites[SM_RING].y = stats_menu_items[item].y;
            Setup_Screen_Polys(stats_menu_polys);
            stats_menu_items[item].caption();
        }
        Draw_Slab();
        Print_Draw();
        Draw_Screen_Polys();
        Draw_Screen_Lines();
        Draw_All(1);
    }
}
