#ifndef SYSTEM_FILE_H
#define SYSTEM_FILE_H

#include "psx/libcd.h"

#include <stdint.h>

/* One entry of the DIRINFO table (on-disk layout). */
typedef struct {
    char name[18];    /* e.g. "LEV0\\FONT.BNK" */
    uint16_t sector;  /* from the start of DIRINFO */
    uint32_t size;
} DirEntry;

_Static_assert(sizeof(DirEntry) == 24, "DirEntry must match the on-disk layout");

#define DIR_SECTORS 5
/* Rounded up: CdRead writes whole sectors (0x2800 bytes), which is not a multiple of 24. */
#define DIR_ENTRIES ((DIR_SECTORS * 2048 + (int)sizeof(DirEntry) - 1) / (int)sizeof(DirEntry))

typedef struct {
    int32_t sector;
    int32_t size;
} FileLocation;

/* Where a queued file ends up. `info` bits 3-7 select the File_Func_List handler; once loaded,
 * bits 8-31 hold the file size. */
typedef struct {
    uint8_t *data;
    uint32_t info;
} FileRecord;

#define FILE_TYPE_SHIFT 3
#define FILE_TYPE_MASK 0x1f
#define FILE_SIZE_SHIFT 8

/* Called with the loaded data and its size; returns the bytes to keep (0 frees the buffer). */
typedef uint32_t (*FileFunc)(uint8_t *data, uint32_t size);

enum {
    FILE_PLAIN = 0,
    FILE_SAMPLE_BANK = 1,
    FILE_FONT_CLUTS = 2,
    FILE_TEXTURE_TABLE = 3,
    FILE_TEXTURE_DATA = 4,
    FILE_CLUTS = 5,
};

/* Texture table entry (on-disk layout, 16 bytes). */
typedef struct {
    int16_t depth;   /* 4 or 8 bits per pixel */
    int16_t width;   /* in pixels */
    int16_t height;
    int16_t vram_x, vram_y;
    int16_t clut_x, clut_y; /* clut_y == -2: no CLUT to upload */
    int16_t pad;
} TextureEntry;

_Static_assert(sizeof(TextureEntry) == 16, "TextureEntry must match the file layout");

extern const FileFunc File_Func_List[];         /* 0x800717C0 */
extern const TextureEntry *texture_table_cursor; /* 0x80079AE0 */
extern int32_t texture_table_count;             /* 0x80079948 */

extern DirEntry dirbuf[DIR_ENTRIES];  /* 0x80084928 */
extern int32_t dirinfo_sector;        /* 0x800799F8, absolute sector of DIRINFO */
extern int32_t sectors_queued;        /* 0x80079944, sectors of all files looked up, for the loading bar */

uint32_t File_Passthrough(uint8_t *data, uint32_t size);                       /* 0x8005118C */
const TextureEntry *Load_Textures(const uint8_t *data, const TextureEntry *table, int32_t size); /* 0x800511C0 */
uint32_t Load_Texture_Table_File(uint8_t *data, uint32_t size);                /* 0x80051538 */
uint32_t Load_Texture_Data_File(uint8_t *data, uint32_t size);                 /* 0x80051600 */
uint32_t File_Load_Cluts_Entry(uint8_t *data, uint32_t size);                  /* 0x8005165C */
void Add_Buffer_Load(char *name, FileRecord *record, uint32_t type);           /* 0x800517EC */
void Buffer_Load(uint8_t *dest, void (*progress)(int));                        /* 0x800518D4 */
uint32_t dispatch_file_record(FileRecord *record, int shrink);                 /* 0x80051E98 */
uint32_t Load_Sample_Bank(uint8_t *data, uint32_t size);                       /* 0x80052460 */
uint32_t load_font_cluts(uint8_t *data, uint32_t size);                        /* 0x800525EC */
void Read_Directory(char *name);                         /* 0x80051C58 */
int32_t File_Load(char *name, void *dest);               /* 0x80051D54 */
int directory_has_file(char *name);                      /* 0x80051F58 */
void Lookup_Load_File(char *name, FileLocation *where);  /* 0x80051FF4 */
void Sector_To_CdlLOC(int32_t sector, CdlLOC *loc);      /* 0x800520D4 */
int32_t CdlLOC_To_Sector(CdlLOC *loc);                   /* 0x80052304 */

#endif
