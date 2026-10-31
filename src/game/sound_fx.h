#ifndef GAME_SOUND_FX_H
#define GAME_SOUND_FX_H

#include "sound/sound.h"

#include <stdint.h>

extern SampleInfo sample_bank[SAMPLE_BANK_SIZE]; /* 0x80087130 */

/* The samples of VAGS\BANK1.SBK. The commentator's lines (SAY_*) are named after what the game plays them
 * for. Front and rear wheels are told apart by the handling a lost pair leaves (Detach_Damaged_Wheel: without
 * wheels 2 and 3 the car cannot steer). */
enum {
    SAMPLE_ENGINE = 0,        /* the engine loop of every car */
    SAMPLE_CAR_COLLISION = 1, /* two cars hit */
    SAMPLE_CRASH = 3,         /* the body hits the ground or a barrier; also the pit's repairs */
    SAMPLE_CROWD_LOW = 4,
    SAMPLE_CROWD_HIGH = 5,
    SAMPLE_WHOOSH = 6,        /* the camera's fly-in at the start */
    SAMPLE_SKID = 7,          /* the player's tyres */
    SAMPLE_COUNT_GO = 8,      /* the start lights and the pit stop's countdown: go, 1, 2, 3 */
    SAMPLE_COUNT_1 = 9,
    SAMPLE_COUNT_2 = 10,
    SAMPLE_COUNT_3 = 11,
    SAY_FIRST = 0x0c,
    SAY_SECOND = 0x0d,
    SAY_THIRD = 0x0e,
    SAY_RACE_START = 0x0f,    /* during the fly-in */
    SAY_HEAVY_DAMAGE = 0x10,
    SAY_REAR_WHEELS_LOST = 0x11,
    SAY_FRONT_WHEELS_LOST = 0x12,
    SAY_FIRE_A = 0x13,
    SAY_BARRIER_A = 0x14,
    SAY_WRECKED = 0x15,
    SAY_WRECKAGE = 0x16,      /* everything lost, or a big pile-up */
    SAY_BONNET_LOST = 0x17,
    SAY_BOOT_LOST = 0x18,
    SAY_BARRIER_E = 0x19,
    SAY_LEADING_FAST = 0x1a,
    SAY_REAR_WHEEL_LOST = 0x1b,
    SAY_PILE_UP_A = 0x1c,
    SAY_BIG_DROP = 0x1d,
    SAY_WRONG_WAY = 0x1e,
    SAY_LAST_LAP = 0x1f,
    SAY_BARRIER_B = 0x20,
    SAY_STUCK = 0x21,
    SAY_BARRIER_C = 0x22,
    SAY_SLOW = 0x23,
    SAY_FRONT_WHEEL_LOST = 0x24,
    SAY_PIT_IN = 0x25,
    SAY_FIRE_B = 0x26,
    SAY_PILE_UP_B = 0x27,
    SAY_BARRIER_D = 0x28,
    SAMPLE_MENU_CLICK = 0x29,   /* a value or the ring moved */
    SAMPLE_MENU_CONFIRM = 0x2a, /* the slab has flipped in; the pause menu's confirm */
    SAMPLE_MENU_WARNING = 0x2b, /* a question pops up */
    SAMPLE_MENU_SELECT = 0x2c,  /* an item was pressed; the pause menu's cursor */
};

#define COMMENTARY_CHANNEL 10 /* the commentator and the start beeps (locked by Init_Race_Sounds) */
#define CAR_SOUND_VOLUME 0x9c4 /* Allocate_Sound_Effect's volume of the crashes, skids and repairs */
#define LANDING_VOLUME 0x708   /* a car landing on a corner */
#define ENGINE_VOLUME 0x640    /* the engines' loudest */
#define CROWD_VOLUME 0x4b0     /* the crowd's level before it cheers */
#define MIN_PITCH 0xc18        /* the lowest pitch of the race sounds (an engine at idle) */
#define EFFECT_PITCH 0x2000    /* the effects and the crowd play at twice their samples' rates */
#define VOICE_PITCH 0xfff      /* the commentator and the start beeps, just under the sample rate */

/* A race sound on a channel: a car's engine, or a one-shot effect at a place (car -1). */
typedef struct {
    const SampleInfo *bank;  /* +0x00: set while the channel carries a race sound */
    int32_t x, y, z;         /* +0x04: where an effect is */
    int32_t pad;             /* +0x10: never accessed */
    int32_t car;             /* +0x14 */
    uint32_t flags;          /* +0x18: RACE_SOUND_* */
    int32_t channel;         /* +0x1C */
    int32_t pitch;           /* +0x20: 0x1000 the sample's own rate */
    int32_t volume;          /* +0x24: the loudest it gets */
} RaceSound;

#define RACE_SOUND_DOPPLER 1
#define RACE_SOUND_PANNED 2

extern RaceSound race_sounds[SOUND_CHANNELS]; /* 0x8007F870 */
extern int32_t strip_trigger;              /* 0x80079A8C: the player's strip's trigger, each frame */

void Load_Game_Vags(void);          /* 0x80046FE0 */
void Init_Race_Sounds(void);        /* 0x80046000 */
void Update_Race_Sounds(void);      /* 0x800461D8 */
void Allocate_Sound_Effect(int sample, int volume, const int32_t where[3]); /* 0x80046D20 */
void Update_Race_Commentary(void);  /* 0x80047154 */

#endif
