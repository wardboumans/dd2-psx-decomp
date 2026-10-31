#include "psx/libpress.h"

#include <stdbool.h>
#include <string.h>

/* ---- DecDCTvlc: the bitstream ---- */

/* A frame: u16 run-level halfwords / 2, u16 0x3800 (the MDEC command), u16 quantiser scale, u16 version,
 * then the bitstream as little-endian halfwords read from the top bit down. Each block is a 10-bit DC
 * value, then AC codes from the MPEG-1 coefficient table (with the PSX escape: 000001, 6-bit run,
 * 10-bit level) up to the end-of-block code 10. */

#define EOB_CODE 0xfe00
#define VLC_BITS 16

typedef struct {
    const char *code; /* without the sign bit that follows */
    uint8_t run;
    uint8_t level;
} VlcCode;

static const VlcCode ac_codes[] = {
    {"11", 0, 1}, {"011", 1, 1}, {"0100", 0, 2}, {"0101", 2, 1}, {"00101", 0, 3}, {"00111", 3, 1},
    {"00110", 4, 1}, {"000110", 1, 2}, {"000111", 5, 1}, {"000101", 6, 1}, {"000100", 7, 1},
    {"0000110", 0, 4}, {"0000100", 2, 2}, {"0000111", 8, 1}, {"0000101", 9, 1},
    {"00100110", 0, 5}, {"00100001", 0, 6}, {"00100101", 1, 3}, {"00100100", 3, 2}, {"00100111", 10, 1},
    {"00100011", 11, 1}, {"00100010", 12, 1}, {"00100000", 13, 1},
    {"0000001010", 0, 7}, {"0000001100", 1, 4}, {"0000001011", 2, 3}, {"0000001111", 4, 2},
    {"0000001001", 5, 2}, {"0000001110", 14, 1}, {"0000001101", 15, 1}, {"0000001000", 16, 1},
    {"000000011101", 0, 8}, {"000000011000", 0, 9}, {"000000010011", 0, 10}, {"000000010000", 0, 11},
    {"000000011011", 1, 5}, {"000000010100", 2, 4}, {"000000011100", 3, 3}, {"000000010010", 4, 3},
    {"000000011110", 6, 2}, {"000000010101", 7, 2}, {"000000010001", 8, 2}, {"000000011111", 17, 1},
    {"000000011010", 18, 1}, {"000000011001", 19, 1}, {"000000010111", 20, 1}, {"000000010110", 21, 1},
    {"0000000011010", 0, 12}, {"0000000011001", 0, 13}, {"0000000011000", 0, 14}, {"0000000010111", 0, 15},
    {"0000000010110", 1, 6}, {"0000000010101", 1, 7}, {"0000000010100", 2, 5}, {"0000000010011", 3, 4},
    {"0000000010010", 5, 3}, {"0000000010001", 9, 2}, {"0000000010000", 10, 2}, {"0000000011111", 22, 1},
    {"0000000011110", 23, 1}, {"0000000011101", 24, 1}, {"0000000011100", 25, 1}, {"0000000011011", 26, 1},
    {"00000000011111", 0, 16}, {"00000000011110", 0, 17}, {"00000000011101", 0, 18},
    {"00000000011100", 0, 19}, {"00000000011011", 0, 20}, {"00000000011010", 0, 21},
    {"00000000011001", 0, 22}, {"00000000011000", 0, 23}, {"00000000010111", 0, 24},
    {"00000000010110", 0, 25}, {"00000000010101", 0, 26}, {"00000000010100", 0, 27},
    {"00000000010011", 0, 28}, {"00000000010010", 0, 29}, {"00000000010001", 0, 30},
    {"00000000010000", 0, 31},
    {"000000000011000", 0, 32}, {"000000000010111", 0, 33}, {"000000000010110", 0, 34},
    {"000000000010101", 0, 35}, {"000000000010100", 0, 36}, {"000000000010011", 0, 37},
    {"000000000010010", 0, 38}, {"000000000010001", 0, 39}, {"000000000010000", 0, 40},
    {"000000000011111", 1, 8}, {"000000000011110", 1, 9}, {"000000000011101", 1, 10},
    {"000000000011100", 1, 11}, {"000000000011011", 1, 12}, {"000000000011010", 1, 13},
    {"000000000011001", 1, 14},
    {"0000000000010011", 1, 15}, {"0000000000010010", 1, 16}, {"0000000000010001", 1, 17},
    {"0000000000010000", 1, 18}, {"0000000000010100", 6, 3}, {"0000000000011010", 11, 2},
    {"0000000000011001", 12, 2}, {"0000000000011000", 13, 2}, {"0000000000010111", 14, 2},
    {"0000000000010110", 15, 2}, {"0000000000010101", 16, 2}, {"0000000000011111", 27, 1},
    {"0000000000011110", 28, 1}, {"0000000000011101", 29, 1}, {"0000000000011100", 30, 1},
    {"0000000000011011", 31, 1},
};

enum { VLC_INVALID, VLC_COEFF, VLC_EOB, VLC_ESCAPE };

typedef struct {
    uint8_t kind;
    uint8_t length; /* without the sign bit */
    uint8_t run;
    uint8_t level;
} VlcEntry;

static VlcEntry vlc_table[1 << VLC_BITS]; /* by the next 16 bits */
static bool vlc_ready;

static void vlc_fill(const char *code, VlcEntry entry)
{
    int length = (int)strlen(code);
    unsigned value = 0;
    for (int i = 0; i < length; i++)
        value = value << 1 | (unsigned)(code[i] - '0');
    entry.length = (uint8_t)length;
    unsigned first = value << (VLC_BITS - length), count = 1u << (VLC_BITS - length);
    for (unsigned i = 0; i < count; i++)
        vlc_table[first + i] = entry;
}

static void vlc_init(void)
{
    for (size_t i = 0; i < sizeof(ac_codes) / sizeof(ac_codes[0]); i++)
        vlc_fill(ac_codes[i].code, (VlcEntry){VLC_COEFF, 0, ac_codes[i].run, ac_codes[i].level});
    vlc_fill("10", (VlcEntry){VLC_EOB, 0, 0, 0});
    vlc_fill("000001", (VlcEntry){VLC_ESCAPE, 0, 0, 0});
    vlc_ready = true;
}

typedef struct {
    const uint8_t *data;
    uint32_t bits;   /* the next bits, top aligned */
    int count;       /* valid bits in `bits` */
} BitReader;

static void bits_fill(BitReader *br)
{
    while (br->count <= 16) {
        uint32_t half = (uint32_t)(br->data[0] | br->data[1] << 8);
        br->data += 2;
        br->bits |= half << (16 - br->count);
        br->count += 16;
    }
}

static unsigned bits_peek(BitReader *br, int n)
{
    bits_fill(br);
    return br->bits >> (32 - n);
}

static unsigned bits_read(BitReader *br, int n)
{
    unsigned v = bits_peek(br, n);
    br->bits <<= n;
    br->count -= n;
    return v;
}

/* buf receives the header word, then the run-level halfwords. Returns 0, or -1 for a bitstream
 * the decoder cannot read (another version, or a bad code). */
int DecDCTvlc(u_long *bs, u_long *buf)
{
    if (!vlc_ready)
        vlc_init();
    const uint8_t *in = (const uint8_t *)bs;
    unsigned halves = (unsigned)(in[0] | in[1] << 8) * 2;
    unsigned qscale = (unsigned)(in[4] | in[5] << 8) & 0x3f;
    unsigned version = (unsigned)(in[6] | in[7] << 8);
    buf[0] = bs[0];
    if (version != 1 && version != 2)
        return -1;
    u_short *out = (u_short *)(buf + 1);
    BitReader br = {in + 8, 0, 0};
    unsigned n = 0;
    while (n < halves) {
        out[n++] = (u_short)(qscale << 10 | bits_read(&br, 10));
        while (n < halves) {
            const VlcEntry *e = &vlc_table[bits_peek(&br, VLC_BITS)];
            if (e->kind == VLC_INVALID)
                return -1;
            if (e->kind == VLC_EOB) {
                bits_read(&br, 2);
                out[n++] = EOB_CODE;
                break;
            }
            if (e->kind == VLC_ESCAPE) {
                bits_read(&br, 6);
                out[n++] = (u_short)bits_read(&br, 16);
                continue;
            }
            bits_read(&br, e->length);
            int level = bits_read(&br, 1) ? -e->level : e->level;
            out[n++] = (u_short)(e->run << 10 | ((unsigned)level & 0x3ff));
        }
    }
    return 0;
}

/* ---- the MDEC ---- */

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55,
    62, 63,
};

/* The quantisation table DecDCTReset loads (natural order): MPEG-1's intra matrix with a DC of 2. */
static const uint8_t quant_matrix[64] = {
    2, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

/* The IDCT matrix DecDCTReset loads: 0x8000 * c(k) * cos((2n + 1) k pi / 16), rounded down. */
static const int16_t idct_matrix[8][8] = {
    {0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82},
    {0x7d8a, 0x6a6d, 0x471c, 0x18f8, -0x18f9, -0x471d, -0x6a6e, -0x7d8b},
    {0x7641, 0x30fb, -0x30fc, -0x7642, -0x7642, -0x30fc, 0x30fb, 0x7641},
    {0x6a6d, -0x18f9, -0x7d8b, -0x471d, 0x471c, 0x7d8a, 0x18f8, -0x6a6e},
    {0x5a82, -0x5a83, -0x5a83, 0x5a82, 0x5a82, -0x5a83, -0x5a83, 0x5a82},
    {0x471c, -0x7d8b, 0x18f8, 0x6a6d, -0x6a6e, -0x18f9, 0x7d8a, -0x471d},
    {0x30fb, -0x7642, 0x7641, -0x30fc, -0x30fc, 0x7641, -0x7642, 0x30fb},
    {0x18f8, -0x471d, 0x6a6d, -0x7d8b, 0x7d8a, -0x6a6e, 0x471c, -0x18f9},
};

static const u_short *mdec_in, *mdec_in_end;
static int mdec_mode;
static void (*out_callback)(void);

void DecDCTReset(int mode)
{
    (void)mode;
    mdec_in = mdec_in_end = NULL;
    out_callback = NULL;
}

int DecDCToutCallback(void (*func)(void))
{
    out_callback = func;
    return 0;
}

void DecDCTin(u_long *buf, int mode)
{
    mdec_in = (const u_short *)(buf + 1);
    mdec_in_end = mdec_in + (buf[0] & 0xffff) * 2;
    mdec_mode = mode;
}

/* The next halfword; an end-of-block once the input is used up. */
static u_short next_code(void)
{
    return mdec_in < mdec_in_end ? *mdec_in++ : EOB_CODE;
}

static int signed10(unsigned v)
{
    return (int)(v & 0x3ff) - (int)((v & 0x200) << 1);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* One block's coefficients (natural order), dequantised as the MDEC does. */
static void rl_decode_block(int32_t blk[64])
{
    memset(blk, 0, 64 * sizeof(int32_t));
    u_short n = next_code();
    while (n == EOB_CODE && mdec_in < mdec_in_end)
        n = next_code();
    int qscale = n >> 10;
    int k = 0;
    int val = signed10(n) * quant_matrix[0];
    for (;;) {
        if (qscale == 0)
            val = signed10(n) * 2;
        val = clamp(val, -0x400, 0x3ff);
        if (qscale > 0)
            blk[zigzag[k]] = val;
        else
            blk[k] = val;
        n = next_code();
        k += (n >> 10) + 1;
        if (k > 63)
            break;
        val = (signed10(n) * quant_matrix[zigzag[k]] * qscale + 4) / 8;
    }
}

/* The 2-D inverse DCT in place; results are the samples, centred on 0. */
static void idct_block(int32_t blk[64])
{
    int32_t tmp[64];
    for (int v = 0; v < 8; v++) {
        for (int x = 0; x < 8; x++) {
            int32_t sum = 0;
            for (int u = 0; u < 8; u++)
                sum += blk[v * 8 + u] * idct_matrix[u][x];
            tmp[v * 8 + x] = (sum + (1 << 12)) >> 13;
        }
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int64_t sum = 0;
            for (int v = 0; v < 8; v++)
                sum += (int64_t)tmp[v * 8 + x] * idct_matrix[v][y];
            blk[y * 8 + x] = (int32_t)((sum + (1 << 18)) >> 19);
        }
    }
}

/* One macroblock (Cr, Cb, then the four luma blocks) as 16 rows of 16 pixels. */
static void decode_macroblock(uint8_t *out)
{
    int32_t cr[64], cb[64], y[4][64];
    rl_decode_block(cr);
    idct_block(cr);
    rl_decode_block(cb);
    idct_block(cb);
    for (int i = 0; i < 4; i++) {
        rl_decode_block(y[i]);
        idct_block(y[i]);
    }
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            int c = (py / 2) * 8 + px / 2;
            int r_off = (cr[c] * 359) >> 8;              /* 1.402 Cr */
            int g_off = (-(cb[c] * 88) - (cr[c] * 183)) >> 8; /* -0.3437 Cb - 0.7143 Cr */
            int b_off = (cb[c] * 454) >> 8;              /* 1.772 Cb */
            int luma = y[(py / 8) * 2 + px / 8][(py % 8) * 8 + px % 8];
            int r = clamp(luma + r_off, -128, 127) + 128;
            int g = clamp(luma + g_off, -128, 127) + 128;
            int b = clamp(luma + b_off, -128, 127) + 128;
            if (mdec_mode & 1) {
                uint8_t *p = out + (py * 16 + px) * 3;
                p[0] = (uint8_t)r;
                p[1] = (uint8_t)g;
                p[2] = (uint8_t)b;
            } else {
                u_short pixel = (u_short)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | (mdec_mode & 2 ? 0x8000 : 0));
                uint8_t *p = out + (py * 16 + px) * 2;
                p[0] = (uint8_t)pixel;
                p[1] = (uint8_t)(pixel >> 8);
            }
        }
    }
}

void DecDCTout(u_long *buf, int size)
{
    int block_bytes = (mdec_mode & 1) ? 16 * 16 * 3 : 16 * 16 * 2;
    uint8_t *out = (uint8_t *)buf;
    for (int done = 0; done + block_bytes <= size * 4; done += block_bytes)
        decode_macroblock(out + done);
    if (out_callback)
        out_callback();
}
