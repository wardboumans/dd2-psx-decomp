#include "game/overlays.h"

#include "game/camera.h"
#include "game/car.h"
#include "game/car_motion.h"
#include "game/frontend_init.h"
#include "game/play_game.h"
#include "game/replay.h"
#include "game/sound_fx.h"
#include "game/track.h"
#include "game/stats.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgte.h"
#include "sound/cd.h"
#include "sound/sound.h"
#include "system/controller.h"

HudSprite hud_sprites[HUD_COUNT];
POLY_F4 pit_panel[2];
POLY_FT4 pit_panel_mode[2];
POLY_F4 replay_panel[2];
POLY_FT4 replay_panel_mode[2];
int32_t derby_time[3];
int32_t pit_countdown;

/* Where each overlay goes and how it is drawn (0x8006F06C); Setup_Sprite_Quad fills in the rest. */
static Sprite hud_templates[75] = {
    {128, 50, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 0: 0x8006f06c */
    {129, 84, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 1: 0x8006f084 */
    {161, 84, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 2: 0x8006f09c */
    {129, 84, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 3: 0x8006f0b4 */
    {161, 84, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 4: 0x8006f0cc */
    {129, 52, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 5: 0x8006f0e4 */
    {161, 52, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 6: 0x8006f0fc */
    {129, 52, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 7: 0x8006f114 */
    {161, 52, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 8: 0x8006f12c */
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 9: 0x8006f144 */
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 10: 0x8006f15c */
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 11: 0x8006f174 */
    {140, 60, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 12: 0x8006f18c */
    {220, 202, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 13: 0x8006f1a4 */
    {0, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 14: 0x8006f1bc */
    {250, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 15: 0x8006f1d4 */
    {0, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 16: 0x8006f1ec */
    {250, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 17: 0x8006f204 */
    {65, 114, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 18: 0x8006f21c */
    {56, 91, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 19: 0x8006f234 */
    {59, 76, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 20: 0x8006f24c */
    {25, 86, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 21: 0x8006f264 */
    {25, 86, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 22: 0x8006f27c */
    {25, 86, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 23: 0x8006f294 */
    {25, 110, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 24: 0x8006f2ac */
    {25, 110, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 25: 0x8006f2c4 */
    {25, 110, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 26: 0x8006f2dc */
    {21, 67, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 27: 0x8006f2f4 */
    {130, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 28: 0x8006f30c */
    {158, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 29: 0x8006f324 */
    {8, 208, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 30: 0x8006f33c */
    {36, 208, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 31: 0x8006f354 */
    {71, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 32: 0x8006f36c */
    {80, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 33: 0x8006f384 */
    {89, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 34: 0x8006f39c */
    {101, 18, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 35: 0x8006f3b4 */
    {228, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 36: 0x8006f3cc */
    {237, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 37: 0x8006f3e4 */
    {71, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 38: 0x8006f3fc */
    {80, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 39: 0x8006f414 */
    {89, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 40: 0x8006f42c */
    {201, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 41: 0x8006f444 */
    {210, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 42: 0x8006f45c */
    {219, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 43: 0x8006f474 */
    {228, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 44: 0x8006f48c */
    {237, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 45: 0x8006f4a4 */
    {276, 176, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 46: 0x8006f4bc */
    {276, 176, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 47: 0x8006f4d4 */
    {276, 207, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 48: 0x8006f4ec */
    {276, 232, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 49: 0x8006f504 */
    {71, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 50: 0x8006f51c */
    {80, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 51: 0x8006f534 */
    {93, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 52: 0x8006f54c */
    {102, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 53: 0x8006f564 */
    {115, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 54: 0x8006f57c */
    {124, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 55: 0x8006f594 */
    {184, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 56: 0x8006f5ac */
    {193, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 57: 0x8006f5c4 */
    {206, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 58: 0x8006f5dc */
    {215, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 59: 0x8006f5f4 */
    {228, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 60: 0x8006f60c */
    {237, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 61: 0x8006f624 */
    {41, 224, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 62: 0x8006f63c */
    {50, 224, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 63: 0x8006f654 */
    {63, 224, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 64: 0x8006f66c */
    {72, 224, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 65: 0x8006f684 */
    {85, 224, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 66: 0x8006f69c */
    {94, 224, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 67: 0x8006f6b4 */
    {0, 34, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 68: 0x8006f6cc */
    {250, 16, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 69: 0x8006f6e4 */
    {25, 30, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 70: 0x8006f6fc */
    {45, 35, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 71: 0x8006f714 */
    {25, 50, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 72: 0x8006f72c */
    {45, 51, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 73: 0x8006f744 */
    {260, 30, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* 74: 0x8006f75c */};

#define T(i) (&hud_templates[i])

/* Which template and sprite each overlay uses. */
static const struct {
    int hud;
    const char *name;
    int template_index;
} hud_setup[] = {
    {HUD_LIGHT_SURROUND, "LIGHTSUR", 0},
    {HUD_RED_LIGHT_1, "REDLIGHT", 5}, {HUD_RED_LIGHT_2, "REDLIGHT", 6},
    {HUD_RED_DIM_1, "REDDIM", 7}, {HUD_RED_DIM_2, "REDDIM", 8},
    {HUD_GREEN_LIGHT_1, "GRNLIGHT", 1}, {HUD_GREEN_LIGHT_2, "GRNLIGHT", 2},
    {HUD_GREEN_DIM_1, "GREENDIM", 3}, {HUD_GREEN_DIM_2, "GREENDIM", 4},
    {HUD_EXIT_PIT, "EXITPIT", 18}, {HUD_REPAIR, "REPAIR", 19}, {HUD_SPIN_LEFT, "SPINLEFT", 20},
};
static const struct {
    int hud;
    const char *name;
    int template_index;
} hud_setup2[] = {
    {HUD_PAD_X, "X", 21}, {HUD_PAD_MAD_I, "MAD_I", 22}, {HUD_PAD_NEG_I, "NEG_I", 23},
    {HUD_PAD_SQUARE, "SQUARE", 24}, {HUD_PAD_MAD_II, "MAD_II", 25}, {HUD_PAD_NEG_II, "NEG_II", 26},
    {HUD_SIDE_ONLY, "SIDEONLY", 27}, {HUD_DEMO, "DEMO", 13}, {HUD_1ST, "1ST", 9}, {HUD_2ND, "2ND", 10},
    {HUD_3RD, "3RD", 11}, {HUD_SPEED, "SPEEDIND", 14}, {HUD_STILL_RUNNING, "STILLRUN", 15},
    {HUD_RACE_POINTS, "RACEPOIN", 16}, {HUD_LAP, "LAPNUM", 17},
};

static void setup(int hud, int b, const char *name, int t)
{
    Setup_Sprite_Quad(&hud_sprites[hud].q[b], name, T(t));
}

/* A box drawn translucent black: its blend-mode packet, then the box. The race refreshes all but the
 * corners every frame it shows one. */
static void reset_panel(POLY_F4 *box, POLY_FT4 *mode)
{
    setPolyF4(box);
    setSemiTrans(box, 1);
    box->r0 = 0;
    box->g0 = 0;
    box->b0 = 0;
    setPolyFT4(mode);
    setUV4(mode, 0, 0, 0, 0, 0, 0, 0, 0);
    mode->tpage = GetTPage(0, 0, 0, 0);
    setSemiTrans(mode, 1);
}

static void setup_panel(POLY_F4 *box, POLY_FT4 *mode, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    reset_panel(box, mode);
    setXY4(box, x0, y0, x1, y0, x0, y1, x1, y1);
}

/* The race's sprite table (level section 3), and every overlay quad for both buffers. */
void Init_Overlays(void)
{
    static int adjusted; /* 0x800793EC: the templates are moved up once for NTSC */
    derby_time[0] = 0;
    derby_time[1] = 0;
    derby_time[2] = 0;
    if (pal_mode == 0 && adjusted == 0) {
        adjusted = 1;
        static const int moved[6] = {30, 31, 46, 47, 48, 49};
        for (int i = 0; i < 6; i++)
            hud_templates[moved[i]].y = (int16_t)(hud_templates[moved[i]].y - 0x10);
        for (int i = 0; i < 6; i++)
            hud_templates[62 + i].y = (int16_t)(hud_templates[62 + i].y - 0x10);
    }
    Load_Sprite_Info(level_data_section(3));
    int laps = track_info[current_level].laps;
    for (int b = 0; b < 2; b++) {
        for (size_t i = 0; i < sizeof(hud_setup) / sizeof(hud_setup[0]); i++)
            setup(hud_setup[i].hud, b, hud_setup[i].name, hud_setup[i].template_index);
        POLY_FT4 *spin = &hud_sprites[HUD_SPIN_LEFT].q[b];
        spin->x1 = (int16_t)(spin->x1 - 0x14);
        spin->x3 = (int16_t)(spin->x3 - 0x14);
        spin->u1 = (uint8_t)(spin->u1 - 0x14);
        spin->u3 = (uint8_t)(spin->u3 - 0x14);
        for (size_t i = 0; i < sizeof(hud_setup2) / sizeof(hud_setup2[0]); i++)
            setup(hud_setup2[i].hud, b, hud_setup2[i].name, hud_setup2[i].template_index);
        int big = race_type == RACE_TIME_TRIAL || race_mode == MODE_STOCK_CAR ? 30 : 28;
        setup(HUD_POSITION_1, b, "POSNUM", big);
        setup(HUD_POSITION_2, b, "POSNUM", big + 1);
        setup(HUD_SPEED_DIGIT_1, b, "SMALLNUM", 32);
        setup(HUD_SPEED_DIGIT_2, b, "SMALLNUM", 33);
        setup(HUD_SPEED_DIGIT_3, b, "SMALLNUM", 34);
        setup(HUD_KPH, b, "KPH", 35);
        setup(HUD_CARS_LEFT_1, b, "SMALLNUM", 36);
        setup(HUD_CARS_LEFT_2, b, "SMALLNUM", 37);
        setup(HUD_POINTS_DIGIT_1, b, "SMALLNUM", 38);
        setup(HUD_POINTS_DIGIT_2, b, "SMALLNUM", 39);
        setup(HUD_POINTS_DIGIT_3, b, "SMALLNUM", 40);
        setup(HUD_LAP_1, b, "SMALLNUM", 41);
        setup(HUD_LAP_2, b, "SMALLNUM", 42);
        setup(HUD_LAP_SLASH, b, "SMALLNUM", 43);
        Modify_Sprite(&hud_sprites[HUD_LAP_SLASH].q[b], T(43), 0x6e, 0, 0xb, 10);
        setup(HUD_LAPS_TENS, b, "SMALLNUM", 44);
        Modify_Sprite(&hud_sprites[HUD_LAPS_TENS].q[b], T(44), (int16_t)(laps / 10) * 0xb, 0, 0xb, 10);
        setup(HUD_LAPS_UNITS, b, "SMALLNUM", 45);
        Modify_Sprite(&hud_sprites[HUD_LAPS_UNITS].q[b], T(45), (int16_t)(laps % 10) * 0xb, 0, 0xb, 10);
        setup(HUD_CAR, b, "CAR", 46);
        Modify_Sprite(&hud_sprites[HUD_CAR].q[b], T(46), 0, 0, 0x1f, 0x3f);
        setup(HUD_DAMAGE_TOP, b, "DAMAGE", 47);
        Modify_Sprite(&hud_sprites[HUD_DAMAGE_TOP].q[b], T(47), 0, 0, 0x1f, 7);
        setup(HUD_DAMAGE_MIDDLE, b, "DAMAGE", 48);
        Modify_Sprite(&hud_sprites[HUD_DAMAGE_MIDDLE].q[b], T(48), 0, 8, 0x1f, 7);
        setup(HUD_DAMAGE_BOTTOM, b, "DAMAGE", 49);
        Modify_Sprite(&hud_sprites[HUD_DAMAGE_BOTTOM].q[b], T(49), 0, 0x10, 0x1f, 8);
        for (int i = 0; i < 6; i++) {
            setup(HUD_TIME_DIGITS + i, b, "SMALLNUM", 50 + i);
            setup(HUD_FAST_DIGITS + i, b, "SMALLNUM", 56 + i);
            setup(HUD_TOTAL_DIGITS + i, b, "SMALLNUM", 62 + i);
        }
        setup(HUD_LAP_TIME, b, "LAPTIME", 68);
        setup(HUD_FAST_LAP, b, "FASTLAP", 69);
        setup_panel(&pit_panel[b], &pit_panel_mode[b], 0x10, 0x46, 0x6e, 0x81);
        setup(HUD_POSITION_BIG, b, "POSNUM", 12);
        switch (pad.type) {
        case 1:
            setup(HUD_REPLAY_BUTTON_1, b, "RX", 72);
            setup(HUD_REPLAY_BUTTON_2, b, "RTRI", 70);
            break;
        case 2:
            setup(HUD_REPLAY_BUTTON_1, b, "NEG_I", 72);
            setup(HUD_REPLAY_BUTTON_2, b, "NEG_B", 70);
            break;
        case 3:
            setup(HUD_REPLAY_BUTTON_1, b, "MAD_I", 72);
            setup(HUD_REPLAY_BUTTON_2, b, "MAD_B", 70);
            break;
        default:
            break;
        }
        setup(HUD_REPLAY_SLOW_MOTION, b, "RSLOWMO", 73);
        setup(HUD_REPLAY_EXIT, b, "REXIT", 71);
        setup(HUD_REPLAY_R, b, "R", 74);
        setup_panel(&replay_panel[b], &replay_panel_mode[b], 0x10, 0x14, 0x4c, 0x4b);
    }
}

int32_t pit_second;           /* 0x8007971C: fraction of the pit countdown's second (0x10000) */
static int32_t pit_beep_wait; /* 0x800793E8: frames until the next countdown beep may sound */
static int32_t blink = 0x28;  /* 0x800794A8: flashing overlays show for 20 of every 41 frames */

static void draw_hud(int hud)
{
    addPrim(&cdb->ot[0], &hud_sprites[hud].q[buffer_num]);
}

static void draw_panel(POLY_F4 *box, POLY_FT4 *mode)
{
    addPrim(&cdb->ot[0], &box[buffer_num]);
    addPrim(&cdb->ot[0], &mode[buffer_num]);
}

static void digit(int hud, int template_index, int value)
{
    Modify_Sprite(&hud_sprites[hud].q[buffer_num], T(template_index), value * 0xb, 0, 0xb, 10);
}

/* A big digit (the POSNUM sheet, five to a row). */
static void big_digit(int hud, int template_index, int value)
{
    Modify_Sprite(&hud_sprites[hud].q[buffer_num], T(template_index), value * 0x20 % 0xa0,
                  value * 0x20 / 0xa0 * 0x1d, 0x1f, 0x1d);
}

static int blink_on(void)
{
    if (--blink < 0)
        blink = 0x28;
    return blink < 0x14;
}

static void beep(int sample)
{
    Play_Sound(COMMENTARY_CHANNEL, sample_bank, sample, SOUND_VOLUME_MAX, VOICE_PITCH, 0);
}

/* The start lights: red with three beeps while overlay_state counts down, then green with the start
 * signal (which also starts the music and the race clock). */
void Update_Race_CountDown(void)
{
    if (overlay_state == -50 || overlay_hold != 0)
        return;
    int32_t was = overlay_state--;
    if (was >= 3 && was <= 100) {
        if (overlay_state == 91)
            beep(SAMPLE_COUNT_3);
        draw_hud(HUD_RED_LIGHT_1);
        draw_hud(HUD_RED_LIGHT_2);
        draw_hud(HUD_GREEN_DIM_1);
        draw_hud(HUD_GREEN_DIM_2);
        if (overlay_state == 61)
            beep(SAMPLE_COUNT_2);
        if (overlay_state == 31)
            beep(SAMPLE_COUNT_1);
        draw_hud(HUD_LIGHT_SURROUND);
    }
    if (overlay_state >= -24 && overlay_state <= 1) {
        int red = overlay_state < -2 ? HUD_RED_DIM_1 : HUD_RED_LIGHT_1;
        draw_hud(red);
        draw_hud(red + 1);
        if (overlay_state == 1) {
            beep(SAMPLE_COUNT_GO);
            Start_CD_Audio();
            frame_count = 0;
        }
        draw_hud(HUD_GREEN_LIGHT_1);
        draw_hud(HUD_GREEN_LIGHT_2);
        draw_hud(HUD_LIGHT_SURROUND);
    }
}

/* Ticks a clock's fraction of a second on by a frame; 1 when a second is complete. */
static int tick_second(int32_t *fraction)
{
    if (*fraction + CLOCK_TICK >= CLOCK_SECOND) {
        *fraction -= CLOCK_SECOND - CLOCK_TICK;
        return 1;
    }
    *fraction += CLOCK_TICK;
    return 0;
}

/* The pit stop's countdown: beeps on its last seconds, and leaves the pit at 0. */
static void pit_clock(void)
{
    static const int beeps[4] = {SAMPLE_COUNT_GO, SAMPLE_COUNT_1, SAMPLE_COUNT_2, SAMPLE_COUNT_3};
    if (pit_beep_wait != 0)
        pit_beep_wait--;
    if (tick_second(&pit_second) && pit_beep_wait == 0) {
        if (pit_camera_state != 0)
            pit_countdown--;
        if (pit_countdown >= 0 && pit_countdown <= 3)
            beep(beeps[pit_countdown]);
        pit_beep_wait = 8;
    }
    if (pit_countdown == 0)
        Exit_Pit_Camera();
}

/* mm:ss:hh in six digits from `first`, placed by the templates from `template_index` down. */
static void time_digits(int first, int template_index, int32_t minutes, int32_t seconds, int32_t fraction)
{
    int32_t hundredths = fraction * 100 >> 16;
    digit(first + 5, template_index, hundredths % 10);
    digit(first + 4, template_index + 1, hundredths / 10 % 10);
    digit(first + 3, template_index + 2, seconds % 10);
    digit(first + 2, template_index + 3, seconds / 10 % 10);
    digit(first + 1, template_index + 4, minutes % 10);
    digit(first, template_index + 5, minutes / 10 % 10);
    for (int i = 0; i < 6; i++)
        draw_hud(first + i);
}

/* The race overlays of `car`: start lights, the pit and replay panels, speed, damage, position, cars
 * left, points, lap, lap times and the total destruction clock. */
void Update_Race_Timers(int car)
{
    const RaceRecord *rr = &race_records[car];
    Update_Race_CountDown();
    if (pit_camera_active == 1)
        pit_clock();
    if (replay_playback != 0) {
        reset_panel(&replay_panel[buffer_num], &replay_panel_mode[buffer_num]);
        draw_hud(HUD_REPLAY_BUTTON_1);
        draw_hud(HUD_REPLAY_SLOW_MOTION);
        draw_hud(HUD_REPLAY_BUTTON_2);
        draw_hud(HUD_REPLAY_EXIT);
        if (blink_on())
            draw_hud(HUD_REPLAY_R);
        draw_panel(replay_panel, replay_panel_mode);
        return;
    }
    if (demo_mode != 0) {
        if (blink_on())
            draw_hud(HUD_DEMO);
        return;
    }
    draw_hud(HUD_SPEED);
    draw_hud(HUD_DAMAGE_TOP);
    draw_hud(HUD_DAMAGE_MIDDLE);
    draw_hud(HUD_DAMAGE_BOTTOM);
    draw_hud(HUD_CAR);
    if (pit_camera_active == 1) {
        reset_panel(&pit_panel[buffer_num], &pit_panel_mode[buffer_num]);
        draw_hud(HUD_EXIT_PIT);
        draw_hud(HUD_REPAIR);
        draw_hud(HUD_SIDE_ONLY);
        draw_hud(HUD_SPIN_LEFT);
        if (pad.type == PAD_DIGITAL) {
            draw_hud(HUD_PAD_X);
            draw_hud(HUD_PAD_SQUARE);
        }
        if (pad.type == PAD_ANALOG) {
            draw_hud(HUD_PAD_MAD_I);
            draw_hud(HUD_PAD_MAD_II);
        }
        if (pad.type == PAD_NEGCON) {
            draw_hud(HUD_PAD_NEG_I);
            draw_hud(HUD_PAD_NEG_II);
        }
        draw_panel(pit_panel, pit_panel_mode);
        if (pit_beep_wait != 0)
            pit_beep_wait--;
        big_digit(HUD_POSITION_BIG, 12, pit_countdown);
        draw_hud(HUD_POSITION_BIG);
    }
    if ((race_mode == MODE_WRECKING_RACING && race_type != RACE_TIME_TRIAL) || race_mode == MODE_DESTRUCTION_DERBY) {
        if (race_mode != MODE_DESTRUCTION_DERBY) {
            big_digit(HUD_POSITION_1, 28, rr->position / 10);
            big_digit(HUD_POSITION_2, 29, rr->position % 10);
            draw_hud(HUD_POSITION_1);
            draw_hud(HUD_POSITION_2);
        }
        digit(HUD_CARS_LEFT_1, 36, active_cars / 10);
        digit(HUD_CARS_LEFT_2, 37, active_cars % 10);
        draw_hud(HUD_CARS_LEFT_1);
        draw_hud(HUD_CARS_LEFT_2);
        if (race_type != RACE_TOTAL_DESTRUCTION) {
            digit(HUD_POINTS_DIGIT_3, 38, rr->points % 10);
            digit(HUD_POINTS_DIGIT_2, 39, rr->points / 10 % 10);
            digit(HUD_POINTS_DIGIT_1, 40, rr->points / 100 % 10);
            draw_hud(HUD_POINTS_DIGIT_1);
            draw_hud(HUD_POINTS_DIGIT_2);
            draw_hud(HUD_POINTS_DIGIT_3);
            draw_hud(HUD_RACE_POINTS);
        }
        draw_hud(HUD_STILL_RUNNING);
    }
    if (race_mode == MODE_STOCK_CAR && race_type != RACE_TIME_TRIAL) {
        big_digit(HUD_POSITION_1, 30, rr->position / 10);
        big_digit(HUD_POSITION_2, 31, rr->position % 10);
        draw_hud(HUD_POSITION_1);
        draw_hud(HUD_POSITION_2);
    }
    digit(HUD_SPEED_DIGIT_3, 32, rr->speed % 10);
    digit(HUD_SPEED_DIGIT_2, 33, rr->speed / 10 % 10);
    digit(HUD_SPEED_DIGIT_1, 34, rr->speed / 100 % 10);
    draw_hud(HUD_SPEED_DIGIT_1);
    draw_hud(HUD_SPEED_DIGIT_2);
    draw_hud(HUD_SPEED_DIGIT_3);
    draw_hud(HUD_KPH);
    if (current_level < FIRST_BOWL_LEVEL && race_type != RACE_TIME_TRIAL) {
        draw_hud(HUD_LAP);
        int lap = rr->lap_counted;
        if (lap == 0)
            lap = 1;
        if (lap > track_info[current_level].laps)
            lap = track_info[current_level].laps;
        digit(HUD_LAP_1, 41, lap / 10);
        digit(HUD_LAP_2, 42, lap % 10);
        draw_hud(HUD_LAP_1);
        draw_hud(HUD_LAP_2);
        draw_hud(HUD_LAP_SLASH);
        draw_hud(HUD_LAPS_TENS);
        draw_hud(HUD_LAPS_UNITS);
    }
    if (race_type == RACE_TIME_TRIAL || race_mode == MODE_STOCK_CAR) {
        draw_hud(HUD_LAP_TIME);
        draw_hud(HUD_FAST_LAP);
        time_digits(HUD_TIME_DIGITS, 50, lap_minutes, lap_seconds, lap_fraction);
        if (lap_record_flash != 0)
            lap_record_flash--;
        if ((lap_record_flash & 4) == 0) {
            const TrackRecord *best = &track_records[current_level - 1];
            time_digits(HUD_FAST_DIGITS, 56, best->minutes, best->seconds, best->fraction);
        }
    }
    if (race_type == RACE_TOTAL_DESTRUCTION) {
        if (overlay_state < 0 && car_blocks[current_player_car].wrecked == 0) {
            if (tick_second(&derby_time[2]))
                derby_time[1]++;
            if (derby_time[1] > 59) {
                derby_time[0]++;
                derby_time[1] -= 60;
            }
            if (derby_time[0] > 98) {
                derby_time[0] = 99;
                derby_time[1] = 0;
                derby_time[2] = 0;
            }
        }
        /* a wrecked player's clock flashes */
        if (car_blocks[current_player_car].wrecked != 0 && ((frame_count / 8) & 1) == 0)
            return;
        int32_t hundredths = derby_time[2] * 100 >> 16;
        const int32_t value[6] = {derby_time[0] / 10 % 10, derby_time[0] % 10, derby_time[1] / 10 % 10,
                                  derby_time[1] % 10, hundredths / 10 % 10, hundredths % 10};
        for (int i = 0; i < 6; i++) {
            draw_hud(HUD_TOTAL_DIGITS + i);
            digit(HUD_TOTAL_DIGITS + i, 62 + i, value[i]);
        }
    }
}

/* "1st", "2nd", "3rd" over the leading three cars when they are on screen, in races on tracks. */
void Display_Position_Pointers(void)
{
    static const int32_t no_pointers[12] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1}; /* 0x8006F774: the bowls */
    if (overlay_state >= 1 || num_cars == 1 || current_level >= FIRST_BOWL_LEVEL ||
        no_pointers[current_level] == 1)
        return;
    for (int car = 0; car < MAX_CARS; car++) {
        int place = race_records[car].position;
        if (place < 1 || place > 3 || car == camera_car || car_states[car].drawn != 1)
            continue;
        const CarState *cs = &car_states[car];
        SVECTOR rel = {(int16_t)(cs->position[0] - world_matrix.t[0]), (int16_t)(cs->position[1] - world_matrix.t[1]),
                       (int16_t)(cs->position[2] - world_matrix.t[2]), 0};
        SetRotMatrix(&world_matrix);
        gte_ldtr(0, 0, 0);
        gte_ldv0(&rel);
        gte_rtps();
        int32_t sz;
        gte_stsz(&sz);
        if (sz < 0x101 || sz > 0x7fff)
            continue;
        int16_t x = (int16_t)gte.sx[2], y = (int16_t)gte.sy[2];
        POLY_FT4 *p = &hud_sprites[HUD_1ST + place - 1].q[buffer_num];
        setXY4(p, x - 0x10, y - 0x23, x + 0xf, y - 0x23, x - 0x10, y - 10, x + 0xf, y - 10);
        setRGB0(p, 0x40, 0x40, 0x40);
        addPrim(&cdb->ot[sz / 4], p);
    }
}
