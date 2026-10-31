/* The memory cards: BIOS events, the two slots' directories as the menus show them, and the game's
 * save files (one block each: a 512-byte header, then 0x1E00 bytes of data). */
#ifndef SYSTEM_CARD_H
#define SYSTEM_CARD_H

#include <stdint.h>

#define CARD_BLOCKS 15
#define CARD_BLOCK_SIZE 0x2000
#define CARD_HEADER_SIZE 0x200  /* the BIOS header of a save: title, colour table, icon */
#define CARD_DATA_SIZE (CARD_BLOCK_SIZE - CARD_HEADER_SIZE)
#define CARD_HEADER_CLUT 0x60
#define CARD_HEADER_ICON 0x80
#define CARD_ICON_ONE_FRAME 0x11 /* header byte 2: a still icon */

/* The directory's icons in VRAM: a 16x16 4-bit icon (4 halfwords wide) per entry, a row of them per slot,
 * and a colour table row per entry. */
#define CARD_ICON_X 0x140
#define CARD_ICON_Y 0x100
#define CARD_CLUT_Y 0x120

enum { CARD_EMPTY, CARD_OURS, CARD_OTHER };

/* A block of a card (0x24 bytes in the original). */
typedef struct {
    char filename[0x16]; /* +0x00: "BASCUS-94350" and the label for ours */
    char label[10];      /* +0x16: ours: the label (8 letters); "USED", "EMPTY" */
    int16_t status;      /* +0x20: CARD_* */
    int16_t index;       /* +0x22: the file's first entry */
} CardEntry;

_Static_assert(sizeof(CardEntry) == 0x24, "CardEntry");

/* A slot (0x22C bytes in the original). */
typedef struct {
    CardEntry entries[CARD_BLOCKS];
    int32_t cursor;      /* +0x21C: the block chosen on the load/save screen, -1 none */
    int32_t icons_hidden; /* +0x220: cleared whenever the card is read and never set, so the icons always show */
    int32_t present;     /* +0x224 */
    int32_t formatted;   /* +0x228 */
} CardSlot;

/* Memory-card events, software (BIOS) and hardware: IO end, error, timeout, new card. */
extern long card_sw_events[4]; /* 0x800799FC */
extern long card_hw_events[4]; /* 0x80079A0C */
extern CardSlot card_slots[2]; /* 0x800A2E04 */

void InitCardSystem(void);                                        /* 0x8005BB40 */
int Card_Info(int slot);                                          /* 0x8005BD30 */
int Card_Wait_Sw_Event(void);                                     /* 0x8005BD58 */
int Card_Wait_Hw_Event(void);                                     /* 0x8005BDCC */
void Card_Clear_Hw_Events(void);                                  /* 0x8005BE40 */
void Card_Clear_Sw_Events(void);                                  /* 0x8005BE88 */
int Card_Wait(void);                                              /* 0x8005BED0 */
void Card_Load(int slot);                                         /* 0x8005BEF0 */
int Card_Format(int slot);                                        /* 0x8005BF20 */
int SaveCardFile(const char *label, int slot, const void *data);  /* 0x8005C040 */
int DeleteFileMC(const char *filename, int slot);                 /* 0x8005C0C4 */
void Make_Card_Header(uint8_t *header, const char *label);        /* 0x8005C100 */
void Make_Card_Filename(const char *label, char *filename, int slot); /* 0x8005C338 */
int Card_Create_File(const char *filename);                       /* 0x8005C420 */
int Card_Write_File(const void *header, const char *filename, const void *data); /* 0x8005C460 */
void Read_Card_Directory(int slot);                               /* 0x8005C4FC */
int LoadCardFile(const char *label, int slot, void *data);        /* 0x8005C8DC */
int Find_First_Card_Save(void);                                   /* 0x8005C980 */
void LoadCardFiles(int first, int count);                         /* 0x8005CAE0 */
void InitCardBlocks(int slot);                                    /* 0x8005CCB0 */
void Check_Card_Status(int slot);                                 /* 0x8005CD6C */
int DupFileCheck(int slot, const char *label, int skip);          /* 0x8005CF00 */
int Load_Card_Icon(const char *path, int16_t slot, int16_t entry); /* 0x8005CFE0 */
int Card_Is_Full(int slot);                                       /* 0x8005D0E4 */
int FirstSavedGame(int slot);                                     /* 0x8005D13C */

#endif
