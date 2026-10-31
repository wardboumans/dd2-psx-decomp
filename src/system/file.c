#include "system/file.h"

#include "graphics/font.h"
#include "psx/libgpu.h"
#include "psx/libspu.h"
#include "sound/sound.h"
#include "system/mpe_heap.h"

#include <string.h>

#define SECTOR_SIZE 0x800
#define CD_READ_DOUBLE_SPEED 0x80
#define PROGRESS_DONE 0x1000

/* One file waiting in the Add_Buffer_Load queue (16 bytes in the original). */
typedef struct LoadNode {
    FileLocation where;
    struct LoadNode *next;
    FileRecord *record;
} LoadNode;

const FileFunc File_Func_List[32] = {
    File_Passthrough, Load_Sample_Bank, load_font_cluts,
    Load_Texture_Table_File, Load_Texture_Data_File, File_Load_Cluts_Entry,
};

DirEntry dirbuf[DIR_ENTRIES];
int32_t dirinfo_sector;
int32_t sectors_queued;
const TextureEntry *texture_table_cursor;
int32_t texture_table_count;

static LoadNode *load_queue; /* 0x80079940 */
static LoadNode *load_tail;  /* 0x8007994C */

static int32_t read_i32(const uint8_t *p)
{
    int32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

uint32_t File_Passthrough(uint8_t *data, uint32_t size)
{
    (void)data;
    return size;
}

/* Uploads every texture of a texture file to VRAM, with its CLUT; returns where the table ends.
 * Per texture the data holds 4 bytes, the CLUT, 4 bytes, then the pixels. */
const TextureEntry *Load_Textures(const uint8_t *data, const TextureEntry *table, int32_t size)
{
    const uint8_t *p = data;
    DrawSync(0);
    while (p - data < size) {
        RECT r;
        int clut_size = table->depth == 4 ? 16 : 256;
        r.x = table->clut_x;
        r.y = table->clut_y;
        r.w = (short)clut_size;
        r.h = 1;
        if (r.y != -2)
            LoadImage(&r, (u_long *)(p + 4));
        DrawSync(0);
        const uint8_t *pixels = p + 4 + clut_size * 2 + 4;
        r.x = table->vram_x;
        r.y = table->vram_y;
        r.w = (short)(table->depth == 4 ? table->width / 4 : table->width / 2);
        r.h = table->height;
        LoadImage(&r, (u_long *)pixels);
        DrawSync(0);
        p = pixels + (table->depth == 4 ? table->width / 2 : table->width) * table->height;
        table++;
    }
    return table;
}

/* A texture table (count, entries) followed by the first textures' data. */
uint32_t Load_Texture_Table_File(uint8_t *data, uint32_t size)
{
    texture_table_count = read_i32(data);
    uint32_t header = (uint32_t)texture_table_count * sizeof(TextureEntry) + 4;
    texture_table_cursor = Load_Textures(data + header, (const TextureEntry *)(data + 4), (int32_t)(size - header));
    return header;
}

/* More texture data for the entries after the previous file's. */
uint32_t Load_Texture_Data_File(uint8_t *data, uint32_t size)
{
    texture_table_cursor = Load_Textures(data, texture_table_cursor, (int32_t)size);
    return 0;
}

/* count, then (depth, x, y, pad) shorts per CLUT, then the CLUTs' colours. */
uint32_t File_Load_Cluts_Entry(uint8_t *data, uint32_t size)
{
    (void)size;
    DrawSync(0);
    int32_t count = read_i32(data);
    const int16_t *entry = (const int16_t *)(data + 4);
    const uint8_t *colours = data + 4 + count * 8;
    for (int i = 0; i < count; i++, entry += 4) {
        int width = entry[0] == 4 ? 16 : 256;
        RECT r = {entry[1], entry[2], (short)width, 1};
        LoadImage(&r, (u_long *)colours);
        DrawSync(0);
        colours += width * 2;
    }
    return 0;
}

void Add_Buffer_Load(char *name, FileRecord *record, uint32_t type)
{
    LoadNode *node = MPE_malloc(sizeof(LoadNode));
    if (!load_queue) {
        sectors_queued = 0;
        load_queue = node;
    } else {
        load_tail->next = node;
    }
    load_tail = node;
    Lookup_Load_File(name, &node->where);
    record->info = (record->info & ~(FILE_TYPE_MASK << FILE_TYPE_SHIFT)) | (type & FILE_TYPE_MASK) << FILE_TYPE_SHIFT;
    node->record = record;
    node->next = NULL;
}

/* Reads every queued file, into one buffer at `dest` or (dest NULL) each into its own, and runs
 * its handler. `progress` gets 0..0x1000 as sectors arrive. */
void Buffer_Load(uint8_t *dest, void (*progress)(int))
{
    LoadNode *node = load_queue;
    int per_file = dest == NULL;
    load_queue = NULL;
    int32_t total = sectors_queued;
    int32_t left = sectors_queued;
    sectors_queued = 0;
    for (;;) {
        FileLocation where = node->where;
        int32_t sectors = (where.size + SECTOR_SIZE - 1) / SECTOR_SIZE;
        LoadNode *next = node->next;
        FileRecord *record = node->record;
        MPE_free(node);
        CdlLOC loc;
        Sector_To_CdlLOC(where.sector + dirinfo_sector, &loc);
        uint8_t *buf = per_file ? MPE_malloc((size_t)sectors * SECTOR_SIZE) : dest;
        record->data = buf;
        record->info = (record->info & 0xff) | (uint32_t)where.size << FILE_SIZE_SHIFT;
        int ready;
        do {
            for (;;) {
                while (!CdControlB(CdlSeekL, (u_char *)&loc, NULL))
                    CdInit();
                if (CdRead(sectors, (u_long *)buf, CD_READ_DOUBLE_SPEED))
                    break;
                CdInit();
            }
            do {
                ready = CdReadSync(1, NULL);
                if (ready == -1)
                    break;
                int32_t remaining = left - (sectors - ready);
                if (progress)
                    progress(PROGRESS_DONE - remaining * PROGRESS_DONE / total);
            } while (ready != 0);
        } while (ready == -1);
        left -= sectors;
        uint32_t used = dispatch_file_record(record, per_file);
        if (!per_file)
            dest += used;
        node = next;
        if (!node) {
            if (progress)
                progress(PROGRESS_DONE);
            return;
        }
    }
}

/* With `shrink`, the file's own buffer is cut to what the handler keeps. The original ignores
 * realloc's result (it shrinks in place); the C library may move the block, so keep it. */
uint32_t dispatch_file_record(FileRecord *record, int shrink)
{
    FileFunc handler = File_Func_List[(record->info >> FILE_TYPE_SHIFT) & FILE_TYPE_MASK];
    uint32_t used = handler(record->data, record->info >> FILE_SIZE_SHIFT);
    if (shrink) {
        if (used == 0) {
            MPE_free(record->data);
            record->data = NULL;
        } else {
            record->data = MPE_realloc(record->data, used);
        }
    }
    return used;
}

/* A .SBK sound bank: the ADPCM data goes to SPU RAM, the sample table (with SPU addresses) stays. */
uint32_t Load_Sample_Bank(uint8_t *data, uint32_t size)
{
    int32_t count = read_i32(data + SBK_COUNT_OFFSET);
    uint32_t header = SBK_HEADER_SIZE + (uint32_t)count * (uint32_t)sizeof(SampleInfo);
    long spu_addr = SpuMalloc((long)(size - header));
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr((u_long)spu_addr);
    SpuWrite(data + header, size - header);
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    for (int i = 0; i < count; i++) {
        uint8_t *field = data + SBK_HEADER_SIZE + i * (int)sizeof(SampleInfo);
        uint32_t offset = (uint32_t)read_i32(field);
        uint32_t addr = (uint32_t)spu_addr + (offset - header);
        memcpy(field, &addr, sizeof(addr));
    }
    return header;
}

/* FONT.BNK: per font a chunk (first word: its length) whose FONT_GLYPHS glyphs start at +0x10. */
uint32_t load_font_cluts(uint8_t *data, uint32_t size)
{
    (void)size;
    int32_t count = read_i32(data + 0x0c);
    const uint8_t *chunk = data + 0x10;
    for (int font = 0; font < count; font++) {
        memcpy(font_glyphs[font], chunk + 0x10, FONT_GLYPHS * sizeof(Glyph));
        chunk += read_i32(chunk);
    }
    return 0;
}

static int32_t to_bcd(int32_t v)
{
    return (v / 10) * 16 + v % 10;
}

static int32_t from_bcd(uint8_t v)
{
    return ((v & 0xf0) >> 4) * 10 + (v & 0xf);
}

/* Every CD step is retried after re-initialising the drive until it succeeds. */
void Read_Directory(char *name)
{
    CdlFILE file;
    for (;;) {
        for (;;) {
            while (!CdSearchFile(&file, name))
                CdInit();
            if (CdControlB(CdlSeekL, (u_char *)&file.pos, NULL))
                break;
            CdInit();
        }
        if (CdRead(DIR_SECTORS, (u_long *)dirbuf, CD_READ_DOUBLE_SPEED))
            break;
        CdInit();
    }
    while (CdReadSync(1, NULL) > 0)
        ;
    dirinfo_sector = CdlLOC_To_Sector(&file.pos);
}

int32_t File_Load(char *name, void *dest)
{
    FileLocation where;
    CdlLOC loc;
    Lookup_Load_File(name, &where);
    int sectors = (where.size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    Sector_To_CdlLOC(where.sector + dirinfo_sector, &loc);
    do {
        for (;;) {
            while (!CdControlB(CdlSeekL, (u_char *)&loc, NULL))
                CdInit();
            if (CdRead(sectors, dest, CD_READ_DOUBLE_SPEED))
                break;
            CdInit();
        }
    } while (CdReadSync(0, NULL) == -1);
    return where.size;
}

int directory_has_file(char *name)
{
    for (const DirEntry *e = dirbuf; e->name[0] != '\0'; e++)
        if (strcmp(name, e->name) == 0)
            return 1;
    return 0;
}

/* Like the original, assumes the file exists: there is no end-of-table check. */
void Lookup_Load_File(char *name, FileLocation *where)
{
    const DirEntry *e = dirbuf;
    while (strcmp(name, e->name) != 0)
        e++;
    where->sector = e->sector;
    where->size = (int32_t)e->size;
    sectors_queued += ((int32_t)e->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
}

void Sector_To_CdlLOC(int32_t sector, CdlLOC *loc)
{
    loc->minute = (u_char)to_bcd(sector / 4500);
    loc->second = (u_char)to_bcd(sector % 4500 / 75);
    loc->sector = (u_char)to_bcd(sector % 75);
    loc->track = 0;
}

int32_t CdlLOC_To_Sector(CdlLOC *loc)
{
    return from_bcd(loc->minute) * 4500 + from_bcd(loc->second) * 75 + from_bcd(loc->sector);
}
