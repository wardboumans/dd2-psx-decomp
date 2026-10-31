#include "system/card.h"

#include "psx/libapi.h"
#include "psx/libcard.h"
#include "psx/libgpu.h"

#include <stdio.h>
#include <string.h>

#define EvMdNOINTR 0x2000
#define CONFIG_SAVE 0x1010 /* SAVE_CONFIG of game/loadsave.h */
#define ICON_HEADER (CARD_HEADER_ICON + 0x80) /* the header up to the end of the icon */

/* Order of the four events in both arrays. */
static const long card_event_specs[4] = {
    EvSpIOE,    /* IO finished */
    EvSpERROR,
    EvSpTIMOUT, /* no card */
    EvSpNEW,    /* a new card (or an unformatted one) */
};

long card_sw_events[4];
long card_hw_events[4];

void InitCardSystem(void)
{
    InitCARD(1);
    StartCARD();
    _bu_init();
    _card_auto(0);
    for (int i = 0; i < 4; i++)
        card_sw_events[i] = OpenEvent(SwCARD, card_event_specs[i], EvMdNOINTR, NULL);
    for (int i = 0; i < 4; i++)
        card_hw_events[i] = OpenEvent(HwCARD, card_event_specs[i], EvMdNOINTR, NULL);
    for (int i = 0; i < 4; i++)
        EnableEvent(card_sw_events[i]);
    for (int i = 0; i < 4; i++)
        EnableEvent(card_hw_events[i]);
}

CardSlot card_slots[2];

/* Which of the four events happened first: 0 IO done, 1 error, 2 timeout (no card), 3 new card. */
int Card_Wait_Sw_Event(void)
{
    for (;;) {
        if (TestEvent(card_sw_events[0]) == 1)
            return 0;
        if (TestEvent(card_sw_events[1]) == 1)
            return 1;
        if (TestEvent(card_sw_events[2]) == 1)
            return 2;
        if (TestEvent(card_sw_events[3]) == 1)
            return 3;
    }
}

int Card_Wait_Hw_Event(void)
{
    for (;;) {
        if (TestEvent(card_hw_events[0]) == 1)
            return 0;
        if (TestEvent(card_hw_events[1]) == 1)
            return 1;
        if (TestEvent(card_hw_events[2]) == 1)
            return 2;
        if (TestEvent(card_hw_events[3]) == 1)
            return 3;
    }
}

void Card_Clear_Hw_Events(void)
{
    for (int i = 0; i < 4; i++)
        TestEvent(card_hw_events[i]);
}

void Card_Clear_Sw_Events(void)
{
    for (int i = 0; i < 4; i++)
        TestEvent(card_sw_events[i]);
}

int Card_Info(int slot)
{
    _card_info(slot << 4);
    return Card_Wait_Sw_Event();
}

int Card_Wait(void)
{
    return Card_Wait_Sw_Event();
}

void Card_Load(int slot)
{
    Card_Clear_Sw_Events();
    _card_load(slot << 4);
}

int Card_Format(int slot)
{
    char device[16];
    snprintf(device, sizeof(device), "bu%d0:", slot);
    return format(device) != 0;
}

/* A save: the 512-byte header (title and icon), then 0x1E00 bytes of `data`, in one block. */
int SaveCardFile(const char *label, int slot, const void *data)
{
    uint8_t header[CARD_HEADER_SIZE];
    char filename[32];
    Make_Card_Header(header, label);
    Make_Card_Filename(label, filename, slot);
    if (!Card_Create_File(filename))
        return 0;
    return Card_Write_File(header, filename, data) != 0;
}

int DeleteFileMC(const char *filename, int slot)
{
    char path[32];
    snprintf(path, sizeof(path), "bu%d0:%s", slot, filename);
    return delete(path) != 0;
}

/* The save's icon (one frame, 16x16 at 4 bits) and its colour table (0x801EC984, 0x801ECA04). */
static const uint8_t save_icon[0x80] = {
    0xff, 0xff, 0xef, 0xcb, 0xbc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xac, 0x8a, 0x58, 0x95, 0xfe, 0xff,
    0xff, 0xbf, 0x8a, 0x55, 0x85, 0x85, 0xec, 0xff, 0xff, 0xae, 0x5a, 0x55, 0x85, 0x88, 0xb8, 0xff,
    0xff, 0xac, 0x58, 0x55, 0x98, 0xaa, 0xc8, 0xff, 0xff, 0x13, 0x11, 0x11, 0x11, 0x61, 0x9c, 0xfe,
    0xff, 0x01, 0x00, 0x00, 0x00, 0x30, 0x97, 0xfe, 0xff, 0xdd, 0x7d, 0x47, 0x72, 0x77, 0xab, 0xfe,
    0xff, 0xff, 0xdf, 0x7d, 0xf4, 0xff, 0xac, 0xfe, 0xff, 0xee, 0x7e, 0x7d, 0xd4, 0x9b, 0x9a, 0xff,
    0xff, 0xac, 0x58, 0x88, 0x88, 0xa8, 0xca, 0xff, 0xff, 0xac, 0x8a, 0x8a, 0xaa, 0xaa, 0xe9, 0xff,
    0xff, 0xae, 0x8a, 0x8a, 0xaa, 0xbc, 0xfd, 0xff, 0xff, 0xef, 0xbb, 0xcc, 0xbb, 0x47, 0xf4, 0xff,
    0xff, 0xff, 0x77, 0xdd, 0x37, 0x11, 0x22, 0xd3, 0xff, 0xde, 0x23, 0x22, 0x10, 0x53, 0x58, 0x00,
};
static const uint8_t save_icon_clut[0x20] = {
    0x9d, 0x63, 0xda, 0x52, 0xb4, 0x56, 0x34, 0x42, 0xef, 0x3d, 0x57, 0x29, 0x32, 0x25, 0x29, 0x25,
    0x95, 0x10, 0x72, 0x0c, 0x33, 0x04, 0x6b, 0x0c, 0x2d, 0x00, 0x84, 0x10, 0x03, 0x00, 0x00, 0x80,
};

/* The header the BIOS shows: "SC", a static icon, one block, the title in full-width Shift-JIS
 * ("ＤＤ２　" and the label; the original's +0x1F conversion is right for capitals and digits only).
 * PORT: the second 256 bytes are zeroed; the original leaves them as whatever was on the stack. */
void Make_Card_Header(uint8_t *header, const char *label)
{
    static const uint8_t title[8] = {0x82, 0x63, 0x82, 0x63, 0x82, 0x51, 0x81, 0x40};
    memset(header, 0, CARD_HEADER_SIZE);
    header[0] = 'S';
    header[1] = 'C';
    header[2] = CARD_ICON_ONE_FRAME;
    header[3] = 1;
    memcpy(header + 4, title, sizeof(title));
    size_t n = strlen(label);
    for (size_t i = 0; i < n; i++) {
        header[0xc + i * 2] = 0x82;
        header[0xd + i * 2] = (uint8_t)(label[i] + 0x1f);
    }
    header[n * 2 + 0xe] = 0x81;
    header[n * 2 + 0xf] = 0x40;
    memcpy(header + CARD_HEADER_CLUT, save_icon_clut, sizeof(save_icon_clut));
    memcpy(header + CARD_HEADER_ICON, save_icon, sizeof(save_icon));
}

/* "bu00:BASCUS-94350" (slot 1) or "bu10:..." and the label. */
void Make_Card_Filename(const char *label, char *filename, int slot)
{
    strcpy(filename, slot == 0 ? "bu00:BASCUS-94350" : "bu10:BASCUS-94350");
    strcat(filename, label);
}

int Card_Create_File(const char *filename)
{
    int fd = open(filename, FCREAT | 1 << 16);
    if (fd >= 0)
        close(fd);
    return fd >= 0;
}

int Card_Write_File(const void *header, const char *filename, const void *data)
{
    int fd = open(filename, FWRITE);
    if (fd < 0)
        return 0;
    int ok = write(fd, header, CARD_HEADER_SIZE) == CARD_HEADER_SIZE && write(fd, data, CARD_DATA_SIZE) == CARD_DATA_SIZE;
    close(fd);
    return ok;
}

/* The slot's directory as the menus show it: our saves by label, other games' blocks as USED, the rest
 * as left by InitCardBlocks; then the icons. */
void Read_Card_Directory(int slot)
{
    CardSlot *s = &card_slots[slot];
    struct DIRENTRY dir;
    int n = 0;
    for (int i = 0; i < CARD_BLOCKS; i++) {
        s->entries[i].filename[0] = '\0';
        s->entries[i].label[0] = '\0';
        s->entries[i].status = CARD_EMPTY;
        s->entries[i].index = (int16_t)i;
    }
    if (firstfile(slot == 0 ? "bu00:*" : "bu10:*", &dir) != NULL) {
        do {
            int16_t first = (int16_t)n;
            CardEntry *e = &s->entries[n];
            if (strncmp(dir.name, "BASCUS-94350", 12) == 0) {
                memcpy(e->label, dir.name + 12, 8);
                e->label[8] = '\0';
                strcpy(e->filename, dir.name);
                e->index = first;
                e->status = CARD_OURS;
                n++;
            } else {
                int blocks = (int)(dir.size / CARD_BLOCK_SIZE);
                e->status = CARD_OTHER;
                e->index = first;
                strcpy(e->filename, dir.name);
                strcpy(e->label, "USED");
                n++;
                for (int k = 1; k < blocks && n < CARD_BLOCKS; k++, n++) {
                    s->entries[n].status = CARD_OTHER;
                    s->entries[n].index = first;
                    strcpy(s->entries[n].filename, dir.name);
                }
            }
        } while (n < CARD_BLOCKS && nextfile(&dir) != NULL);
    }
    for (int i = 0; i < n; i++) {
        char path[40];
        snprintf(path, sizeof(path), "%s%s", slot == 0 ? "bu00:" : "bu10:", s->entries[i].filename);
        Load_Card_Icon(path, (int16_t)slot, (int16_t)i);
    }
}

int LoadCardFile(const char *label, int slot, void *data)
{
    uint8_t header[CARD_HEADER_SIZE];
    char filename[0x120];
    Make_Card_Filename(label, filename, slot);
    int fd = open(filename, FREAD);
    if (fd < 0)
        return 0;
    int ok = read(fd, header, CARD_HEADER_SIZE) == CARD_HEADER_SIZE && read(fd, data, CARD_DATA_SIZE) == CARD_DATA_SIZE;
    close(fd);
    return ok;
}

/* The first card with one of our saves holding a game (type 0x1010): slot << 16 | entry, or -1. */
int Find_First_Card_Save(void)
{
    for (int slot = 0; slot < 2; slot++) {
        InitCardBlocks(slot);
        LoadCardFiles(slot, 1);
        if (card_slots[slot].present && card_slots[slot].formatted) {
            int entry = FirstSavedGame(slot);
            if (entry != -1)
                return slot << 16 | entry;
        }
    }
    return -1;
}

/* Looks at `count` slots from `first` (a new card is cleared first) and reads both directories. */
void LoadCardFiles(int first, int count)
{
    for (int slot = first; slot < first + count; slot++) {
        if (Card_Info(slot) == 3) {
            Card_Clear_Hw_Events();
            _card_clear(slot << 4);
            Card_Wait_Hw_Event();
        }
        Card_Load(slot);
        int r = Card_Wait();
        if (r == 0) {
            card_slots[slot].present = 1;
            card_slots[slot].formatted = 1;
        } else if (r == 3) {
            card_slots[slot].present = 1;
            card_slots[slot].formatted = 0;
        }
        card_slots[slot].icons_hidden = 0;
    }
    Read_Card_Directory(0);
    Read_Card_Directory(1);
}

void InitCardBlocks(int slot)
{
    for (int i = 0; i < CARD_BLOCKS; i++) {
        CardEntry *e = &card_slots[slot].entries[i];
        strcpy(e->label, "EMPTY");
        e->index = (int16_t)i;
        e->filename[0] = '\0';
        e->status = CARD_EMPTY;
    }
}

/* A card taken out empties the slot; a new one is read. */
void Check_Card_Status(int slot)
{
    int r = Card_Info(slot);
    if (r == 2) {
        InitCardBlocks(slot);
        card_slots[slot].present = 0;
    } else if (r == 3) {
        Card_Clear_Hw_Events();
        _card_clear(slot << 4);
        Card_Wait_Hw_Event();
        Card_Load(slot);
        r = Card_Wait();
        if (r == 0) {
            Read_Card_Directory(slot);
            card_slots[slot].present = 1;
            card_slots[slot].formatted = 1;
        } else if (r == 3) {
            card_slots[slot].present = 1;
            card_slots[slot].formatted = 0;
        }
    }
}

/* Another of our saves (not entry `skip`) with this label. */
int DupFileCheck(int slot, const char *label, int skip)
{
    for (int i = 0; i < CARD_BLOCKS; i++) {
        const CardEntry *e = &card_slots[slot].entries[i];
        if (i != skip && e->status == CARD_OURS && strcmp(e->label, label) == 0)
            return 1;
    }
    return 0;
}

/* The file's icon and colour table into VRAM, by slot and entry. */
int Load_Card_Icon(const char *path, int16_t slot, int16_t entry)
{
    u_long header[ICON_HEADER / sizeof(u_long)];
    int fd = open(path, FREAD);
    if (fd < 0)
        return 0;
    if (read(fd, header, ICON_HEADER) != ICON_HEADER) {
        close(fd);
        return 0;
    }
    RECT clut = {CARD_ICON_X, (int16_t)(entry + slot * CARD_BLOCKS + CARD_CLUT_Y), 0x10, 1};
    LoadImage(&clut, header + CARD_HEADER_CLUT / sizeof(u_long));
    DrawSync(0);
    RECT icon = {(int16_t)(entry * 4 + CARD_ICON_X), (int16_t)(slot * 0x10 + CARD_ICON_Y), 4, 0x10};
    LoadImage(&icon, header + CARD_HEADER_ICON / sizeof(u_long));
    DrawSync(0);
    close(fd);
    return 1;
}

int Card_Is_Full(int slot)
{
    for (int i = 0; i < CARD_BLOCKS; i++)
        if (card_slots[slot].entries[i].status == CARD_EMPTY)
            return 0;
    return 1;
}

int FirstSavedGame(int slot)
{
    for (int i = 0; i < CARD_BLOCKS; i++) {
        const CardEntry *e = &card_slots[slot].entries[i];
        if (e->status != CARD_OURS)
            continue;
        char filename[32];
        uint8_t data[CARD_HEADER_SIZE + 0x80]; /* the header and the save's first 128 bytes */
        Make_Card_Filename(e->label, filename, slot);
        int fd = open(filename, FREAD);
        if (fd < 0)
            continue;
        int n = read(fd, data, CARD_HEADER_SIZE + 0x80);
        close(fd);
        if (n == CARD_HEADER_SIZE + 0x80 && (data[CARD_HEADER_SIZE] | data[CARD_HEADER_SIZE + 1] << 8) == CONFIG_SAVE)
            return i;
    }
    return -1;
}
