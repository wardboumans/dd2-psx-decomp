#include "psx/libspu.h"

#include "platform/platform.h"
#include "psx/libcd.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* An SPU emulation behind the Psy-Q calls, following psx-spx ("Sound Processing Unit"): voices
 * decode SPU-ADPCM from SPU RAM at their pitch with the 4-point Gaussian interpolation, through
 * the ADSR envelope and their volumes, mixed with the CD input at 44.1 kHz. Reverb, noise and
 * pitch modulation are not emulated yet (the game does not turn them on so far). */

#define SPU_RATE 44100
#define MAX_VBLANK_FRAMES 800
#define VOICES 24
#define BLOCK_SAMPLES 28
#define BLOCK_BYTES 16

/* The first 0x1010 bytes of SPU RAM are reserved (capture buffers and the SPU's own data). */
#define SPU_RAM_RESERVED 0x1010
#define DUMMY_BLOCK 0x1000 /* where _spu_init puts a silent looping block for idle voices */

/* ADPCM header flags */
#define FLAG_LOOP_END 1
#define FLAG_LOOP_REPEAT 2
#define FLAG_LOOP_START 4

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    /* registers */
    uint16_t volume[2];  /* VOLL/VOLR, direct mode: volume / 2 */
    uint16_t pitch;
    uint16_t start;      /* in 8-byte units */
    uint16_t adsr1, adsr2;
    uint16_t repeat;     /* LSAX */
    /* state */
    uint32_t address;    /* current block, in 8-byte units */
    uint32_t counter;    /* pitch counter: bit 12 up the sample in the block, bits 4..11 interpolation */
    int16_t samples[3 + BLOCK_SAMPLES]; /* the previous block's last 3, then this block's */
    int16_t history[2];  /* ADPCM filter: last and second to last decoded */
    uint8_t flags;       /* the current block's header flags */
    bool decoded;
    int phase;           /* ENV_* */
    int32_t level;       /* ADSR volume, 0..7FFFh */
    uint32_t env_counter;
} Voice;

extern const int16_t spu_gauss[512];

static u_char ram[SPU_RAM_SIZE];
static u_long malloc_next;
static u_long transfer_addr;
static SpuCommonAttr common;
static Voice voices[VOICES];
static uint32_t endx;
static int64_t vblanks_mixed;

const u_char *spu_ram(void)
{
    return ram;
}

/* As _spu_init: voices silent at pitch 3FFFh on the dummy block, envelopes zero. */
void SpuInit(void)
{
    static const u_char dummy[BLOCK_BYTES] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}; /* 0x800789A0 */
    memset(ram, 0, sizeof(ram));
    memcpy(ram + DUMMY_BLOCK, dummy, sizeof(dummy));
    memset(voices, 0, sizeof(voices));
    for (int v = 0; v < VOICES; v++) {
        voices[v].pitch = 0x3fff;
        voices[v].start = DUMMY_BLOCK / 8;
    }
    endx = 0;
    malloc_next = SPU_RAM_RESERVED;
}

/* Psy-Q keeps a block table in `top`; the game never frees SPU memory, so a bump allocator does. */
/* Releases the SPU's transfer callback and event; the port has neither. */
void SpuQuit(void)
{
}

long SpuInitMalloc(long num, char *top)
{
    (void)top;
    malloc_next = SPU_RAM_RESERVED;
    return num;
}

long SpuMalloc(long size)
{
    size = (size + 7) & ~7L;
    if (malloc_next + (u_long)size > SPU_RAM_SIZE)
        return -1;
    long addr = (long)malloc_next;
    malloc_next += (u_long)size;
    return addr;
}

void SpuSetCommonAttr(SpuCommonAttr *attr)
{
    if (attr->mask & SPU_COMMON_MVOLL)
        common.mvol.left = attr->mvol.left;
    if (attr->mask & SPU_COMMON_MVOLR)
        common.mvol.right = attr->mvol.right;
    if (attr->mask & SPU_COMMON_CDVOLL)
        common.cd.volume.left = attr->cd.volume.left;
    if (attr->mask & SPU_COMMON_CDVOLR)
        common.cd.volume.right = attr->cd.volume.right;
}

long SpuSetReverb(long on_off)
{
    return on_off;
}

long SpuSetTransferMode(long mode)
{
    return mode;
}

u_long SpuSetTransferStartAddr(u_long addr)
{
    transfer_addr = addr;
    return addr;
}

u_long SpuWrite(u_char *addr, u_long size)
{
    if (transfer_addr >= SPU_RAM_SIZE)
        return 0;
    if (size > SPU_RAM_SIZE - transfer_addr)
        size = SPU_RAM_SIZE - transfer_addr;
    memcpy(ram + transfer_addr, addr, size);
    return size;
}

long SpuIsTransferCompleted(long flag)
{
    (void)flag;
    return 1;
}

/* Only the volume modes the game uses are emulated: direct volume (mode 0). */
static uint16_t volume_register(short volume, short mode)
{
    static bool warned;
    if (mode != 0 && !warned) {
        fprintf(stderr, "libspu: volume mode %d is played as direct volume\n", mode);
        warned = true;
    }
    return (uint16_t)(volume & 0x7fff);
}

/* Writes the registers the mask selects into every voice the attribute names. */
void SpuSetVoiceAttr(SpuVoiceAttr *attr)
{
    for (int i = 0; i < VOICES; i++) {
        if (!(attr->voice & (1u << i)))
            continue;
        Voice *v = &voices[i];
        if (attr->mask & SPU_VOICE_VOLL)
            v->volume[0] = volume_register(attr->volume.left, attr->volmode.left);
        if (attr->mask & SPU_VOICE_VOLR)
            v->volume[1] = volume_register(attr->volume.right, attr->volmode.right);
        if (attr->mask & SPU_VOICE_PITCH)
            v->pitch = attr->pitch;
        if (attr->mask & SPU_VOICE_WDSA)
            v->start = (uint16_t)(attr->addr >> 3);
        if (attr->mask & SPU_VOICE_LSAX)
            v->repeat = (uint16_t)(attr->loop_addr >> 3);
        if (attr->mask & SPU_VOICE_ADSR_ADSR1)
            v->adsr1 = attr->adsr1;
        if (attr->mask & SPU_VOICE_ADSR_ADSR2)
            v->adsr2 = attr->adsr2;
    }
}

/* Key on restarts the sample with the envelope at zero; key off starts the release. */
void SpuSetKey(long on_off, u_long voice_bit)
{
    for (int i = 0; i < VOICES; i++) {
        if (!(voice_bit & (1u << i)))
            continue;
        Voice *v = &voices[i];
        if (on_off == SPU_ON) {
            v->address = v->start;
            v->counter = 0;
            v->decoded = false;
            memset(v->samples, 0, sizeof(v->samples));
            v->history[0] = v->history[1] = 0;
            v->phase = ENV_ATTACK;
            v->level = 0;
            v->env_counter = 0;
            endx &= ~(1u << i);
        } else if (v->phase != ENV_OFF) {
            v->phase = ENV_RELEASE;
            v->env_counter = 0;
        }
    }
}

static int16_t clamp16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* Decodes the block at the voice's address; a Loop Start flag makes it the repeat address. Shift
 * values 13..15 act as 9 and filters past 4 as 4 (as DuckStation; psx-spx leaves them open). */
static void decode_block(Voice *v)
{
    static const int filter_pos[5] = {0, 60, 115, 98, 122};
    static const int filter_neg[5] = {0, 0, -52, -55, -60};
    const u_char *block = &ram[(v->address * 8) & (SPU_RAM_SIZE - 1)];
    int shift = block[0] & 0x0f;
    int filter = (block[0] >> 4) & 7;
    if (shift > 12)
        shift = 9;
    if (filter > 4)
        filter = 4;
    v->flags = block[1];
    if (v->flags & FLAG_LOOP_START)
        v->repeat = (uint16_t)v->address;
    memmove(v->samples, v->samples + BLOCK_SAMPLES, 3 * sizeof(int16_t));
    for (int i = 0; i < BLOCK_SAMPLES; i++) {
        int nibble = (block[2 + i / 2] >> ((i & 1) * 4)) & 0xf;
        int32_t t = (int32_t)((uint32_t)nibble << 28) >> 28;
        int32_t s = t * (1 << (12 - shift)) +
                    (v->history[0] * filter_pos[filter] + v->history[1] * filter_neg[filter] + 32) / 64;
        int16_t sample = clamp16(s);
        v->history[1] = v->history[0];
        v->history[0] = sample;
        v->samples[3 + i] = sample;
    }
    v->decoded = true;
}

/* One envelope step from psx-spx "Envelope Operation depending on Shift/Step/Mode/Direction". */
static void envelope_tick(Voice *v, bool exponential, bool decrease, int shift, int step)
{
    int32_t adsr_step = 7 - step;
    if (decrease)
        adsr_step = ~adsr_step;
    adsr_step *= 1 << (shift < 11 ? 11 - shift : 0);
    uint32_t increment = 0x8000u >> (shift > 11 ? shift - 11 : 0);
    if (exponential && !decrease && v->level > 0x6000) {
        if (shift < 10) {
            adsr_step /= 4;
        } else if (shift >= 11) {
            increment /= 4;
        } else {
            adsr_step /= 2;
            increment /= 2;
        }
    } else if (exponential && decrease) {
        adsr_step = adsr_step * v->level / 0x8000;
    }
    if ((step | shift << 2) != 0x7f && increment == 0)
        increment = 1;
    v->env_counter += increment;
    if (!(v->env_counter & 0x8000))
        return;
    v->env_counter = 0;
    v->level += adsr_step;
    if (!decrease)
        v->level = v->level > 0x7fff ? 0x7fff : v->level;
    else if (v->level < 0)
        v->level = 0;
}

static void envelope(Voice *v)
{
    switch (v->phase) {
    case ENV_ATTACK:
        envelope_tick(v, v->adsr1 >> 15, false, (v->adsr1 >> 10) & 0x1f, (v->adsr1 >> 8) & 3);
        if (v->level >= 0x7fff) {
            v->phase = ENV_DECAY;
            v->env_counter = 0;
        }
        break;
    case ENV_DECAY:
        envelope_tick(v, true, true, (v->adsr1 >> 4) & 0xf, 0);
        if (v->level <= ((v->adsr1 & 0xf) + 1) * 0x800) {
            v->phase = ENV_SUSTAIN;
            v->env_counter = 0;
        }
        break;
    case ENV_SUSTAIN:
        envelope_tick(v, v->adsr2 >> 15, (v->adsr2 >> 14) & 1, (v->adsr2 >> 8) & 0x1f, (v->adsr2 >> 6) & 3);
        break;
    case ENV_RELEASE:
        envelope_tick(v, (v->adsr2 >> 5) & 1, true, v->adsr2 & 0x1f, 0);
        if (v->level == 0)
            v->phase = ENV_OFF;
        break;
    default:
        break;
    }
}

/* One 44.1 kHz sample of a voice, after its envelope, into the left and right sums. */
static void voice_sample(int index, int32_t *left, int32_t *right)
{
    Voice *v = &voices[index];
    if (!v->decoded)
        decode_block(v);
    int s = (int)(v->counter >> 12);
    int g = (int)(v->counter >> 4) & 0xff;
    int32_t out = (spu_gauss[0xff - g] * v->samples[s] >> 15) + (spu_gauss[0x1ff - g] * v->samples[s + 1] >> 15) +
                  (spu_gauss[0x100 + g] * v->samples[s + 2] >> 15) + (spu_gauss[g] * v->samples[s + 3] >> 15);
    out = out * v->level >> 15;
    *left += out * (int16_t)(v->volume[0] << 1) >> 15;
    *right += out * (int16_t)(v->volume[1] << 1) >> 15;

    envelope(v);
    v->counter += v->pitch > 0x3fff ? 0x4000 : v->pitch;
    if ((v->counter >> 12) >= BLOCK_SAMPLES) {
        v->counter -= BLOCK_SAMPLES << 12;
        if (v->flags & FLAG_LOOP_END) {
            endx |= 1u << index;
            v->address = v->repeat;
            if (!(v->flags & FLAG_LOOP_REPEAT)) {
                v->phase = ENV_RELEASE;
                v->level = 0;
            }
        } else {
            v->address += BLOCK_BYTES / 8;
        }
        v->decoded = false;
    }
}

void SpuGetVoiceEnvelope(int voice, short *envx)
{
    *envx = (short)voices[voice].level;
}

void spu_mix(short *stereo, int frames)
{
    cd_audio_read(stereo, frames);
    for (int i = 0; i < frames; i++) {
        int32_t l = stereo[i * 2] * common.cd.volume.left >> 15;
        int32_t r = stereo[i * 2 + 1] * common.cd.volume.right >> 15;
        for (int v = 0; v < VOICES; v++)
            if (voices[v].phase != ENV_OFF)
                voice_sample(v, &l, &r);
        l = clamp16(l);
        r = clamp16(r);
        stereo[i * 2] = clamp16(l * common.mvol.left >> 14);
        stereo[i * 2 + 1] = clamp16(r * common.mvol.right >> 14);
    }
}

/* 44100 Hz against the 59.94 Hz vblank: 735 or 736 frames per vblank. */
void spu_service(void)
{
    int64_t start = vblanks_mixed * SPU_RATE * 1001 / 60000;
    vblanks_mixed++;
    int frames = (int)(vblanks_mixed * SPU_RATE * 1001 / 60000 - start);
    short out[MAX_VBLANK_FRAMES * 2];
    spu_mix(out, frames);
    platform_audio_push(out, frames);
}
