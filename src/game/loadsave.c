/* The memory card screen: the two cards' blocks as grids of icons, loading, saving under a typed name,
 * deleting, and the save files' contents (GENERAL.BIN). */
#include "game/loadsave.h"

#include "game/frontend.h"
#include "game/frontend_init.h"
#include "game/league.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/replay.h"
#include "game/stats.h"
#include "graphics/font.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"
#include "sound/cd.h"
#include "sound/sound.h"
#include "system/card.h"
#include "system/controller.h"
#include "system/debug.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define NAME_LETTERS 8           /* of a save file's label */
#define REPLAY_SCRIPT_PSX 0x8009581Cu /* replay_script's address, as replay saves store its end */

static uint8_t save_image[SAVE_IMAGE_SIZE];     /* 0x801EED68 */
static uint8_t season_snapshot[SAVE_IMAGE_SIZE]; /* 0x801F0D68 */

enum {
    LS_MINICARD1, LS_MINICARD2, LS_PAD2, LS_TCANCEL, LS_PAD1, LS_TSELECT, LS_PAD4, LS_TOGGLE, LS_IMEMCARD,
    LS_LOAD, LS_DELETE, LS_RINGS, LS_4WAY, LS_TMOVE, LS_CARD_ERR, LS_CARD_YN, LS_SMALRING, LS_ERROR_YN,
    LOADSAVE_SPRITES
};

static Sprite loadsave_sprites[LOADSAVE_SPRITES] = { /* 0x801EC53C */
    [LS_MINICARD1] = BUTTON_SPRITE(44, 70),
    [LS_MINICARD2] = BUTTON_SPRITE(174, 70),
    [LS_PAD2] = BUTTON_SPRITE(260, 24),
    [LS_TCANCEL] = BUTTON_SPRITE(278, 29),
    [LS_PAD1] = BUTTON_SPRITE(260, 41),
    [LS_TSELECT] = BUTTON_SPRITE(278, 46),
    [LS_PAD4] = BUTTON_SPRITE(260, 83),
    [LS_TOGGLE] = BUTTON_SPRITE(278, 85),
    [LS_IMEMCARD] = BUTTON_SPRITE(16, 24),
    [LS_LOAD] = BUTTON_SPRITE(214, 192),
    [LS_DELETE] = BUTTON_SPRITE(262, 192),
    [LS_RINGS] = TRANS_SPRITE(214, 190),
    [LS_4WAY] = BUTTON_SPRITE(256, 58),
    [LS_TMOVE] = BUTTON_SPRITE(282, 67),
    [LS_CARD_ERR] = BUTTON_SPRITE(96, 96),
    [LS_CARD_YN] = BUTTON_SPRITE(102, 101),
    [LS_SMALRING] = TRANS_SPRITE(163, 130),
    [LS_ERROR_YN] = BUTTON_SPRITE(102, 101),
};

/* The name entry's sprites. */
enum { NE_CARDTEXT, NE_BACKSPCE, NE_TICK2, NE_CARDCURS, NAME_ENTRY_SPRITES };

static Sprite name_entry_sprites[NAME_ENTRY_SPRITES] = { /* 0x801ECA24 */
    [NE_CARDTEXT] = BUTTON_SPRITE(108, 111),
    [NE_BACKSPCE] = BUTTON_SPRITE(134, 130),
    [NE_TICK2] = BUTTON_SPRITE(163, 130),
    [NE_CARDCURS] = TRANS_SPRITE(108, 110),
};

static ScreenPoly loadsave_polys[] = { /* 0x801EC728 */
    {3, "MEMLOAD", SCREEN_GREY, &loadsave_sprites[LS_LOAD]},
    {3, "MEMDEL", SCREEN_GREY, &loadsave_sprites[LS_DELETE]},
    {3, "MINICARD", SCREEN_GREY_TRANS, &loadsave_sprites[LS_MINICARD1]},
    {3, "MINICARD", SCREEN_GREY_TRANS, &loadsave_sprites[LS_MINICARD2]},
    {3, "IMEMCARD", SCREEN_GREY, &loadsave_sprites[LS_IMEMCARD]},
    {3, "ZZZZPAD2", SCREEN_GREY_TRANS, &loadsave_sprites[LS_PAD2]},
    {3, "TCANCEL", SCREEN_GREY_TRANS, &loadsave_sprites[LS_TCANCEL]},
    {3, "ZZZZPAD1", SCREEN_GREY_TRANS, &loadsave_sprites[LS_PAD1]},
    {3, "TSELECT", SCREEN_GREY_TRANS, &loadsave_sprites[LS_TSELECT]},
    {3, "ZZZZPAD4", SCREEN_GREY_TRANS, &loadsave_sprites[LS_PAD4]},
    {3, "TOGGLE", SCREEN_GREY_TRANS, &loadsave_sprites[LS_TOGGLE]},
    {3, "4WAY", SCREEN_GREY_TRANS, &loadsave_sprites[LS_4WAY]},
    {3, "TMOVE", SCREEN_GREY_TRANS, &loadsave_sprites[LS_TMOVE]},
    {2, "RINGS", SCREEN_HIGHLIGHT, &loadsave_sprites[LS_RINGS]},
    {0, NULL, 0, NULL},
};

/* The two buttons: Load (or Save) and Delete, plain and pressed. */
static const char *const button_sprites[3] = {"MEMLOAD", "MEMSAVE", "MEMDEL"};         /* 0x801EC96C */
static const char *const pressed_sprites[3] = {"MEMLOADP", "MEMSAVEP", "MEMDELP"};    /* 0x801EC978 */

/* Lines of loadsave_text. */
enum {
    LT_TITLE = 0,     /* and its shadow */
    LT_CAPTION = 2,   /* the button's caption, then "Select ..." */
    LT_FILE = 4,      /* the chosen block's file */
    LT_STATUS = 6,    /* two lines per card: no card, unformatted, full */
    LT_MESSAGE = 14,  /* errors, questions, "Please Wait", the name being typed */
    LT_DETAIL = 15,
};

static ScreenText loadsave_text[] = { /* 0x801EC818 */
    {3, 0, TITLE_INK, -104, -96, SCREEN_Z, ""},
    {4, 3, {0, 0, 0, 0}, -101, -93, SCREEN_Z, ""},
    {3, 1, GREY_INK, -120, 91, SCREEN_Z, ""},
    {4, 4, GREY_INK, -117, 94, SCREEN_Z, ""},
    {3, 1, GREY_INK, -120, 70, SCREEN_Z, ""},
    {4, 4, GREY_INK, -117, 73, SCREEN_Z, ""},
    {3, 2, {0, 0, 0, 0}, -65, -17, SCREEN_Z, ""},
    {3, 2, {0, 0, 0, 0}, -65, -9, SCREEN_Z, ""},
    {3, 2, {0, 0, 0, 0}, 65, -17, SCREEN_Z, ""},
    {3, 2, {0, 0, 0, 0}, 65, -9, SCREEN_Z, ""},
    {3, 1, GREY_INK, -112, -38, SCREEN_Z, "%R%JL%T/1"},
    {4, 4, GREY_INK, -109, -35, SCREEN_Z, "%R%JL%T/1"},
    {3, 1, GREY_INK, 15, -38, SCREEN_Z, "%R%JL%T/2"},
    {4, 4, GREY_INK, 18, -35, SCREEN_Z, "%R%JL%T/2"},
    {1, 2, {0, 0, 0, 0}, 0, -13, SCREEN_Z, ""},
    {1, 2, {0, 0, 0, 0}, 0, 7, SCREEN_Z, ""},
    {0, 0, {0, 0, 0, 0}, 0, 0, 0, NULL},
};

static ScreenLine loadsave_lines[] = { /* 0x801EC704: the title's underline fits the title */
    {1, 54, 52, 54, 0xc8, 0xff, 0xff, 0},
    {2, 57, 55, 54, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0, 0},
};

/* Per display buffer and card: a block's icon, the cell behind it, and the box behind a card's status. */
static POLY_FT4 block_icons[2][2][CARD_BLOCKS]; /* 0x801EDE5C */
static POLY_F4 block_cells[2][2][CARD_BLOCKS];  /* 0x801EE7BC */
static POLY_F4 status_boxes[2][2];              /* 0x801EDC8C */
static int32_t grid_glow;                       /* 0x801EDAF4 */

static int32_t card_unusable[2]; /* 0x801EED5C: no card, unformatted or full; its status gets a box */
static int32_t loadsave_mode;    /* 0x801EED64 */
static char file_text[0x80];     /* 0x801EDCEC */

static void Init_LoadSave_Prim_Codes(void) /* 0x801E22F0 */
{
    for (int b = 0; b < 2; b++) {
        for (int slot = 0; slot < 2; slot++) {
            for (int i = 0; i < CARD_BLOCKS; i++) {
                setPolyFT4(&block_icons[b][slot][i]);
                setSemiTrans(&block_icons[b][slot][i], 0);
                setPolyF4(&block_cells[b][slot][i]);
                setSemiTrans(&block_cells[b][slot][i], 0);
            }
        }
    }
}

/* Both cards' blocks, 3 by 5: the chosen file's blocks red with their icons glowing. */
static void Draw_LoadSave_Slot_Grid(void) /* 0x801E2950 */
{
    uint8_t glow = (uint8_t)(grid_glow + grid_glow / 0xff);
    int16_t top = (int16_t)(pal_mode << 3);
    grid_glow += 0x14;
    for (int slot = 0; slot < 2; slot++) {
        const CardSlot *card = &card_slots[slot];
        int16_t left = slot == 0 ? 0x42 : 0xc4;
        if (card_unusable[slot]) {
            POLY_F4 *box = &status_boxes[buffer_num][slot];
            int16_t x0 = (int16_t)(slot * 0x82 + 0x3c), x1 = (int16_t)(slot * 0x82 + 0x82);
            setPolyF4(box);
            setSemiTrans(box, 1);
            setXY4(box, x0, top + 100, x1, top + 100, x0, top + 0x78, x1, top + 0x78);
            setRGB0(box, 0xff, 0xff, 0xff);
            addPrim(&cdb->ot[4], box);
        }
        int chosen = card->cursor == -1 ? -1 : card->entries[card->cursor].index;
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                int i = row * 3 + col;
                const CardEntry *e = &card->entries[i];
                POLY_FT4 *icon = &block_icons[buffer_num][slot][i];
                POLY_F4 *cell = &block_cells[buffer_num][slot][i];
                int16_t x = (int16_t)(left + col * 0x14), y = (int16_t)(top + 0x3e + row * 0x14);
                setXY4(icon, x, y, x + 0x10, y, x, y + 0x10, x + 0x10, y + 0x10);
                setXY4(cell, x, y, x + 0x10, y, x, y + 0x10, x + 0x10, y + 0x10);
                if (e->index == chosen) {
                    setRGB0(cell, 0x80, 0, 0);
                    if (e->status != CARD_EMPTY) {
                        for (int b = 0; b < 2; b++)
                            setRGB0(&block_icons[b][slot][i], glow, glow, glow);
                    }
                } else {
                    setRGB0(cell, 0x27, 200, 0xde);
                    setRGB0(icon, 0x80, 0x80, 0x80);
                }
                if (card->present && card->icons_hidden == 0 && e->status != CARD_EMPTY) {
                    uint8_t u = (uint8_t)(i * 0x10), v = (uint8_t)(slot * 0x10);
                    setUV4(icon, u, v, u + 0xf, v, u, v + 0xf, u + 0xf, v + 0xf);
                    icon->tpage = GetTPage(0, 0, CARD_ICON_X, CARD_ICON_Y);
                    icon->clut = GetClut(CARD_ICON_X, slot * CARD_BLOCKS + i + CARD_CLUT_Y);
                    addPrim(&cdb->ot[4], icon);
                }
                addPrim(&cdb->ot[5], cell);
            }
        }
    }
}

/* Every loop of the screen ends with this. */
static void draw_loadsave_screen(void)
{
    Draw_LoadSave_Slot_Grid();
    Draw_Slab();
    Print_Draw();
    Draw_Screen_Polys();
    Draw_Screen_Lines();
    Draw_All(1);
}

/* The status lines under each card; LoadSave also marks the unusable ones (Select_Card_File does not). */
static void set_card_status_text(int mark)
{
    for (int slot = 0; slot < 2; slot++) {
        const char *line1 = "", *line2 = "";
        int unusable = 1;
        if (!card_slots[slot].present) {
            line1 = "%R%JC%T/No Memory";
            line2 = "%R%JC%T/card";
        } else if (!card_slots[slot].formatted) {
            line1 = "%R%JC%T/Unformatted";
            line2 = "%R%JC%T/Memory card";
        } else if (Card_Is_Full(slot) == 1) {
            line1 = "%R%JC%T/Memory card";
            line2 = "%R%JC%T/Full";
        } else {
            unusable = 0;
        }
        loadsave_text[LT_STATUS + slot * 2].text = line1;
        loadsave_text[LT_STATUS + slot * 2 + 1].text = line2;
        if (mark)
            card_unusable[slot] = unusable;
    }
}

/* A card to choose blocks on, `preferred` if it is usable, else the other: -1 when neither is. Loading
 * needs a formatted card, saving any card; full cards stay unusable. */
static int Find_Usable_Card(int preferred) /* 0x801E6DC8 */
{
    int present0 = card_slots[0].present, present1 = card_slots[1].present;
    int usable0 = card_slots[0].formatted, usable1 = card_slots[1].formatted;
    if (!present0) {
        card_unusable[0] = 1;
        card_slots[0].cursor = -1;
    } else if (usable0 && Card_Is_Full(0) == 0) {
        card_unusable[0] = 0;
    }
    if (!present1) {
        card_unusable[1] = 1;
        card_slots[1].cursor = -1;
    } else if (usable1 && Card_Is_Full(1) == 0) {
        card_unusable[1] = 0;
    }
    if (loadsave_mode != LOADSAVE_LOAD) {
        if (present0)
            usable0 = 1;
        if (present1)
            usable1 = 1;
    }
    if (present0 || present1) {
        if (preferred == 0) {
            if (present0 && usable0)
                return 0;
            card_slots[0].cursor = -1;
            card_slots[1].cursor = 0;
            if (present1 && usable1)
                return 1;
        } else if (preferred == 1) {
            if (present1 && usable1)
                return 1;
            card_slots[0].cursor = 0;
            card_slots[1].cursor = -1;
            if (present0 && usable0)
                return 0;
        }
    }
    loadsave_sprites[LS_PAD4].x = loadsave_sprites[LS_PAD4].y = OFF_SCREEN;
    loadsave_sprites[LS_TOGGLE].x = loadsave_sprites[LS_TOGGLE].y = OFF_SCREEN;
    return -1;
}

/* PORT: the original reads the halfword before the slot's table for block -1 (a card that went away
 * or is unformatted while the screen waits for a choice); it is taken as an empty block here. */
static int block_status(int slot, int block)
{
    return block < 0 ? CARD_EMPTY : card_slots[slot].entries[block].status;
}

static void Set_Card_Slot_Text(int slot, int block) /* 0x801E59B4 */
{
    const CardEntry *e = &card_slots[slot].entries[block];
    if (e->status == CARD_OURS)
        snprintf(file_text, sizeof(file_text), "%%R%%JL%%T/File: DD2 - %s", e->label);
    else if (e->status == CARD_EMPTY)
        snprintf(file_text, sizeof(file_text), "%%R%%JL%%T/File: EMPTY");
    else if (e->status == CARD_OTHER)
        snprintf(file_text, sizeof(file_text), "%%R%%JL%%T/File: USED");
    loadsave_text[LT_FILE].text = file_text;
    loadsave_text[LT_FILE + 1].text = file_text;
}

static void Set_LoadSave_Menu_Texts(int mode, int button) /* 0x801E5770 */
{
    const char *title = NULL, *caption = NULL;
    int16_t underline = 0x3c;
    switch (mode) {
    case LOADSAVE_LOAD:
        title = "%R%JL%T/Load";
        caption = "%R%JL%T/Load File";
        underline = 0x36;
        break;
    case LOADSAVE_CONFIG:
        title = "%R%JL%T/Save";
        caption = "%R%JL%T/Save Configuration";
        break;
    case LOADSAVE_GAME:
        title = "%R%JL%T/Save";
        caption = "%R%JL%T/Save Game";
        break;
    case LOADSAVE_REPLAY:
        title = "%R%JL%T/Save";
        caption = "%R%JL%T/Save Replay";
        break;
    default:
        break;
    }
    if (title != NULL) {
        loadsave_text[LT_TITLE].text = loadsave_text[LT_TITLE + 1].text = title;
        loadsave_text[LT_CAPTION].text = loadsave_text[LT_CAPTION + 1].text = caption;
        loadsave_lines[0].length = loadsave_lines[1].length = underline;
    }
    if (button == 1)
        loadsave_text[LT_CAPTION].text = loadsave_text[LT_CAPTION + 1].text = "%R%JL%T/Delete File";
    loadsave_text[LT_FILE].text = loadsave_text[LT_FILE + 1].text = "";
}

static void Show_Card_Error(int error) /* 0x801E4948 */
{
    static const char *const messages[9] = {
        "%R%JC%T/Cannot format", "%R%JC%T/Cannot delete", "%R%JC%T/Cannot load", "%R%JC%T/Cannot save",
        "%R%JC%T/Memory card full", "%R%JC%T/No Memory card", "%R%JC%T/File already exists",
        "%R%JC%T/Not a DD2 file", "%R%JC%T/Memory card unformatted",
    };
    static POLY_FT4 panel, yes_no;
    Setup_Sprite_Quad(&panel, "CARD_ERR", &loadsave_sprites[LS_CARD_ERR]);
    Setup_Sprite_Quad(&yes_no, "CARD_YN", &loadsave_sprites[LS_ERROR_YN]);
    loadsave_text[LT_MESSAGE].x = -23;
    loadsave_text[LT_MESSAGE].text = "%R%JL%T/ERROR";
    loadsave_text[LT_DETAIL].text = messages[error];
    Setup_Screen_Text(loadsave_text);
    Play_Warning_FX();
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (pad.pressed & (PAD_CROSS | PAD_TRIANGLE))
            break;
        DrawSync(0);
        addPrim(&cdb->ot[2], &panel);
        addPrim(&cdb->ot[1], &yes_no);
        draw_loadsave_screen();
    }
    DrawSync(0);
    loadsave_text[LT_MESSAGE].x = 0;
    loadsave_text[LT_MESSAGE].text = "";
    loadsave_text[LT_DETAIL].text = "";
    Setup_Screen_Text(loadsave_text);
}

/* The message stays up for two frames, while the card is busy. */
static void Show_Card_Please_Wait(int what) /* 0x801E4CE0 */
{
    static const char *const doing[4] = {
        "%R%JC%T/Formatting", "%R%JC%T/Saving", "%R%JC%T/Loading", "%R%JC%T/Reading Memory cards",
    };
    static POLY_FT4 panels[2];
    Setup_Sprite_Quad(&panels[0], "CARD_ERR", &loadsave_sprites[LS_CARD_ERR]);
    Setup_Sprite_Quad(&panels[1], "CARD_ERR", &loadsave_sprites[LS_CARD_ERR]);
    loadsave_text[LT_DETAIL].text = "%R%JC%T/Please Wait..";
    loadsave_text[LT_MESSAGE].text = doing[what];
    Setup_Screen_Text(loadsave_text);
    for (int frame = 0;; frame++) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (frame == 2)
            break;
        addPrim(&cdb->ot[2], &panels[buffer_num]);
        draw_loadsave_screen();
    }
    DrawSync(0);
    loadsave_text[LT_MESSAGE].text = "";
    loadsave_text[LT_DETAIL].text = "";
    Setup_Screen_Text(loadsave_text);
}

/* Delete File? (0), Overwrite File? (1), Format? (2): the tick or the cross. Taking the card out is a no. */
static int Ask_Card_Yes_No(int question, int slot) /* 0x801E4FA8 */
{
    static const char *const questions[3] = {
        "%R%JL%T/Delete File?", "%R%JL%T/Overwrite File?", "%R%JL%T/Format?",
    };
    static POLY_FT4 panel, yes_no, tick, cross, ring;
    int16_t y = (int16_t)((pal_mode << 3) + 0x7a);
    Sprite tick_sprite = {.x = 0x86, .y = y, .raw = 1}; /* 0x801E02B0 */
    Sprite cross_sprite = {.x = 0xa3, .y = y, .raw = 1};
    Sprite *ring_sprite = &loadsave_sprites[LS_SMALRING];
    int answer = 0;
    ring_sprite->x = 0xa3;
    Setup_Sprite_Quad(&panel, "CARD_ERR", &loadsave_sprites[LS_CARD_ERR]);
    Setup_Sprite_Quad(&yes_no, "CARD_YN", &loadsave_sprites[LS_CARD_YN]);
    Setup_Sprite_Quad(&tick, "CARDTICK", &tick_sprite);
    Setup_Sprite_Quad(&cross, "CARDCROS", &cross_sprite);
    Setup_Sprite_Quad(&ring, "SMALRING", ring_sprite);
    loadsave_text[LT_MESSAGE].x = -23;
    loadsave_text[LT_MESSAGE].text = questions[question];
    loadsave_text[LT_DETAIL].text = "";
    Setup_Screen_Text(loadsave_text);
    Play_Warning_FX();
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        Check_Card_Status(slot);
        if (!card_slots[slot].present) {
            answer = 0;
            break;
        }
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            answer = 1;
            ring_sprite->x = 0x86;
            Setup_Sprite_Quad(&ring, "SMALRING", ring_sprite);
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            answer = 0;
            ring_sprite->x = 0xa3;
            Setup_Sprite_Quad(&ring, "SMALRING", ring_sprite);
        } else if (pad.pressed & PAD_CROSS) {
            Play_Select_FX();
            break;
        } else if (pad.pressed & PAD_TRIANGLE) {
            answer = 0;
            break;
        }
        DrawSync(0);
        addPrim(&cdb->ot[2], &panel);
        addPrim(&cdb->ot[1], &ring);
        addPrim(&cdb->ot[1], &yes_no);
        addPrim(&cdb->ot[1], &tick);
        addPrim(&cdb->ot[1], &cross);
        draw_loadsave_screen();
    }
    DrawSync(0);
    loadsave_text[LT_MESSAGE].x = 0;
    loadsave_text[LT_MESSAGE].text = "";
    loadsave_text[LT_DETAIL].text = "";
    Setup_Screen_Text(loadsave_text);
    return answer;
}

/* Typing a label: two rows of letters (A-M, N-Z), then backspace and the tick. Up to 8 letters; the
 * tick returns 1, Triangle or taking the card out 0. */
static int Enter_Card_File_Name(char *name, int slot) /* 0x801E6160 */
{
    static POLY_FT4 panel, letters, backspace, tick, ring, cursor; /* 0x801EDD6C */
    Sprite *s = name_entry_sprites;
    Sprite *ring_sprite = &loadsave_sprites[LS_SMALRING];
    char shown[24];
    int16_t top = (int16_t)(pal_mode << 3);
    int result = 0;
    int length = (int)strlen(name);
    int col = 0, row = 0, on_tick = 0;
    s[NE_CARDTEXT].y = top + 0x67;
    s[NE_BACKSPCE].y = top + 0x7a;
    s[NE_TICK2].y = top + 0x7a;
    s[NE_CARDCURS].y = top + 0x66;
    Setup_Sprite_Quad(&panel, "CARD_ERR", &loadsave_sprites[LS_CARD_ERR]);
    Setup_Sprite_Quad(&letters, "CARDTEXT", &s[NE_CARDTEXT]);
    Setup_Sprite_Quad(&backspace, "BACKSPCE", &s[NE_BACKSPCE]);
    Setup_Sprite_Quad(&tick, "TICK2", &s[NE_TICK2]);
    Setup_Sprite_Quad(&ring, "SMALRING", ring_sprite);
    Setup_Sprite_Quad(&cursor, "CARDCURS", &s[NE_CARDCURS]);
    setRGB0(&letters, 0, 0, 0);
    setShadeTex(&letters, 0);
    snprintf(shown, sizeof(shown), "%%R%%JC%%T/%s", name);
    loadsave_text[LT_MESSAGE].ink[0] = 0xff;
    loadsave_text[LT_MESSAGE].ink[1] = 0;
    loadsave_text[LT_MESSAGE].ink[2] = 0;
    loadsave_text[LT_MESSAGE].y = -26;
    loadsave_text[LT_MESSAGE].text = shown;
    loadsave_text[LT_DETAIL].text = "";
    Setup_Screen_Text(loadsave_text);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        Check_Card_Status(slot);
        if (!card_slots[slot].present) {
            result = 0;
            break;
        }
        if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (row == 2)
                on_tick = 0;
            else if (col > 0)
                col--;
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (row == 2)
                on_tick = 1;
            else if (col < 12)
                col++;
        }
        if (pad.pressed & PAD_UP) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (row > 0)
                row--;
        } else if (pad.pressed & PAD_DOWN) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (row < 2)
                row++;
            if (row == 2)
                on_tick = col >= 7;
        } else if (pad.pressed & PAD_CROSS) {
            Play_Select_FX();
            if (row < 2) {
                if (length < NAME_LETTERS) {
                    name[length] = (char)(row * 13 + 'A' + col);
                    name[length + 1] = '\0';
                    length++;
                }
            } else {
                if (on_tick) {
                    result = 1;
                    break;
                }
                if (length > 0)
                    name[--length] = '\0';
            }
            snprintf(shown, sizeof(shown), "%%R%%JC%%T/%s", name);
            Setup_Screen_Text(loadsave_text);
        } else if (pad.pressed & PAD_TRIANGLE) {
            result = 0;
            break;
        }
        if (row < 2) {
            s[NE_CARDCURS].x = (int16_t)(col * 8 + 0x6c);
            s[NE_CARDCURS].y = (int16_t)(row * 9 + top + 0x66);
            Setup_Sprite_Quad(&cursor, "CARDCURS", &s[NE_CARDCURS]);
        } else {
            ring_sprite->x = on_tick ? 0xa3 : 0x86;
            ring_sprite->y = top + 0x7a;
            Setup_Sprite_Quad(&ring, "SMALRING", ring_sprite);
        }
        DrawSync(0);
        addPrim(&cdb->ot[2], &panel);
        addPrim(&cdb->ot[1], row == 2 ? &ring : &cursor);
        addPrim(&cdb->ot[1], &letters);
        addPrim(&cdb->ot[1], &backspace);
        addPrim(&cdb->ot[1], &tick);
        draw_loadsave_screen();
    }
    DrawSync(0);
    loadsave_text[LT_MESSAGE].ink[0] = 0;
    loadsave_text[LT_MESSAGE].ink[1] = 0;
    loadsave_text[LT_MESSAGE].ink[2] = 0;
    loadsave_text[LT_MESSAGE].y = -13;
    loadsave_text[LT_MESSAGE].text = "";
    loadsave_text[LT_DETAIL].text = "";
    Setup_Screen_Text(loadsave_text);
    return result;
}

/* Choosing a block: the D-pad moves on the card's grid, Circle swaps cards. `what`: 0 load, 1 save,
 * 2 delete. Returns the card (its cursor on the file's first block), -1 for Triangle, and 0 when the
 * cards went away. */
static int Select_Card_File(int what) /* 0x801E1588 */
{
    static const char *const prompts[3] = {
        "%R%JL%T/Select File To Load", "%R%JL%T/Select Block To Save To", "%R%JL%T/Select File To Delete",
    };
    int slot = Find_Usable_Card(0);
    if (slot == -1)
        return 0;
    card_slots[slot].cursor = 0;
    card_slots[1 - slot].cursor = -1;
    loadsave_sprites[LS_RINGS].x = loadsave_sprites[LS_RINGS].y = OFF_SCREEN;
    loadsave_sprites[LS_PAD4].x = 0x104;
    loadsave_sprites[LS_PAD4].y = 0x53;
    loadsave_sprites[LS_TOGGLE].x = 0x116;
    loadsave_sprites[LS_TOGGLE].y = 0x55;
    loadsave_text[LT_CAPTION].text = loadsave_text[LT_CAPTION + 1].text = prompts[what];
    Set_Card_Slot_Text(slot, card_slots[slot].cursor);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        Check_Card_Status(0);
        Check_Card_Status(1);
        slot = Find_Usable_Card(slot);
        if (slot == -1)
            return 0;
        set_card_status_text(0);
        int32_t *cursor = &card_slots[slot].cursor;
        if (pad.pressed & PAD_CIRCLE) {
            Set_Highlight_Grey();
            Play_Click_FX();
            *cursor = -1;
            slot = Find_Usable_Card(slot ^ 1);
            if (slot == -1)
                return 0;
            card_slots[slot].cursor = 0;
            Set_Card_Slot_Text(slot, card_slots[slot].cursor);
        } else if (pad.pressed & PAD_LEFT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (*cursor > 0) {
                --*cursor;
                Set_Card_Slot_Text(slot, *cursor);
            }
        } else if (pad.pressed & PAD_RIGHT) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (*cursor < CARD_BLOCKS - 1) {
                ++*cursor;
                Set_Card_Slot_Text(slot, *cursor);
            }
        } else if (pad.pressed & PAD_UP) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (*cursor > 2) {
                *cursor -= 3;
                Set_Card_Slot_Text(slot, *cursor);
            }
        } else if (pad.pressed & PAD_DOWN) {
            Set_Highlight_Grey();
            Play_Click_FX();
            if (*cursor < 12) {
                *cursor += 3;
                Set_Card_Slot_Text(slot, *cursor);
            }
        } else if (pad.pressed & PAD_TRIANGLE) {
            loadsave_sprites[LS_PAD4].x = loadsave_sprites[LS_PAD4].y = OFF_SCREEN;
            loadsave_sprites[LS_TOGGLE].x = loadsave_sprites[LS_TOGGLE].y = OFF_SCREEN;
            return -1;
        } else if (pad.pressed & PAD_CROSS) {
            Play_Select_FX();
            *cursor = card_slots[slot].entries[*cursor].index;
            loadsave_sprites[LS_PAD4].x = loadsave_sprites[LS_PAD4].y = OFF_SCREEN;
            loadsave_sprites[LS_TOGGLE].x = loadsave_sprites[LS_TOGGLE].y = OFF_SCREEN;
            return slot;
        }
        Setup_Screen_Text(loadsave_text);
        draw_loadsave_screen();
    }
}

static int Load_Card_Slot(int slot, int block, uint8_t *image) /* 0x801E5B4C */
{
    if (!card_slots[slot].present)
        return 0;
    if (!card_slots[slot].formatted) {
        Show_Card_Error(8);
        return 0;
    }
    if (block_status(slot, block) != CARD_OURS) {
        Show_Card_Error(7);
        return 0;
    }
    Show_Card_Please_Wait(2);
    if (!LoadCardFile(card_slots[slot].entries[block].label, slot, image)) {
        Show_Card_Error(2);
        return 0;
    }
    Load_Card_File();
    return 1;
}

/* Saving to an empty block (formatting the card first if it has to) or over a file, under a typed
 * label no other of our files has. Nonzero when the card's directory should be read again. */
static int Save_Card_Slot(int slot, int block, const uint8_t *image) /* 0x801E5D58 */
{
    char name[16] = "";
    if (!card_slots[slot].present)
        return 0;
    if (block_status(slot, block) == CARD_EMPTY) {
        if (!card_slots[slot].formatted) {
            if (!Ask_Card_Yes_No(2, slot))
                return 0;
            Show_Card_Please_Wait(0);
            if (!Card_Format(slot)) {
                Show_Card_Error(0);
                return 0;
            }
        }
        for (;;) {
            if (!Enter_Card_File_Name(name, slot))
                return 1;
            if (!DupFileCheck(slot, name, block))
                break;
            Show_Card_Error(6);
        }
        Show_Card_Please_Wait(1);
        if (!SaveCardFile(name, slot, image)) {
            Show_Card_Error(3);
            return 0;
        }
        return 1;
    }
    if (!Ask_Card_Yes_No(1, slot))
        return 0;
    for (;;) {
        const CardEntry *e = &card_slots[slot].entries[block];
        if (e->status == CARD_OURS)
            snprintf(name, sizeof(name), "%s", e->label);
        if (!Enter_Card_File_Name(name, slot))
            return 0;
        if (!DupFileCheck(slot, name, block))
            break;
        Show_Card_Error(6);
    }
    if (!DeleteFileMC(card_slots[slot].entries[block].filename, slot)) {
        Show_Card_Error(3);
        return 0;
    }
    Show_Card_Please_Wait(1);
    if (!SaveCardFile(name, slot, image))
        Show_Card_Error(3);
    return 1;
}

/* Cross must be let go, and at least `frames` frames drawn; 7 plays the select sound as well. */
static void Wait_LoadSave_Button_Release(int frames) /* 0x801E6CDC */
{
    if (frames != 2)
        Play_Select_FX();
    for (int frame = 0;; frame++) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        if (!(pad.held & PAD_CROSS) && frame >= frames)
            break;
        draw_loadsave_screen();
    }
}

/* The memory card screen: Load (or Save) and Delete, Left and Right between them. Always returns 0. */
int LoadSave(int mode, uint8_t *image) /* 0x801E09D0 */
{
    bool changed = true;
    int button = 0;
    int16_t top = (int16_t)(pal_mode << 3);
    loadsave_sprites[LS_CARD_ERR].y = top + 0x58;
    loadsave_sprites[LS_CARD_YN].y = top + 0x5d;
    loadsave_sprites[LS_ERROR_YN].y = top + 0x5d;
    loadsave_sprites[LS_SMALRING].y = top + 0x7a;
    card_unusable[0] = card_unusable[1] = 0;
    Init_LoadSave_Prim_Codes();
    loadsave_polys[0].name = mode == LOADSAVE_LOAD ? button_sprites[0] : button_sprites[1];
    loadsave_sprites[LS_RINGS].x = 0xd6;
    loadsave_sprites[LS_RINGS].y = 0xbe;
    loadsave_sprites[LS_PAD4].x = loadsave_sprites[LS_PAD4].y = OFF_SCREEN;
    loadsave_sprites[LS_TOGGLE].x = loadsave_sprites[LS_TOGGLE].y = OFF_SCREEN;
    card_slots[0].cursor = card_slots[1].cursor = -1;
    loadsave_mode = mode;
    Set_LoadSave_Menu_Texts(mode, 0);
    for (int i = LT_STATUS; i < LT_STATUS + 4; i++)
        loadsave_text[i].text = "";
    Setup_Screen_Polys(loadsave_polys);
    Setup_Screen_Text(loadsave_text);
    Setup_Screen_Lines(loadsave_lines);
    Rotate_Slab_On(NULL);
    InitCardBlocks(0);
    InitCardBlocks(1);
    Show_Card_Please_Wait(3);
    LoadCardFiles(0, 2);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        Check_Card_Status(0);
        Check_Card_Status(1);
        set_card_status_text(1);
        if (pad.pressed & PAD_LEFT) {
            Play_Click_FX();
            button = 0;
            Set_LoadSave_Menu_Texts(mode, 0);
            changed = true;
        } else if (pad.pressed & PAD_RIGHT) {
            Play_Click_FX();
            button = 1;
            Set_LoadSave_Menu_Texts(mode, 1);
            changed = true;
        } else if (pad.pressed & PAD_TRIANGLE) {
            Rotate_Slab_Off(NULL);
            return 0;
        } else if (pad.pressed & PAD_CROSS) {
            int sprite = button == 0 ? mode != LOADSAVE_LOAD : 2;
            Play_Select_FX();
            loadsave_polys[button].name = pressed_sprites[sprite];
            Setup_Screen_Polys(loadsave_polys);
            Wait_LoadSave_Button_Release(7);
            loadsave_polys[button].name = button_sprites[sprite];
            Setup_Screen_Polys(loadsave_polys);
            Wait_LoadSave_Button_Release(2);
            if (button == 0) {
                if (mode == LOADSAVE_LOAD) {
                    int slot = Select_Card_File(0);
                    if (slot != -1 && Load_Card_Slot(slot, card_slots[slot].cursor, image))
                        return 0;
                } else if (mode >= LOADSAVE_CONFIG && mode <= LOADSAVE_REPLAY) {
                    int slot = Select_Card_File(1);
                    if (slot != -1 && Save_Card_Slot(slot, card_slots[slot].cursor, image))
                        LoadCardFiles(slot, 1);
                }
            } else {
                int slot = Select_Card_File(2);
                if (slot != -1) {
                    int block = card_slots[slot].cursor;
                    if (block_status(slot, block) != CARD_EMPTY && Ask_Card_Yes_No(0, slot)) {
                        if (!DeleteFileMC(card_slots[slot].entries[block].filename, slot))
                            Show_Card_Error(1);
                        else
                            LoadCardFiles(slot, 1);
                    }
                }
            }
            card_slots[0].cursor = card_slots[1].cursor = -1;
            Set_LoadSave_Menu_Texts(mode, button);
            changed = true;
        }
        if (changed) {
            changed = false;
            loadsave_sprites[LS_RINGS].x = (int16_t)(button * 0x30 + 0xd6);
            loadsave_sprites[LS_RINGS].y = 0xbe;
            Set_Highlight_Grey();
        }
        Setup_Screen_Text(loadsave_text);
        Glow_Selector();
        Glow_Selector();
        Glow_Selector();
        draw_loadsave_screen();
    }
}

int Memory_Card_Screen(void) /* 0x801E7094 */
{
    return LoadSave(LOADSAVE_LOAD, save_image);
}

static void Unpack_Loaded_Config(void) /* 0x801E73A0 */
{
    Unpack_Save_Image(save_image);
}

static void Unpack_Loaded_Game(void) /* 0x801E73D8 */
{
    Unpack_Save_Image(save_image);
    Show_Camcorder_Loading_Screen();
}

/* What a loaded file does: a configuration or a game is taken over, a replay played. */
void Load_Card_File(void) /* 0x801E70E0 */
{
    switch (save_image[0] | save_image[1] << 8) {
    case SAVE_BOOT_CONFIG:
        Unpack_Loaded_Config();
        break;
    case SAVE_CONFIG:
        Unpack_Loaded_Config();
        Rotate_Slab_Off(NULL);
        break;
    case SAVE_REPLAY:
        Rotate_Slab_Off(NULL);
        Loading_Screen_From_Slab(NULL);
        Play_Saved_Replay();
        Show_Camcorder_Loading_Screen();
        break;
    case SAVE_GAME:
        Unpack_Loaded_Game();
        break;
    default:
        break;
    }
}

void Save_Configuration(void) /* 0x801E71D8 */
{
    Pack_Save_Image(save_image, SAVE_CONFIG);
    LoadSave(LOADSAVE_CONFIG, save_image);
}

void Save_Game_State_To_Card(void) /* 0x801E7228 */
{
    Pack_Save_Image(save_image, SAVE_GAME);
    LoadSave(LOADSAVE_GAME, save_image);
}

static void put16(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static int16_t get16(const uint8_t *p)
{
    return (int16_t)(p[0] | p[1] << 8);
}

/* A replay save: byte offsets of its fields after the type halfword. */
#define REPLAY_CAR 0x02
#define REPLAY_END 0x04       /* the end of the script, as a PSX address in two halfwords */
#define REPLAY_MODE 0x08
#define REPLAY_TYPE 0x0a
#define REPLAY_DIVISION 0x0c
#define REPLAY_LEVEL 0x0e
#define REPLAY_PAD_TYPE 0x10
#define REPLAY_SCRIPT 0x12
#define REPLAY_CAR_ORDER (REPLAY_SCRIPT + REPLAY_SCRIPT_WORDS * 2)
_Static_assert(REPLAY_CAR_ORDER == 0x1c12, "replay save layout");

void Save_Race_Replay(void) /* 0x801E7278 */
{
    uint32_t end = REPLAY_SCRIPT_PSX + (uint32_t)(replay_script_end - replay_script) * 2;
    put16(save_image, SAVE_REPLAY);
    put16(save_image + REPLAY_CAR, race_car);
    put16(save_image + REPLAY_END, (int32_t)end);
    put16(save_image + REPLAY_END + 2, (int32_t)(end >> 16));
    put16(save_image + REPLAY_MODE, race_mode);
    put16(save_image + REPLAY_TYPE, race_type);
    put16(save_image + REPLAY_DIVISION, division);
    put16(save_image + REPLAY_LEVEL, replay_level);
    put16(save_image + REPLAY_PAD_TYPE, replay_pad_type);
    memcpy(save_image + REPLAY_SCRIPT, replay_script, REPLAY_SCRIPT_WORDS * 2);
    memcpy(save_image + REPLAY_CAR_ORDER, car_order, sizeof(car_order));
    LoadSave(LOADSAVE_REPLAY, save_image);
}

/* Plays the loaded replay with 20 cars, keeping the game setup it changes. */
void Play_Saved_Replay(void) /* 0x801E7418 */
{
    int32_t cars = num_cars, car = race_car, mode = race_mode, type = race_type;
    uint32_t end = (uint32_t)(uint16_t)get16(save_image + REPLAY_END) |
                   (uint32_t)(uint16_t)get16(save_image + REPLAY_END + 2) << 16;
    uint32_t words = (end - REPLAY_SCRIPT_PSX) / 2;
    num_cars = 20;
    race_car = get16(save_image + REPLAY_CAR);
    race_mode = get16(save_image + REPLAY_MODE);
    race_type = get16(save_image + REPLAY_TYPE);
    division = get16(save_image + REPLAY_DIVISION);
    /* PORT: the original takes the stored address as it is; a damaged file is kept inside the script. */
    replay_script_end = replay_script + (words <= REPLAY_SCRIPT_WORDS ? words : REPLAY_SCRIPT_WORDS);
    replay_level = get16(save_image + REPLAY_LEVEL);
    current_level = get16(save_image + REPLAY_LEVEL);
    replay_pad_type = get16(save_image + REPLAY_PAD_TYPE);
    memcpy(replay_script, save_image + REPLAY_SCRIPT, REPLAY_SCRIPT_WORDS * 2);
    memcpy(car_order, save_image + REPLAY_CAR_ORDER, sizeof(car_order));
    View_Frontend_Replay();
    race_mode = mode;
    race_type = type;
    race_car = car;
    num_cars = cars;
}

void Keep_Season_Snapshot(void) /* 0x801E75E8 */
{
    Pack_Save_Image(season_snapshot, SAVE_GAME);
}

void Restore_Season_Snapshot(void) /* 0x801E7624 */
{
    Unpack_Save_Image(season_snapshot);
}

/* At start-up: the configuration of the first card file holding one, if any. */
void Load_First_Config(void) /* 0x801E765C */
{
    int found = Find_First_Card_Save();
    if (found == -1)
        return;
    int slot = found >> 16;
    if (LoadCardFile(card_slots[slot].entries[found & 0xf].label, slot, save_image)) {
        put16(save_image, SAVE_BOOT_CONFIG);
        Load_Card_File();
    }
}

/* A configuration or game save, 0x1978 bytes of the image: halfwords (GAME_*, by index), then the tables.
 * champ_info: the original puts it 4 bytes into the last season's statistics, which a load then gets
 * back as champ_info's first bytes; kept, so the files stay the same. */
enum {
    GAME_TYPE, GAME_RACE_MODE, GAME_RACE_TYPE, GAME_RACE_CAR, GAME_RACE_TRACK, GAME_RACE_TRACK_COUNT,
    GAME_ARENA_COUNT, GAME_MUSIC_VOLUME, GAME_SOUND_VOLUME, GAME_PAD_TYPE, GAME_PAD_LAYOUT, GAME_PAD_CONFIG_1,
    GAME_PAD_CONFIG_2, GAME_SCREEN_X_PAL, GAME_SCREEN_Y_PAL, GAME_SCREEN_X_NTSC, GAME_SCREEN_Y_NTSC,
    GAME_RACE_MENU_CHOICE, GAME_STATS_AVAILABLE, GAME_PLAYER_CAR, GAME_CURRENT_PLAYER, GAME_PLAYERS_PER_RACE,
    GAME_SEASON_RACE, GAME_DIVISION, GAME_SEASON_RACES, GAME_HUMAN_PLAYERS, GAME_NUM_CARS, GAME_RACE_CUTOFF,
    GAME_STATS_SEASON, GAME_SEASONS_PLAYED,
    GAME_HALFWORDS
};
#define GAME_SEASON_STATS 0x3c
#define GAME_CHAMP_INFO 0x1298
#define GAME_PLAYER_NAMES 0x16d0
#define GAME_BEST_LAPS 0x1748
_Static_assert(GAME_HALFWORDS * 2 == GAME_SEASON_STATS, "the halfwords end at the statistics");

static int16_t game_half(const uint8_t *image, int index)
{
    return get16(image + index * 2);
}

void Pack_Save_Image(uint8_t *image, int type) /* 0x801E777C */
{
    const int32_t values[] = {
        type, race_mode, race_type, race_car, race_track, race_track_count, arena_count, music_volume,
        sound_volume, pad.type, pad_layout_setting, pad_config[0], pad_config[1], video_modes[0].screen_x,
        video_modes[0].screen_y, video_modes[1].screen_x, video_modes[1].screen_y, race_menu_choice,
        stats_available, current_player_car, current_player, players_per_race, season_race, division,
        season_races, human_players, num_cars, race_cutoff, stats_season, seasons_played,
    };
    _Static_assert(sizeof(values) / sizeof(values[0]) == GAME_HALFWORDS, "a value per halfword");
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        put16(image + i * 2, values[i]);
    memcpy(image + GAME_SEASON_STATS, season_stats, sizeof(season_stats));
    memcpy(image + GAME_CHAMP_INFO, champ_info, sizeof(champ_info));
    memcpy(image + GAME_PLAYER_NAMES, player_names, sizeof(player_names));
    memcpy(image + GAME_BEST_LAPS, best_laps, sizeof(best_laps));
}

void Unpack_Save_Image(const uint8_t *image) /* 0x801E7CAC */
{
    race_mode = game_half(image, GAME_RACE_MODE);
    race_type = game_half(image, GAME_RACE_TYPE);
    race_car = game_half(image, GAME_RACE_CAR);
    race_track = game_half(image, GAME_RACE_TRACK);
    race_track_count = game_half(image, GAME_RACE_TRACK_COUNT);
    arena_count = game_half(image, GAME_ARENA_COUNT);
    music_volume = game_half(image, GAME_MUSIC_VOLUME);
    sound_volume = game_half(image, GAME_SOUND_VOLUME);
    set_master_volume(sound_volume);
    Set_CD_Volume((short)music_volume);
    Poll_Pad_Buttons();
    if (pad.type == game_half(image, GAME_PAD_TYPE))
        Setup_Pad(game_half(image, GAME_PAD_TYPE), game_half(image, GAME_PAD_LAYOUT),
                  game_half(image, GAME_PAD_CONFIG_1), game_half(image, GAME_PAD_CONFIG_2));
    video_modes[0].screen_x = game_half(image, GAME_SCREEN_X_PAL);
    video_modes[0].screen_y = game_half(image, GAME_SCREEN_Y_PAL);
    video_modes[1].screen_x = game_half(image, GAME_SCREEN_X_NTSC);
    video_modes[1].screen_y = game_half(image, GAME_SCREEN_Y_NTSC);
    Set_Draw_Mode(0);
    race_menu_choice = game_half(image, GAME_RACE_MENU_CHOICE);
    stats_available = game_half(image, GAME_STATS_AVAILABLE);
    current_player_car = game_half(image, GAME_PLAYER_CAR);
    current_player = game_half(image, GAME_CURRENT_PLAYER);
    players_per_race = game_half(image, GAME_PLAYERS_PER_RACE);
    season_race = game_half(image, GAME_SEASON_RACE);
    division = game_half(image, GAME_DIVISION);
    season_races = game_half(image, GAME_SEASON_RACES);
    human_players = game_half(image, GAME_HUMAN_PLAYERS);
    num_cars = game_half(image, GAME_NUM_CARS);
    race_cutoff = game_half(image, GAME_RACE_CUTOFF);
    stats_season = game_half(image, GAME_STATS_SEASON);
    seasons_played = game_half(image, GAME_SEASONS_PLAYED);
    memcpy(season_stats, image + GAME_SEASON_STATS, sizeof(season_stats));
    memcpy(champ_info, image + GAME_CHAMP_INFO, sizeof(champ_info));
    memcpy(player_names, image + GAME_PLAYER_NAMES, sizeof(player_names));
    memcpy(best_laps, image + GAME_BEST_LAPS, sizeof(best_laps));
}
