/* Checks of the MDEC decoder: a one-macroblock bitstream of DC values, and an AC code from each length
 * of the coefficient table, decoded to the colours they stand for. */
#include "psx/libpress.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

/* Writes bits top first into little-endian halfwords, as the .STR bitstream holds them. */
typedef struct {
    unsigned char *out;
    int bit;
} BitWriter;

static void put_bits(BitWriter *w, unsigned value, int n)
{
    for (int i = n - 1; i >= 0; i--, w->bit++) {
        int half = w->bit / 16, pos = 15 - w->bit % 16;
        if (value >> i & 1)
            w->out[half * 2 + pos / 8] |= (unsigned char)(1 << (pos % 8));
    }
}

/* A frame header and one macroblock: Cr, Cb and the four luma blocks with these DC values, the
 * first luma block also given the AC codes in `ac` (bit strings with their sign). */
static void make_frame(u_long *bs, const int dc[6], const char *const *ac, int ac_count, int halves)
{
    unsigned char *p = (unsigned char *)bs;
    memset(bs, 0, 256);
    p[0] = (unsigned char)(halves / 2);
    p[2] = 0x00;
    p[3] = 0x38;
    p[4] = 1; /* quantiser scale */
    p[6] = 2; /* version */
    BitWriter w = {p + 8, 0};
    for (int b = 0; b < 6; b++) {
        put_bits(&w, (unsigned)dc[b] & 0x3ff, 10);
        for (int i = 0; b == 2 && i < ac_count; i++)
            for (const char *c = ac[i]; *c; c++)
                put_bits(&w, (unsigned)(*c - '0'), 1);
        put_bits(&w, 2, 2); /* end of block */
    }
}

int main(void)
{
    static u_long bs[64], codes[256];
    static unsigned char pixels[16 * 16 * 3];
    DecDCTReset(0);

    /* Luma DC 40 dequantises to 80, an eighth of which is every sample: 128 + 10. */
    const int grey[6] = {0, 0, 40, 40, 40, 40};
    make_frame(bs, grey, NULL, 0, 12);
    CHECK(DecDCTvlc(bs, codes) == 0, "decodes a version 2 frame");
    const u_short *rl = (const u_short *)(codes + 1);
    CHECK(rl[0] == (1 << 10 | 0) && rl[1] == 0xfe00 && rl[4] == (1 << 10 | 40), "DC then end of block (%04x %04x)",
          rl[0], rl[1]);
    DecDCTin(codes, 3);
    DecDCTout((u_long *)pixels, 16 * 16 * 3 / 4);
    int flat = 1;
    for (int i = 0; i < 16 * 16 * 3; i++)
        flat &= pixels[i] == 138;
    CHECK(flat, "a flat grey macroblock (%d %d %d)", pixels[0], pixels[1], pixels[2]);

    /* Cr alone: red up by 1.402 Cr, green down by 0.7143 Cr, blue unchanged. */
    const int red[6] = {80, 0, 0, 0, 0, 0};
    make_frame(bs, red, NULL, 0, 12);
    DecDCTvlc(bs, codes);
    DecDCTin(codes, 3);
    DecDCTout((u_long *)pixels, 16 * 16 * 3 / 4);
    CHECK(pixels[0] == 128 + 28 && pixels[1] == 128 - 15 && pixels[2] == 128, "Cr 20 (%d %d %d)", pixels[0],
          pixels[1], pixels[2]);

    /* The longest codes and the escape decode to their run and level. */
    static const char *const ac[] = {"0000000000011011" "1", "000000000011001" "0", "000001" "000010" "1111111110"};
    make_frame(bs, grey, ac, 3, 16);
    CHECK(DecDCTvlc(bs, codes) == 0, "long codes");
    CHECK(rl[5] == (31 << 10 | 0x3ff), "run 31, level -1 (%04x)", rl[5]);
    CHECK(rl[6] == (1 << 10 | 14), "run 1, level 14 (%04x)", rl[6]);
    CHECK(rl[7] == (2 << 10 | 0x3fe), "escape: run 2, level -2 (%04x)", rl[7]);

    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
