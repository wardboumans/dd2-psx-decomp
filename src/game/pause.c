#include "game/pause.h"

#include "game/camera.h"
#include "game/car.h"
#include "game/frontend_init.h"
#include "game/main_init.h"
#include "game/overlays.h"
#include "game/play_game.h"
#include "game/sound_fx.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libetc.h"
#include "psx/libgpu.h"
#include "sound/cd.h"
#include "sound/sound.h"
#include "system/controller.h"

/* The pause menu's sprites (one quad per display buffer) and where they go (the templates at 0x80070E04;
 * Setup_Sprite_Quad fills in their texture, which the volume bars read back). */
enum {
    PAUSE_PAUSED,     /* 0x800A717C */
    PAUSE_CONTINUE,   /* 0x800A6710 */
    PAUSE_MUSIC,      /* 0x800A711C */
    PAUSE_SFX,        /* 0x800907A4 */
    PAUSE_RETIRE,     /* 0x800A6968 */
    PAUSE_MUSIC_BAR,  /* 0x8009EBE8 */
    PAUSE_SFX_BAR,    /* 0x800892A0 */
    PAUSE_RETIRE_SURE, /* 0x8009EC38 */
    PAUSE_YES,        /* 0x80090C24 */
    PAUSE_NO,         /* 0x80088D50 */
    PAUSE_QUIT,       /* 0x80089434 */
    PAUSE_QUIT_SURE,  /* 0x800A6780 */
    PAUSE_PAD_X,      /* 0x8007FC58 */
    PAUSE_PAD_NEG_I,  /* 0x8007FCEC */
    PAUSE_PAD_MAD_I,  /* 0x8008853C */
    PAUSE_SELECT,     /* 0x8008009C */
    PAUSE_4WAY,       /* 0x8009E9DC */
    PAUSE_MOVE,       /* 0x8009EB8C */
    PAUSE_SPRITES
};

static POLY_FT4 pause_quads[PAUSE_SPRITES][2];
static Sprite pause_templates[PAUSE_SPRITES] = {
    [PAUSE_PAUSED] = {.x = 120, .y = 110, .raw = 1},
    [PAUSE_CONTINUE] = {.x = 124, .y = 127, .raw = 1},
    [PAUSE_MUSIC] = {.x = 124, .y = 145, .raw = 1},
    [PAUSE_SFX] = {.x = 128, .y = 170, .raw = 1},
    [PAUSE_RETIRE] = {.x = 132, .y = 193, .raw = 1},
    [PAUSE_MUSIC_BAR] = {.x = 124, .y = 158, .raw = 1},
    [PAUSE_SFX_BAR] = {.x = 124, .y = 183, .raw = 1},
    [PAUSE_RETIRE_SURE] = {.x = 112, .y = 140, .raw = 1},
    [PAUSE_YES] = {.x = 144, .y = 160, .raw = 1},
    [PAUSE_NO] = {.x = 143, .y = 177, .raw = 1},
    [PAUSE_QUIT] = {.x = 144, .y = 210, .raw = 1},
    [PAUSE_QUIT_SURE] = {.x = 126, .y = 140, .raw = 1},
    [PAUSE_PAD_X] = {.x = 63, .y = 155, .semi_trans = 1, .raw = 1},
    [PAUSE_PAD_NEG_I] = {.x = 63, .y = 155, .semi_trans = 1, .raw = 1},
    [PAUSE_PAD_MAD_I] = {.x = 63, .y = 155, .semi_trans = 1, .raw = 1},
    [PAUSE_SELECT] = {.x = 58, .y = 179, .semi_trans = 1, .raw = 1},
    [PAUSE_4WAY] = {.x = 30, .y = 152, .raw = 1},
    [PAUSE_MOVE] = {.x = 34, .y = 179, .semi_trans = 1, .raw = 1},
};
static const char *const pause_names[PAUSE_SPRITES] = {
    "PAUSED", "CONTINUE", "MUSICVOL", "SFXVOL", "RETIRE", "NOTCHES", "NOTCHES", "RSURE", "YES",
    "NO", "QUIT", "SURE", "X", "NEG_I", "MAD_I", "TSELECT", "4WAY", "TMOVE",
};

/* The dark boxes behind the menu and behind the pad help, with a quad that sets their blending. */
static POLY_F4 menu_box[2];      /* 0x8007FD3C */
static POLY_FT4 menu_box_mode[2]; /* 0x800885B8 */
static POLY_F4 help_box[2];      /* 0x800A6F30 */
static POLY_FT4 help_box_mode[2]; /* 0x8007F730 */

static int32_t retire_asked;     /* 0x80079A60 */
static int32_t music_notches;    /* 0x80079C3C: the volume bars' lengths, 2 .. 72 in steps of 7 */
static int32_t sfx_notches;      /* 0x80079B40 */

/* Nobody in the pit lane. */
void Init_Pit_State(void)
{
    pit_in = 0;
    pit_camera_active = 0;
    pit_camera_done = 0;
    pit_second = 0;
    pit_countdown = 0;
    pit_camera_state = 0;
    pit_camera_busy = 0;
}

static void setup_box(POLY_F4 *box, POLY_FT4 *mode, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    setPolyF4(box);
    setSemiTrans(box, 1);
    setRGB0(box, 0, 0, 0);
    setXY4(box, x0, y0, x1, y0, x0, y1, x1, y1);
    setPolyFT4(mode);
    setXY4(mode, 0, 0, 0, 0, 0, 0, 0, 0);
    mode->tpage = GetTPage(0, 0, 0, 0);
    setSemiTrans(mode, 1);
}

/* The pause menu's volume bars: 7 pixels a notch after a 2-pixel start. */
#define BAR_NOTCH 7
#define BAR_START 2
#define BAR_FULL (VOLUME_NOTCHES * BAR_NOTCH + BAR_START)

static int32_t notches(int32_t volume)
{
    return volume / VOLUME_STEP * BAR_NOTCH + BAR_START;
}

void Initialise_Pause_Mode(void)
{
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < PAUSE_SPRITES; i++)
            Setup_Sprite_Quad(&pause_quads[i][b], pause_names[i], &pause_templates[i]);
        setup_box(&menu_box[b], &menu_box_mode[b], 0x72, 0x68, 0xce, 0xe8);
        setup_box(&help_box[b], &help_box_mode[b], 0x16, 0x94, 0x56, 0xbc);
    }
    yes_quit = 0;
    retire_asked = 0;
    music_notches = notches(music_volume);
    sfx_notches = notches(sound_volume);
}

/* A volume bar: the first `length` texels of the notches sprite. */
static void volume_bar(int sprite, int16_t y, int32_t length)
{
    const Sprite *t = &pause_templates[sprite];
    POLY_FT4 *p = &pause_quads[sprite][buffer_num];
    int16_t x1 = (int16_t)(0x7c + length);
    setXY4(p, 0x7c, y, x1, y, 0x7c, y + 7, x1, y + 7);
    uint8_t u1 = (uint8_t)(t->u + length), v1 = (uint8_t)(t->v + 7);
    setUV4(p, t->u, t->v, u1, t->v, t->u, v1, u1, v1);
}

static void menu_sound(int sample)
{
    Play_Sound(ANY_CHANNEL, sample_bank, sample, SOUND_VOLUME_MAX, PITCH_HALF, 0);
}

/* Adds the listed sprites to the OT entry, the first only on the blink's lit frames. */
static void show(OT_TAG *ot, int blink_on, int first, int a, int b, int c, int d)
{
    const int rest[4] = {a, b, c, d};
    if (blink_on)
        addPrim(ot, &pause_quads[first][buffer_num]);
    for (int i = 0; i < 4 && rest[i] >= 0; i++)
        addPrim(ot, &pause_quads[rest[i]][buffer_num]);
}

/* The pause menu, run in its own loop over the frozen picture: continue, music and effects volume,
 * retire (the player's car is wrecked) or quit, each of the last two asking for a yes. Returns
 * PAUSE_END_RACE to end the race, 0 to carry on. */
int Pause_Mode(void)
{
    int item = 0, blink = 0, retire_yes = 0, quit_yes = 0;
    if (frame_count < 2)
        return 0;
    Sound_Pause();
    CD_Pause();
    music_notches = notches(music_volume);
    sfx_notches = notches(sound_volume);
    /* the frozen picture into the buffer drawn next, keeping the panels' backgrounds to restore */
    DrawSync(0);
    RECT whole = {0, db[buffer_num].disp.disp.y, SCREEN_W, SCREEN_H};
    MoveImage(&whole, 0, db[buffer_num].draw.clip.y);
    DrawSync(0);
    RECT menu_back = {0x72, (int16_t)(db[buffer_num].draw.clip.y + 0x68), 0x5c, 0x80};
    RECT help_back = {0x16, (int16_t)(db[buffer_num].draw.clip.y + 0x94), 0x40, 0x28};
    DrawSync(0);
    OT_TAG *ot = db[buffer_num ^ 1].ot;
    int result;
    for (;;) {
        ClearOTagR(ot, otsize);
        Poll_Pad_Buttons();
        if (pad.type != current_pad_type) {
            pad.held = 0;
            pad.pressed = 0;
        }
        if (retire_asked == 1) {
            if ((pad.pressed & PAD_UP) && !retire_yes) {
                retire_yes = 1;
                menu_sound(SAMPLE_MENU_SELECT);
            }
            if ((pad.pressed & PAD_DOWN) && retire_yes) {
                retire_yes = 0;
                menu_sound(SAMPLE_MENU_SELECT);
            }
            if (pad.pressed & PAD_CROSS) {
                yes_quit = 0;
                if (retire_yes) {
                    car_blocks[current_player_car].wrecked = 1;
                    result = PAUSE_END_RACE;
                } else {
                    result = 0;
                }
                break;
            }
        }
        if (yes_quit == 1) {
            if ((pad.pressed & PAD_UP) && !quit_yes) {
                quit_yes = 1;
                menu_sound(SAMPLE_MENU_SELECT);
            }
            if ((pad.pressed & PAD_DOWN) && quit_yes) {
                quit_yes = 0;
                menu_sound(SAMPLE_MENU_SELECT);
            }
            if (pad.pressed & PAD_CROSS) {
                if (quit_yes) {
                    result = PAUSE_END_RACE;
                    yes_quit = 1;
                } else {
                    yes_quit = 0;
                    result = 0;
                }
                break;
            }
        }
        if (retire_asked == 0 && yes_quit == 0) {
            if ((pad.pressed & PAD_UP) && item > 0) {
                item--;
                menu_sound(SAMPLE_MENU_SELECT);
            }
            if ((pad.pressed & PAD_DOWN) && item < 4) {
                item++;
                menu_sound(SAMPLE_MENU_SELECT);
            }
            if (pad.pressed & PAD_CROSS) {
                menu_sound(SAMPLE_MENU_CONFIRM);
                if (item == 3)
                    retire_asked = 1;
                if (item == 0) {
                    result = 0;
                    break;
                }
                if (item == 4)
                    yes_quit = 1;
            }
        }
        blink = (blink + 1) & 7;
        int lit = blink < 4;
        if (pad.type == current_pad_type) {
            addPrim(&ot[1], &pause_quads[PAUSE_4WAY][buffer_num]);
            addPrim(&ot[1], &pause_quads[PAUSE_MOVE][buffer_num]);
            addPrim(&ot[1], &pause_quads[PAUSE_SELECT][buffer_num]);
            if (pad.type == PAD_DIGITAL)
                addPrim(&ot[1], &pause_quads[PAUSE_PAD_X][buffer_num]);
            if (pad.type == PAD_NEGCON)
                addPrim(&ot[1], &pause_quads[PAUSE_PAD_NEG_I][buffer_num]);
            if (pad.type == PAD_ANALOG)
                addPrim(&ot[1], &pause_quads[PAUSE_PAD_MAD_I][buffer_num]);
            addPrim(&ot[2], &help_box[buffer_num]);
            addPrim(&ot[2], &help_box_mode[buffer_num]);
        }
        switch (item) {
        case 0:
            retire_asked = 0;
            show(&ot[1], lit, PAUSE_CONTINUE, PAUSE_MUSIC, PAUSE_SFX, PAUSE_RETIRE, PAUSE_QUIT);
            break;
        case 1:
            if ((pad.pressed & PAD_LEFT) && music_notches > BAR_START) {
                music_notches -= BAR_NOTCH;
                menu_sound(SAMPLE_MENU_CLICK);
            }
            if ((pad.pressed & PAD_RIGHT) && music_notches < BAR_FULL) {
                music_notches += BAR_NOTCH;
                menu_sound(SAMPLE_MENU_CLICK);
            }
            show(&ot[1], lit, PAUSE_MUSIC, PAUSE_SFX, PAUSE_CONTINUE, PAUSE_RETIRE, PAUSE_QUIT);
            break;
        case 2:
            if ((pad.pressed & PAD_LEFT) && sfx_notches > BAR_START) {
                sfx_notches -= BAR_NOTCH;
                menu_sound(SAMPLE_MENU_CLICK);
            }
            if ((pad.pressed & PAD_RIGHT) && sfx_notches < BAR_FULL) {
                sfx_notches += BAR_NOTCH;
                menu_sound(SAMPLE_MENU_CLICK);
            }
            show(&ot[1], lit, PAUSE_SFX, PAUSE_MUSIC, PAUSE_CONTINUE, PAUSE_RETIRE, PAUSE_QUIT);
            break;
        case 3:
            if (retire_asked == 1)
                show(&ot[1], lit, retire_yes ? PAUSE_YES : PAUSE_NO, PAUSE_RETIRE_SURE,
                     retire_yes ? PAUSE_NO : PAUSE_YES, -1, -1);
            else
                show(&ot[1], lit, PAUSE_RETIRE, PAUSE_MUSIC, PAUSE_SFX, PAUSE_CONTINUE, PAUSE_QUIT);
            break;
        case 4:
            if (yes_quit == 1)
                show(&ot[1], lit, quit_yes ? PAUSE_YES : PAUSE_NO, PAUSE_QUIT_SURE,
                     quit_yes ? PAUSE_NO : PAUSE_YES, -1, -1);
            else
                show(&ot[1], lit, PAUSE_QUIT, PAUSE_MUSIC, PAUSE_SFX, PAUSE_CONTINUE, PAUSE_RETIRE);
            break;
        }
        if (retire_asked == 0 && yes_quit == 0) {
            volume_bar(PAUSE_MUSIC_BAR, 0x9e, music_notches);
            addPrim(&ot[1], &pause_quads[PAUSE_MUSIC_BAR][buffer_num]);
            volume_bar(PAUSE_SFX_BAR, 0xb7, sfx_notches);
            addPrim(&ot[1], &pause_quads[PAUSE_SFX_BAR][buffer_num]);
            addPrim(&ot[1], &pause_quads[PAUSE_PAUSED][buffer_num]);
        }
        addPrim(&ot[2], &menu_box[buffer_num]);
        addPrim(&ot[2], &menu_box_mode[buffer_num]);
        VSync(0);
        PutDrawEnv(&db[buffer_num ^ 1].draw);
        PutDispEnv(&db[buffer_num].disp);
        DrawSync(0);
        MoveImage(&help_back, 0x16, db[buffer_num].disp.disp.y + 0x94);
        MoveImage(&menu_back, 0x72, db[buffer_num].disp.disp.y + 0x68);
        DrawOTag(&ot[otsize - 1]);
    }
    music_volume = (music_notches - BAR_START) / BAR_NOTCH * VOLUME_STEP;
    sound_volume = (sfx_notches - BAR_START) / BAR_NOTCH * VOLUME_STEP;
    Set_CD_Volume((short)music_volume);
    set_master_volume(sound_volume);
    if (yes_quit == 1)
        return result;
    CD_Restart();
    Update_Race_Sounds();
    return result;
}
