/* Checks of the SPU emulation against psx-spx rules: the ADSR steps, ADPCM decoding and the
 * loop flags. The expected envelope values are worked out by hand from the psx-spx formula. */
#include "psx/libspu.h"

#include <stdio.h>
#include <stdlib.h>
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

#define SAMPLE_ADDR 0x2000

static short envelope(int voice)
{
    short envx;
    SpuGetVoiceEnvelope(voice, &envx);
    return envx;
}

static void mix(int frames, short *out)
{
    short scratch[64 * 2];
    spu_mix(out ? out : scratch, frames);
}

/* A block of 28 samples of `nibble` at shift 0 (nibble << 12) with filter 0. */
static void write_block(u_long addr, int nibble, int flags)
{
    u_char block[16];
    block[0] = 0x00;
    block[1] = (u_char)flags;
    memset(block + 2, nibble | nibble << 4, 14);
    SpuSetTransferStartAddr(addr);
    SpuWrite(block, sizeof(block));
}

static void start_voice(int voice, u_long addr, int pitch)
{
    SpuVoiceAttr attr = {0};
    attr.voice = 1u << voice;
    attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH | SPU_VOICE_WDSA;
    attr.volume.left = 0x3fff;
    attr.volume.right = 0x3fff;
    attr.pitch = (u_short)pitch;
    attr.addr = addr;
    SpuSetVoiceAttr(&attr);
    SpuSetKey(SPU_ON, attr.voice);
}

static void set_main_volume(void)
{
    SpuCommonAttr common = {0};
    common.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR;
    common.mvol.left = 0x3fff;
    common.mvol.right = 0x3fff;
    SpuSetCommonAttr(&common);
}

/* ADSR1 = ADSR2 = 0, as SpuInit leaves them: attack +7<<11 per sample, decay to 800h, sustain back
 * up at +7<<11, release -8<<11. */
static void test_envelope_zero(void)
{
    static const short want[10] = {14336, 28672, 32767, 16384, 8192, 4096, 2048, 16384, 30720, 32767};
    SpuInit();
    start_voice(0, 0x1000, 0x1000);
    for (int i = 0; i < 10; i++) {
        mix(1, NULL);
        CHECK(envelope(0) == want[i], "envelope after %d samples: %d, want %d", i + 1, envelope(0), want[i]);
    }
    SpuSetKey(SPU_OFF, 1);
    mix(1, NULL);
    CHECK(envelope(0) == 16383, "release step: %d, want 16383", envelope(0));
    mix(1, NULL);
    CHECK(envelope(0) == 0, "release to zero: %d", envelope(0));
}

/* A looping block of constant 1000h plays as 1000h, give or take the Gaussian table's 7F80h/8000h
 * and the volume steps. */
static void test_looping_block(void)
{
    SpuInit();
    set_main_volume();
    write_block(SAMPLE_ADDR, 1, 7);
    start_voice(1, SAMPLE_ADDR, 0x1000);
    short out[64 * 2];
    mix(64, out);
    for (int i = 16; i < 64; i++)
        CHECK(abs(out[i * 2] - 0x1000) < 0x20 && out[i * 2] == out[i * 2 + 1], "sample %d: %d %d", i, out[i * 2],
              out[i * 2 + 1]);
}

/* A block with Loop End but not Loop Repeat ends the voice: envelope zero after 28 samples. */
static void test_one_shot(void)
{
    SpuInit();
    write_block(SAMPLE_ADDR, 1, 1);
    start_voice(2, SAMPLE_ADDR, 0x1000);
    mix(27, NULL);
    CHECK(envelope(2) != 0, "still playing in its block");
    mix(2, NULL);
    CHECK(envelope(2) == 0, "envelope after the end: %d", envelope(2));
}

/* At pitch 2000h a block of 28 samples lasts 14 output samples: the second block's value shows up
 * after that. */
static void test_pitch(void)
{
    SpuInit();
    set_main_volume();
    write_block(SAMPLE_ADDR, 1, 0);
    write_block(SAMPLE_ADDR + 16, 3, 7);
    start_voice(3, SAMPLE_ADDR, 0x2000);
    short out[32 * 2];
    mix(32, out);
    CHECK(abs(out[12 * 2] - 0x1000) < 0x20, "first block at 12: %d", out[12 * 2]);
    CHECK(abs(out[20 * 2] - 0x3000) < 0x40, "second block at 20: %d", out[20 * 2]);
}

int main(void)
{
    test_envelope_zero();
    test_looping_block();
    test_one_shot();
    test_pitch();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("all SPU checks passed\n");
    return failures != 0;
}
