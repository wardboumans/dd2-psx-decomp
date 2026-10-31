#ifndef SOUND_SOUND_H
#define SOUND_SOUND_H

#include "psx/libspu.h"

#include <stdint.h>

#define SOUND_CHANNELS 24
#define ANY_CHANNEL (-1)       /* Play_Sound: the first channel not locked */

/* Play_Sound / Modify_Sound units. */
#define SOUND_VOLUME_MAX 0xfff /* full volume */
#define PITCH_NORMAL 0x1000    /* the sample's own rate */
#define PITCH_HALF 0x800
#define PAN_FULL 0x1000        /* pan -PAN_FULL .. PAN_FULL silences one side */

#define NO_PRIORITY 0x100      /* SoundChannel.priority of a free channel (lower numbers win) */
#define SPU_RATE 44100         /* SPU pitch 0x1000 plays at this rate */
#define SPU_PITCH_MAX 0x3fff

/* One sample of a sound bank (.SBK), as stored in the file. */
typedef struct {
    uint32_t spu_addr;    /* offset in the file; SPU RAM address once loaded */
    uint32_t length;      /* bytes of ADPCM data */
    uint32_t looping;
    uint32_t sample_rate; /* Hz */
    uint32_t priority;
    uint32_t unused[2];   /* 0 in BANK1.SBK; never read */
} SampleInfo;

_Static_assert(sizeof(SampleInfo) == 0x1c, "SampleInfo must match the .SBK layout");

#define SAMPLE_BANK_SIZE 64

/* .SBK: 0x10-byte header with the sample count at +0x0C, the SampleInfo table, then the ADPCM data. */
#define SBK_HEADER_SIZE 0x10
#define SBK_COUNT_OFFSET 0x0c

/* One SPU voice and the game's bookkeeping for it (0x68 bytes in the original). */
typedef struct {
    SpuVoiceAttr attr;    /* 0x00 */
    int32_t looping;      /* 0x40 */
    int32_t time_left;    /* 0x44, vblanks left of a one-shot sample */
    int32_t priority;     /* 0x48, lower wins when channels run out */
    int32_t sample_rate;  /* 0x4C */
    uint8_t unused[0x14]; /* 0x50, never accessed */
    int32_t locked;       /* 0x64 */
} SoundChannel;

_Static_assert(sizeof(SoundChannel) == 0x68, "SoundChannel keeps the original layout");

extern SoundChannel channels[SOUND_CHANNELS]; /* 0x8007BD40 */
extern SpuCommonAttr spu_common;              /* 0x8007C700 */
extern int32_t master_volume;                 /* 0x800798C8 */

void reset_sound_channels(void);    /* 0x8004E698 */
void Sound_Init(void);              /* 0x8004E734 */
void set_master_volume(int32_t v);  /* 0x8004E794 */
int Play_Sound(int channel, const SampleInfo *bank, int sample, int volume, int pitch, int pan); /* 0x8004E8D0 */
void Sound_Timer(void);             /* 0x8004EFAC */
int Find_Unlocked_Channel(int32_t priority); /* 0x8004F010 */
void Modify_Sound(int channel, int volume, int pitch, int pan); /* 0x8004EBF0 */
void Lock_Channel(int first, int last); /* 0x8004EE9C */
void Sound_Pause(void);                /* 0x8004ED5C */
void Sound_Restart(void);              /* 0x8004ED98 */
void Kill_Sound(int channel);          /* 0x8004EE10 */
void Sound_Stop(void);              /* 0x8004EF28 */

#endif
