/* libspu replacement: SPU RAM, its allocator and transfers, and the voices (ADPCM, Gaussian
 * interpolation, ADSR), mixed with the CD input. */
#ifndef PSX_LIBSPU_H
#define PSX_LIBSPU_H

#include "psx/types.h"

#define SPU_RAM_SIZE 0x80000

#define SPU_OFF 0
#define SPU_ON 1
#define SPU_ALLCH 0xffffff /* every voice, for SpuSetKey */
#define SPU_TRANSFER_BY_DMA 0
#define SPU_TRANSFER_WAIT 1

#define SPU_VOICE_VOLL (1 << 0)
#define SPU_VOICE_VOLR (1 << 1)
#define SPU_VOICE_VOLMODEL (1 << 2)
#define SPU_VOICE_VOLMODER (1 << 3)
#define SPU_VOICE_PITCH (1 << 4)
#define SPU_VOICE_WDSA (1 << 7)
#define SPU_VOICE_LSAX (1 << 16)
#define SPU_VOICE_ADSR_ADSR1 (1 << 17)
#define SPU_VOICE_ADSR_ADSR2 (1 << 18)

#define SPU_COMMON_MVOLL (1 << 0)
#define SPU_COMMON_MVOLR (1 << 1)
#define SPU_COMMON_CDVOLL (1 << 6)
#define SPU_COMMON_CDVOLR (1 << 7)

typedef struct {
    short left;
    short right;
} SpuVolume;

/* Psy-Q's `long` fields are 32 bits; int32_t keeps the layout on LP64 systems. */
typedef struct {
    SpuVolume volume;
    int32_t reverb;
    int32_t mix;
} SpuExtAttr;

typedef struct {
    u_long mask;
    SpuVolume mvol;
    SpuVolume mvolmode;
    SpuVolume mvolx;
    SpuExtAttr cd;
    SpuExtAttr ext;
} SpuCommonAttr;

typedef struct {
    u_long voice;
    u_long mask;
    SpuVolume volume;
    SpuVolume volmode;
    SpuVolume volumex;
    u_short pitch;
    u_short note;
    u_short sample_note;
    short envx;
    u_long addr;
    u_long loop_addr;
    int32_t a_mode;
    int32_t s_mode;
    int32_t r_mode;
    u_short ar;
    u_short dr;
    u_short sr;
    u_short rr;
    u_short sl;
    u_short adsr1;
    u_short adsr2;
} SpuVoiceAttr;

_Static_assert(sizeof(SpuVoiceAttr) == 0x40, "SpuVoiceAttr has the Psy-Q layout");

void SpuInit(void);
void SpuQuit(void);
long SpuInitMalloc(long num, char *top);
long SpuMalloc(long size);
void SpuSetCommonAttr(SpuCommonAttr *attr);
long SpuSetReverb(long on_off);
long SpuSetTransferMode(long mode);
u_long SpuSetTransferStartAddr(u_long addr);
u_long SpuWrite(u_char *addr, u_long size);
long SpuIsTransferCompleted(long flag);
void SpuSetVoiceAttr(SpuVoiceAttr *attr);
void SpuSetKey(long on_off, u_long voice_bit);
void SpuGetVoiceEnvelope(int voice, short *envx);

/* Port-only: SPU RAM for the voice emulation. */
const u_char *spu_ram(void);

/* Port-only: one vblank of SPU output, run by the vblank service: the voices and the CD input
 * at the CD and main volumes, sent to the sound card. */
void spu_service(void);

/* Port-only: mix `frames` 44.1 kHz stereo frames (voices and CD input, at the CD and main
 * volumes) into `stereo`. CD input volume 0x8000 is 1.0, main volume 0x4000. */
void spu_mix(short *stereo, int frames);

#endif
