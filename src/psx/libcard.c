/* libcard and the BIOS memory card file system, on 128 KB raw card images (the format emulators use):
 * frame 0 the "MC" header, frames 1..15 the directory (one per 8 KB block), blocks 1..15 the files.
 * Slot 1's card is always there (a formatted one is made when there is no image yet); slot 2's only
 * when its image exists. Every change is written back to the image at once. */
#include "psx/libcard.h"

#include "platform/platform.h"
#include "psx/libapi.h"

#include <stdio.h>
#include <string.h>

#define CARD_SIZE 0x20000
#define FRAME 0x80
#define BLOCK 0x2000
#define BLOCKS 15

#define DIR_FIRST 0x51  /* in use: the file's first block */
#define DIR_MIDDLE 0x52
#define DIR_LAST 0x53
#define DIR_FREE 0xa0
#define NO_NEXT 0xffff

typedef struct {
    uint8_t image[CARD_SIZE];
    int present;
    char path[512];
    int has_path;
} Card;

static Card cards[2];

static uint8_t *frame(Card *c, int f)
{
    return c->image + f * FRAME;
}

static void checksum(uint8_t *f)
{
    uint8_t x = 0;
    for (int i = 0; i < FRAME - 1; i++)
        x ^= f[i];
    f[FRAME - 1] = x;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void format_image(Card *c)
{
    memset(c->image, 0, CARD_SIZE);
    uint8_t *h = frame(c, 0);
    h[0] = 'M';
    h[1] = 'C';
    checksum(h);
    for (int b = 1; b <= BLOCKS; b++) {
        uint8_t *d = frame(c, b);
        put32(d, DIR_FREE);
        d[8] = d[9] = 0xff;
        checksum(d);
    }
    for (int f = 16; f < 36; f++) {
        uint8_t *d = frame(c, f);
        put32(d, 0xffffffff);
        d[8] = d[9] = 0xff;
        checksum(d);
    }
    memcpy(frame(c, 63), frame(c, 0), FRAME);
}

static void save_image(Card *c)
{
    if (!c->has_path)
        return;
    FILE *f = fopen(c->path, "wb");
    if (!f)
        return;
    fwrite(c->image, 1, CARD_SIZE, f);
    fclose(f);
}

static int formatted(const Card *c)
{
    return c->image[0] == 'M' && c->image[1] == 'C';
}

static int opened;
static int paths_given;

void card_set_image_paths(const char *slot1, const char *slot2)
{
    const char *paths[2] = {slot1, slot2};
    for (int s = 0; s < 2; s++) {
        cards[s].has_path = paths[s] != NULL;
        if (paths[s])
            snprintf(cards[s].path, sizeof(cards[s].path), "%s", paths[s]);
    }
    paths_given = 1;
    opened = 0;
}

/* Loads the images: slot 1 made formatted when it does not exist yet. */
static void open_cards(void)
{
    if (opened)
        return;
    opened = 1;
    for (int s = 0; s < 2; s++) {
        Card *c = &cards[s];
        c->present = 0;
        if (!paths_given)
            c->has_path = platform_card_path(s, c->path, sizeof(c->path));
        FILE *f = c->has_path ? fopen(c->path, "rb") : NULL;
        if (f) {
            c->present = fread(c->image, 1, CARD_SIZE, f) == CARD_SIZE;
            fclose(f);
        } else if (s == 0) {
            format_image(c);
            c->present = 1;
            save_image(c);
        }
    }
}

void InitCARD(long pad_enable)
{
    (void)pad_enable;
    open_cards();
}

long StartCARD(void)
{
    return 1;
}

long StopCARD(void)
{
    return 1;
}

void _bu_init(void)
{
}

long _card_auto(long val)
{
    (void)val;
    return 1;
}

/* chan: 0x00 slot 1, 0x10 slot 2. */
static Card *card_of_chan(long chan)
{
    return (chan & 0x10) ? &cards[1] : &cards[0];
}

long _card_info(long chan)
{
    open_cards();
    DeliverEvent(SwCARD, card_of_chan(chan)->present ? EvSpIOE : EvSpTIMOUT);
    return 1;
}

/* Reading the directory: an unformatted card answers as a new one. */
long _card_load(long chan)
{
    open_cards();
    const Card *c = card_of_chan(chan);
    if (!c->present)
        DeliverEvent(SwCARD, EvSpTIMOUT);
    else
        DeliverEvent(SwCARD, formatted(c) ? EvSpIOE : EvSpNEW);
    return 1;
}

long _card_clear(long chan)
{
    (void)chan;
    DeliverEvent(HwCARD, EvSpIOE);
    return 1;
}

/* ---- the file system ---- */

/* "bu00:NAME" or "bu10:NAME": the card and the name after the colon. */
static Card *card_of_name(const char *name, const char **file)
{
    open_cards();
    if (strncmp(name, "bu", 2) != 0 || (name[2] != '0' && name[2] != '1') || name[3] != '0' || name[4] != ':')
        return NULL;
    Card *c = &cards[name[2] - '0'];
    if (!c->present || !formatted(c))
        return NULL;
    *file = name + 5;
    return c;
}

/* The directory frame of the file's first block, or 0. */
static int find_file(Card *c, const char *file)
{
    for (int b = 1; b <= BLOCKS; b++) {
        const uint8_t *d = frame(c, b);
        if (get32(d) == DIR_FIRST && strncmp((const char *)d + 0x0a, file, 20) == 0)
            return b;
    }
    return 0;
}

#define MAX_FILES 4

static struct {
    Card *card;
    int first;      /* directory frame of the first block */
    int pos;        /* byte position */
    int size;
    int used;
} files[MAX_FILES];

/* The data block holding byte `pos` of the file, following the chain. */
static int block_at(Card *c, int first, int pos)
{
    int b = first;
    for (int skip = pos / BLOCK; skip > 0 && b != 0; skip--) {
        uint16_t next = (uint16_t)(frame(c, b)[8] | frame(c, b)[9] << 8);
        b = next == NO_NEXT ? 0 : next + 1;
    }
    return b;
}

static int create_file(Card *c, const char *file, int blocks)
{
    if (blocks < 1 || find_file(c, file))
        return 0;
    int chain[BLOCKS], n = 0;
    for (int b = 1; b <= BLOCKS && n < blocks; b++)
        if ((get32(frame(c, b)) & 0xf0) == DIR_FREE)
            chain[n++] = b;
    if (n < blocks)
        return 0;
    for (int i = 0; i < n; i++) {
        uint8_t *d = frame(c, chain[i]);
        memset(d, 0, FRAME);
        put32(d, i == 0 ? DIR_FIRST : i == n - 1 ? DIR_LAST : DIR_MIDDLE);
        if (i == 0) {
            put32(d + 4, (uint32_t)(n * BLOCK));
            strncpy((char *)d + 0x0a, file, 20);
        }
        uint16_t next = i == n - 1 ? NO_NEXT : (uint16_t)(chain[i + 1] - 1);
        d[8] = (uint8_t)next;
        d[9] = (uint8_t)(next >> 8);
        checksum(d);
        memset(c->image + chain[i] * BLOCK, 0, BLOCK);
    }
    save_image(c);
    return chain[0];
}

int open(const char *name, int flags)
{
    const char *file;
    Card *c = card_of_name(name, &file);
    if (!c)
        return -1;
    int first;
    if (flags & FCREAT)
        first = create_file(c, file, flags >> 16);
    else
        first = find_file(c, file);
    if (!first)
        return -1;
    for (int fd = 0; fd < MAX_FILES; fd++) {
        if (!files[fd].used) {
            files[fd].card = c;
            files[fd].first = first;
            files[fd].pos = 0;
            files[fd].size = (int)get32(frame(c, first) + 4);
            files[fd].used = 1;
            return fd;
        }
    }
    return -1;
}

static int transfer(int fd, uint8_t *in, const uint8_t *out, int n)
{
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used || n < 0 || n % FRAME != 0)
        return -1;
    Card *c = files[fd].card;
    int done = 0;
    while (done < n && files[fd].pos < files[fd].size) {
        int b = block_at(c, files[fd].first, files[fd].pos);
        if (b == 0)
            break;
        int at = files[fd].pos % BLOCK;
        int chunk = BLOCK - at;
        if (chunk > n - done)
            chunk = n - done;
        uint8_t *data = c->image + b * BLOCK + at;
        if (in)
            memcpy(in + done, data, (size_t)chunk);
        else
            memcpy(data, out + done, (size_t)chunk);
        done += chunk;
        files[fd].pos += chunk;
    }
    if (out && done > 0)
        save_image(c);
    return done;
}

int read(int fd, void *buf, int n)
{
    return transfer(fd, buf, NULL, n);
}

int write(int fd, const void *buf, int n)
{
    return transfer(fd, NULL, buf, n);
}

int close(int fd)
{
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used)
        return -1;
    files[fd].used = 0;
    return fd;
}

/* Frees the file's blocks (marked deleted, as the BIOS does). */
int delete(const char *name)
{
    const char *file;
    Card *c = card_of_name(name, &file);
    int b = c ? find_file(c, file) : 0;
    if (!b)
        return 0;
    while (b != 0) {
        uint8_t *d = frame(c, b);
        uint16_t next = (uint16_t)(d[8] | d[9] << 8);
        put32(d, (get32(d) & 0x0f) | DIR_FREE);
        checksum(d);
        b = next == NO_NEXT ? 0 : next + 1;
    }
    save_image(c);
    return 1;
}

int format(const char *device)
{
    open_cards();
    if (strncmp(device, "bu", 2) != 0 || (device[2] != '0' && device[2] != '1'))
        return 0;
    Card *c = &cards[device[2] - '0'];
    if (!c->present)
        return 0;
    format_image(c);
    save_image(c);
    return 1;
}

/* Directory listing: every file (the game only asks for "*"). */
static Card *list_card;
static int list_next;

static struct DIRENTRY *list_from(struct DIRENTRY *dir)
{
    for (; list_next <= BLOCKS; list_next++) {
        const uint8_t *d = frame(list_card, list_next);
        if (get32(d) != DIR_FIRST)
            continue;
        memset(dir, 0, sizeof(*dir));
        memcpy(dir->name, d + 0x0a, 20);
        dir->name[19] = '\0';
        dir->attr = (long)get32(d);
        dir->size = (long)get32(d + 4);
        dir->head = list_next;
        list_next++;
        return dir;
    }
    return NULL;
}

struct DIRENTRY *firstfile(const char *pattern, struct DIRENTRY *dir)
{
    const char *file;
    list_card = card_of_name(pattern, &file);
    list_next = 1;
    return list_card ? list_from(dir) : NULL;
}

struct DIRENTRY *nextfile(struct DIRENTRY *dir)
{
    return list_card ? list_from(dir) : NULL;
}
