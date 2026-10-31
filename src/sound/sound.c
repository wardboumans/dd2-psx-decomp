#include "sound/sound.h"

#include "graphics/object.h"

#include <string.h>

#define SPU_MALLOC_RECSIZ 8
#define SPU_MALLOC_BLOCKS 2

SoundChannel channels[SOUND_CHANNELS];
SpuCommonAttr spu_common;
int32_t master_volume;

static char spu_malloc_table[SPU_MALLOC_RECSIZ * (SPU_MALLOC_BLOCKS + 1)]; /* 0x8007C8B8 */

void reset_sound_channels(void)
{
    for (int i = 0; i < SOUND_CHANNELS; i++) {
        SoundChannel *ch = &channels[i];
        ch->attr.voice = 1u << i;
        ch->attr.volmode.left = 0;
        ch->attr.volmode.right = 0;
        ch->locked = 0;
        ch->looping = 0;
        ch->time_left = 0;
        ch->priority = NO_PRIORITY;
    }
}

void Sound_Init(void)
{
    SpuInit();
    SpuInitMalloc(SPU_MALLOC_BLOCKS, spu_malloc_table);
    reset_sound_channels();
    spu_common.mvol.left = 0x3ffc;
    spu_common.mvol.right = 0x3ffc;
    spu_common.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR;
    SpuSetCommonAttr(&spu_common);
}

void set_master_volume(int32_t v)
{
    master_volume = v;
}

/* Starts `sample` of `bank` on `channel` (-1: any free channel the sample's priority allows).
 * `pitch` 4096 plays at the sample's rate, `volume` 4095 is full, `pan` -4096..4096 quietens the
 * right or left side. Returns the channel, or -1 when none was free. */
int Play_Sound(int channel, const SampleInfo *bank, int sample, int volume, int pitch, int pan)
{
    const SampleInfo *s = &bank[sample];
    if (channel == -1) {
        channel = Find_Unlocked_Channel((int32_t)s->priority);
        if (channel == -1)
            return -1;
    }
    SoundChannel *ch = &channels[channel];
    SpuSetKey(SPU_OFF, ch->attr.voice);
    ch->attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_VOLMODEL | SPU_VOICE_VOLMODER |
                    SPU_VOICE_PITCH | SPU_VOICE_WDSA;
    ch->looping = s->looping != 0;
    int32_t rate = pitch * (int32_t)s->sample_rate / PITCH_NORMAL;
    int32_t spu_pitch = rate * 0x1000 / SPU_RATE;
    int32_t frame_bytes = rate / frames_per_sec * 2;
    if (frame_bytes == 0)
        return spu_pitch <= SPU_PITCH_MAX; /* sic: not a channel */
    if (spu_pitch > SPU_PITCH_MAX)
        spu_pitch = SPU_PITCH_MAX;
    ch->time_left = (int32_t)(s->length / (uint32_t)frame_bytes);
    ch->sample_rate = (int32_t)s->sample_rate;
    ch->priority = (int32_t)s->priority;
    ch->attr.pitch = (u_short)spu_pitch;
    ch->attr.addr = s->spu_addr;
    int32_t vol = volume * master_volume / 0x1000;
    int32_t left = vol, right = vol;
    if (pan < 0)
        right = vol - vol * -pan / PAN_FULL;
    else
        left = vol - vol * pan / PAN_FULL;
    ch->attr.volume.left = (short)(left * 4);
    ch->attr.volume.right = (short)(right * 4);
    SpuSetVoiceAttr(&ch->attr);
    SpuSetKey(SPU_ON, ch->attr.voice);
    return channel;
}

/* Runs every vblank from the VSync callback: one-shot samples count down their play time. */
void Sound_Timer(void)
{
    for (int i = 0; i < SOUND_CHANNELS; i++) {
        SoundChannel *ch = &channels[i];
        if (!ch->looping && ch->time_left > 0)
            ch->time_left--;
    }
}

/* A channel that is not locked and has finished its sample; failing that, one playing a sample
 * of lower priority (a higher number) than `priority`. */
int Find_Unlocked_Channel(int32_t priority)
{
    for (int i = 0; i < SOUND_CHANNELS; i++)
        if (!channels[i].locked && channels[i].time_left == 0)
            return i;
    for (int i = 0; i < SOUND_CHANNELS; i++)
        if (!channels[i].locked && channels[i].priority > priority)
            return i;
    return -1;
}

/* Changes a playing sample's volume, pitch (4096 the sample's rate) and pan (as Play_Sound). */
void Modify_Sound(int channel, int volume, int pitch, int pan)
{
    SoundChannel *ch = &channels[channel];
    if (ch->time_left <= 0)
        return;
    ch->attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_VOLMODEL | SPU_VOICE_VOLMODER | SPU_VOICE_PITCH;
    int32_t spu_pitch = (int32_t)((uint32_t)(pitch * ch->sample_rate) & 0xfffff000u) / SPU_RATE;
    if (spu_pitch > SPU_PITCH_MAX)
        spu_pitch = SPU_PITCH_MAX;
    ch->attr.pitch = (u_short)spu_pitch;
    int32_t vol = volume * master_volume / 0x1000;
    int32_t left = vol, right = vol;
    if (pan < 0)
        right = vol - vol * -pan / PAN_FULL;
    else
        left = vol - vol * pan / PAN_FULL;
    ch->attr.volume.left = (short)(left * 4);
    ch->attr.volume.right = (short)(right * 4);
    SpuSetVoiceAttr(&ch->attr);
}

/* Keeps channels first..last for the caller: Play_Sound(-1, ...) will not take them. */
void Lock_Channel(int first, int last)
{
    for (int i = first; i <= last; i++)
        channels[i].locked = 1;
}

/* Mutes every voice, keeping its settings (the pause menu). */
void Sound_Pause(void)
{
    SpuVoiceAttr a;
    memset(&a, 0, sizeof(a));
    a.voice = SPU_ALLCH;
    a.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
    a.volume.left = 0;
    a.volume.right = 0;
    SpuSetVoiceAttr(&a);
}

/* Gives every voice back the volume its channel holds. */
void Sound_Restart(void)
{
    for (int i = 0; i < SOUND_CHANNELS; i++) {
        channels[i].attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
        SpuSetVoiceAttr(&channels[i].attr);
    }
}

/* Stops one channel's voice and frees the channel. */
void Kill_Sound(int channel)
{
    SpuSetKey(SPU_OFF, 1u << (channel & 0x1f));
    channels[channel].locked = 0;
    channels[channel].looping = 0;
    channels[channel].time_left = 0;
    channels[channel].priority = NO_PRIORITY;
}

/* Silences every voice and frees every channel. */
void Sound_Stop(void)
{
    SpuSetKey(SPU_OFF, SPU_ALLCH);
    for (int i = 0; i < SOUND_CHANNELS; i++) {
        channels[i].locked = 0;
        channels[i].looping = 0;
        channels[i].time_left = 0;
        channels[i].priority = NO_PRIORITY;
    }
}
