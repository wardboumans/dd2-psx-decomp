#include "game/sound_fx.h"

#include "game/ai.h"
#include "game/camera.h"
#include "game/car.h"
#include "game/car_collision.h"
#include "game/car_motion.h"
#include "game/collision.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/track.h"
#include "graphics/object.h"
#include "psx/libgte.h"
#include "system/maths.h"
#include "psx/libspu.h"
#include "system/file.h"
#include "system/mpe_heap.h"

#include <stdlib.h>
#include <string.h>

SampleInfo sample_bank[SAMPLE_BANK_SIZE];

/* Loads the effects bank: ADPCM data into SPU RAM, the sample table into sample_bank. */
void Load_Game_Vags(void)
{
    uint8_t *file = heap_base;
    int32_t size = File_Load("VAGS\\BANK1.SBK", file);
    int32_t count;
    memcpy(&count, file + SBK_COUNT_OFFSET, sizeof(count));
    SampleInfo *table = (SampleInfo *)(file + SBK_HEADER_SIZE);
    uint32_t data_offset = SBK_HEADER_SIZE + (uint32_t)count * sizeof(SampleInfo);
    uint32_t data_size = (uint32_t)size - data_offset;

    long spu_addr = SpuMalloc((long)data_size);
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr((u_long)spu_addr);
    SpuWrite(file + data_offset, data_size);
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);

    for (int i = 0; i < count; i++)
        table[i].spu_addr = (uint32_t)spu_addr + (table[i].spu_addr - data_offset);
    memcpy(sample_bank, table, sizeof(sample_bank));
}

/* ---- the race's sounds: engines, effects at places, the crowd, the commentator */

RaceSound race_sounds[SOUND_CHANNELS];
int32_t strip_trigger;

static uint32_t car_distance[MAX_CARS]; /* 0x800884EC: squared from the camera, 0xFFFFFFFF far away */
static int32_t nearest_cars[MAX_CARS];  /* 0x8008C70C: the cars by distance after the player's */

#define ENGINE_CHANNELS 8 /* the player's engine and the seven nearest others */
#define CROWD_LOW 8       /* the crowd, two channels, the first at half volume */
#define CROWD_HIGH 9
#define NO_CAR (-1)       /* RaceSound.car of a sound at a place */
#define CROWD_QUIET 0x200 /* the crowd's level away from the stands */
/* Pine Hills' grandstand by the player's strip: the crowd fades out, stays quiet, fades back in. */
#define STAND_FIRST 127
#define STAND_QUIET 160
#define STAND_LOUD 177
#define STAND_LAST 209

static int32_t crowd_volume;      /* 0x80079B5C */
static int32_t crowd_cheer;       /* 0x80079B9C: the crowd swells (set by the commentary's events) */
static int32_t crowd_level;       /* 0x80079B98: how far into the swell, 0..0x40 */
static int32_t crowd_rising;      /* 0x80079AD4 */
static int32_t cars_drawn;        /* 0x80079B8C: cars on screen this frame */
static int32_t last_lap;          /* 0x80079AFC: the player's lap the commentator last spoke of */
/* Each remark once, or not again for a while (frames). */
static int32_t wreck_remarked;    /* 0x80079BAC */
static int32_t damage_remarked;   /* 0x80079BB0 */
static int32_t bonnet_remarked;   /* 0x80079BB8 */
static int32_t boot_remarked;     /* 0x80079BC0 */
static int32_t unused_bc4;        /* 0x80079BC4: only cleared */
static int32_t unused_bc8;        /* 0x80079BC8: only cleared and counted down, so it stays 0 */
static int32_t parts_remarked;    /* 0x80079C08 */
static int32_t fire_remarked;     /* 0x80079AF4 */
static int32_t pit_remarked;      /* 0x80079C00 */
static int32_t wrong_way_wait;    /* 0x80079C04 */
static int32_t air_wait;          /* 0x80079BD4 */
static int32_t roof_wait;         /* 0x80079C10 */
static int32_t slow_remark_wait;  /* 0x80079BFC */
static int32_t pileup_wait;       /* 0x80079C0C */
static int32_t crowd_remark_wait; /* 0x80079BD0 */

/* Engines on the first channels (the engine loop, sample 0), the crowd on 8 and 9, the commentator on
 * 10; channels 0 to 10 are kept from other sounds. */
void Init_Race_Sounds(void)
{
    slow_remark_wait = 1000;
    fire_remarked = 0;
    wreck_remarked = 0;
    damage_remarked = 0;
    bonnet_remarked = 0;
    boot_remarked = 0;
    unused_bc4 = 0;
    unused_bc8 = 0;
    wrong_way_wait = 0;
    roof_wait = 0;
    air_wait = 0;
    pit_remarked = 0;
    parts_remarked = 0;
    crowd_remark_wait = 0;
    pileup_wait = 0;
    barrier_remark_wait = 0;
    skid_sound_wait = 0;
    last_lap = 1;
    commentary_busy = 0;
    car_collisions = 0;
    cars_drawn = 0;
    crowd_rising = 1;
    crowd_level = 0;
    crowd_cheer = 0;
    for (int i = 0; i < SOUND_CHANNELS; i++) {
        race_sounds[i].bank = NULL;
        race_sounds[i].channel = i;
    }
    Lock_Channel(0, COMMENTARY_CHANNEL);
    for (int car = 0; car < num_cars && car < ENGINE_CHANNELS; car++) {
        RaceSound *rs = &race_sounds[car];
        rs->car = car;
        rs->bank = sample_bank;
        rs->volume = ENGINE_VOLUME;
        Play_Sound(car, sample_bank, SAMPLE_ENGINE, 0, MIN_PITCH, 0);
        rs->flags |= RACE_SOUND_DOPPLER | RACE_SOUND_PANNED;
    }
    crowd_volume = CROWD_VOLUME;
    Play_Sound(CROWD_LOW, sample_bank, SAMPLE_CROWD_LOW, 0, EFFECT_PITCH, 0);
    Play_Sound(CROWD_HIGH, sample_bank, SAMPLE_CROWD_HIGH, 0, EFFECT_PITCH, 0);
}

/* The engine note from the car's speed, through four gears; off its wheels (and not flying) it dies
 * away. */
static int32_t Get_Engine_Frequency(const RaceSound *rs)
{
    const CarBlock *cb = &car_blocks[rs->car];
    if (cb->motion_state != MOTION_DRIVING && cb->motion_state != MOTION_AIRBORNE)
        return rs->pitch - 0x100 < MIN_PITCH ? MIN_PITCH : rs->pitch - 0x100;
    int32_t s = cb->speed;
    if (s < 0x100)
        return s * 16000 / 256 + MIN_PITCH;
    if (s < 0x180)
        return (s - 0x100) * 4000 / 128 + 12000;
    if (s < 0x200)
        return (s - 0x180) * 0xdac / 128 + 0x2cec;
    return (s - 0x200) * 3000 / 0x58 + 11000;
}

/* Loudness at the camera: the sound's volume over the squared distance, none beyond 30000 on an axis. */
static int32_t Amplitude(const RaceSound *rs)
{
    int32_t a;
    if (rs->car == NO_CAR) {
        int32_t dx = abs(world_matrix.t[0] - rs->x), dz = abs(world_matrix.t[2] - rs->z);
        if (dx > 30000 || dz > 30000)
            return 0;
        a = (rs->volume << 16) / ((dx * dx + dz * dz) / 1024 + 1);
    } else {
        if (car_distance[rs->car] == 0xffffffffu)
            return 0;
        a = (int32_t)((uint32_t)(rs->volume << 18) / ((car_distance[rs->car] >> 6) + 1));
    }
    if (a < 0)
        a = 0;
    if (a > rs->volume)
        a = rs->volume;
    return a;
}

/* The pitch shifted by the source's and the listener's (the player's car's) speeds along the line
 * between them. */
static int32_t DopplerFrequency(const RaceSound *rs, const int32_t listener[3])
{
    int32_t source = 0, heard;
    if (rs->car == NO_CAR) {
        int32_t dx = world_matrix.t[0] - rs->x, dz = world_matrix.t[2] - rs->z;
        int32_t dist = SquareRoot0(dx * dx + dz * dz) + 1;
        heard = (dx * (listener[0] >> 12) + dz * (listener[2] >> 12)) / dist;
    } else {
        const CarBlock *cb = &car_blocks[rs->car];
        int32_t dx = world_matrix.t[0] - car_states[rs->car].position[0];
        int32_t dz = world_matrix.t[2] - car_states[rs->car].position[2];
        int32_t dist = SquareRoot0((int32_t)car_distance[rs->car]) + 1;
        source = (dx * (cb->velocity[0] >> 12) + dz * (cb->velocity[2] >> 12)) / dist;
        heard = (dx * (listener[0] >> 12) + dz * (listener[2] >> 12)) / dist;
    }
    heard += ONE;
    if (heard == 0)
        heard = 1;
    int32_t f = (source + ONE) * rs->pitch / heard;
    return f < MIN_PITCH ? MIN_PITCH : f;
}

/* A one-shot effect at a place (x, y, z), heard by distance and direction. */
void Allocate_Sound_Effect(int sample, int volume, const int32_t where[3])
{
    int ch = Play_Sound(ANY_CHANNEL, sample_bank, sample, 0, EFFECT_PITCH, 0);
    /* PORT: the original writes the record of channel -1 (just before the table) when none is free */
    if (ch < 0)
        return;
    RaceSound *rs = &race_sounds[ch];
    rs->car = NO_CAR;
    rs->bank = sample_bank;
    rs->flags = (rs->flags & ~RACE_SOUND_DOPPLER) | RACE_SOUND_PANNED;
    rs->x = where[0];
    rs->y = where[1];
    rs->z = where[2];
    rs->pitch = EFFECT_PITCH;
    rs->volume = volume;
}

static void crowd(int32_t volume)
{
    Modify_Sound(CROWD_LOW, volume / 2, EFFECT_PITCH, 0);
    Modify_Sound(CROWD_HIGH, volume, EFFECT_PITCH, 0);
}

/* Left/right balance from where the sound is in the camera's view. */
static int32_t pan_of(const RaceSound *rs)
{
    const int32_t *cam = world_matrix.t;
    SVECTOR rel;
    if (rs->car == NO_CAR) {
        rel.vx = (int16_t)(rs->x - cam[0]);
        rel.vy = (int16_t)(rs->y - cam[1]);
        rel.vz = (int16_t)(rs->z - cam[2]);
    } else {
        const int32_t *p = car_states[rs->car].position;
        rel.vx = (int16_t)(p[0] - cam[0]);
        rel.vy = (int16_t)(p[1] - cam[1]);
        rel.vz = (int16_t)(p[2] - cam[2]);
    }
    gte_ldv0(&rel);
    gte_rtv0();
    int32_t a = Calc_Angle((int16_t)gte.ir[3] * 16, (int16_t)gte.ir[1] * 16);
    int32_t pan;
    int32_t within = a & (QUARTER_TURN - 1);
    if (a < QUARTER_TURN)
        pan = 0xfff - within * 4;
    else if (a > HALF_TURN + QUARTER_TURN)
        pan = within * 4;
    else if (a < HALF_TURN)
        pan = -within * 4;
    else
        pan = within * 4 - 0x1000;
    return rs->car == 0 ? 0 : pan;
}

/* Every frame: the engine channels follow the player and the seven cars nearest the camera, every race
 * sound gets its loudness, pitch (engine speed, Doppler) and pan, and the crowd its level (on level 1
 * by where the player is). */
void Update_Race_Sounds(void)
{
    if (commentary_busy > 0)
        commentary_busy--;
    SetRotMatrix(&world_matrix);
    const int32_t listener[3] = {car_blocks[0].velocity[0], car_blocks[0].velocity[1], car_blocks[0].velocity[2]};
    for (int car = 0; car < num_cars; car++) {
        int32_t dx = abs(world_matrix.t[0] - car_states[car].position[0]);
        int32_t dz = abs(world_matrix.t[2] - car_states[car].position[2]);
        car_distance[car] = dx < 0x7531 && dz < 0x7531 ? (uint32_t)(dx * dx + dz * dz) : 0xffffffffu;
        nearest_cars[car] = car;
    }
    for (int pass = 0; pass < num_cars - 8; pass++) {
        for (int i = 1; i < num_cars - 1; i++) {
            if (car_distance[nearest_cars[i + 1]] < car_distance[nearest_cars[i]]) {
                int32_t t = nearest_cars[i];
                nearest_cars[i] = nearest_cars[i + 1];
                nearest_cars[i + 1] = t;
            }
        }
    }
    race_sounds[0].car = 0;
    for (int i = 1; i < ENGINE_CHANNELS; i++)
        race_sounds[i].car = nearest_cars[i];
    strip_trigger = strip_at(car_fd[0].strip)->trigger;
    if (crowd_cheer == 0) {
        if (current_level == LEVEL_PINE_HILLS) {
            uint16_t at = (uint16_t)car_fd[0].strip_number;
            if (at < STAND_FIRST || at > STAND_LAST)
                crowd(crowd_volume);
            if (at >= STAND_FIRST && at < STAND_QUIET)
                crowd(crowd_volume - (crowd_volume - CROWD_QUIET) * (at - STAND_FIRST) / 32);
            if (at >= STAND_QUIET && at < STAND_LOUD)
                crowd(CROWD_QUIET);
            if (at >= STAND_LOUD && at <= STAND_LAST)
                crowd((crowd_volume - CROWD_QUIET) * (at - STAND_LOUD) / 32 + CROWD_QUIET);
        } else {
            crowd(CROWD_QUIET);
        }
    }
    /* the original keeps one pan value across the channels: one without its own takes the last one */
    int32_t pan = 0;
    for (int i = 0; i < SOUND_CHANNELS; i++) {
        RaceSound *rs = &race_sounds[i];
        if (rs->bank == NULL)
            continue;
        if (rs->car != NO_CAR)
            rs->pitch = Get_Engine_Frequency(rs);
        int32_t amp = Amplitude(rs);
        if (amp == 0) {
            Modify_Sound(rs->channel, 0, rs->pitch, 0);
            continue;
        }
        if (rs->flags & RACE_SOUND_PANNED)
            pan = pan_of(rs);
        int32_t pitch = rs->flags & RACE_SOUND_DOPPLER ? DopplerFrequency(rs, listener) : rs->pitch;
        Modify_Sound(rs->channel, amp, pitch, pan);
    }
}

/* A commentator's line; `busy` frames before the next. */
static void say(int sample, int32_t busy)
{
    Play_Sound(COMMENTARY_CHANNEL, sample_bank, sample, SOUND_VOLUME_MAX, VOICE_PITCH, 0);
    commentary_busy = busy;
}

static void count_down(int32_t *t)
{
    if (*t > 0)
        (*t)--;
}

/* The commentator on what the player's car does (wrecked, damaged, laps and places, wrong way, big
 * jumps, on its roof, parts lost, pile-ups), one remark at a time; the crowd swells with them. */
void Update_Race_Commentary(void)
{
    int me = current_player_car;
    const CarBlock *cb = &car_blocks[me];
    const RaceRecord *rr = &race_records[me];
    if (pit_in == 1 && pit_camera_done == 0 && commentary_busy == 0 && pit_remarked == 0) {
        say(SAY_PIT_IN, 50);
        pit_remarked = 1;
    }
    if (pit_in == 1)
        return;
    if (cb->wrecked != 0 && wreck_remarked == 0 && commentary_busy == 0) {
        say(SAY_WRECKED, 80);
        wreck_remarked = 1;
        crowd_cheer = 1;
    }
    if (cb->heavy_damage != 0 && damage_remarked == 0 && commentary_busy == 0) {
        say(SAY_HEAVY_DAMAGE, 40);
        damage_remarked = 1;
        crowd_cheer = 1;
    }
    int laps = track_info[current_level].laps;
    if (last_lap < rr->lap_counted) {
        if (rr->lap_counted == laps + 1)
            crowd_cheer = 1;
        last_lap = rr->lap_counted;
        if (rr->lap_counted == laps && commentary_busy == 0 && num_cars > 1) {
            say(SAY_LAST_LAP, 55);
            crowd_cheer = 1;
        }
        if (rr->position == 1 && commentary_busy == 0 && num_cars > 1) {
            if (cb->speed < 0x1c3)
                say(SAY_FIRST, 15);
            else
                say(SAY_LEADING_FAST, 55);
            crowd_cheer = 1;
        }
        if (rr->position == 2 && commentary_busy == 0)
            say(SAY_SECOND, 15);
        if (rr->position == 3 && commentary_busy == 0)
            say(SAY_THIRD, 15);
    }
    if (current_level < FIRST_BOWL_LEVEL && commentary_busy == 0 && wrong_way_wait == 0 && cb->forward_speed > 0xfa &&
        cb->motion_state == MOTION_DRIVING) {
        int32_t d = (Get_Car_Angle(car_states[0].angle[1] & ANGLE_MASK) & ANGLE_MASK) -
                    road_angle(strip_at(car_fd[0].strip)->heading), c;
        Get_Direction_Cosines(&d, &c);
        if (d > QUARTER_TURN && c > QUARTER_TURN) {
            say(SAY_WRONG_WAY, 55);
            wrong_way_wait = 1000;
        }
    }
    if (air_wait == 0 && cb->motion_state == MOTION_AIRBORNE && abs(car_states[me].position[1] - car_fd[me].y) > 0x5db) {
        if (commentary_busy == 0) {
            say(SAY_BIG_DROP, 55);
            air_wait = 15000;
        } else {
            air_wait = 0x96;
        }
        crowd_cheer = 1;
    }
    if (roof_wait == 0 && cb->motion_state == MOTION_STUCK) {
        if (commentary_busy == 0) {
            say(SAY_STUCK, 45);
            roof_wait = 25000;
        } else {
            roof_wait = 3000;
        }
        crowd_cheer = 1;
    }
    if (slow_remark_wait == 0 && commentary_busy == 0 && cb->speed < 0x14 && pit_in != 1) {
        say(SAY_SLOW, 40);
        slow_remark_wait = 25000;
    }
    if (bonnet_remarked == 0 && cb->bonnet_lost == 1) {
        crowd_cheer = 1;
        bonnet_remarked = 1;
        if (commentary_busy == 0)
            say(SAY_BONNET_LOST, 45);
    }
    if (boot_remarked == 0 && cb->boot_lost == 1) {
        crowd_cheer = 1;
        boot_remarked = 1;
        if (commentary_busy == 0)
            say(SAY_BOOT_LOST, 25);
    }
    if (fire_remarked == 0 && cb->fire_timer < 300) {
        if (commentary_busy == 0) {
            if ((cb->position[0] & 3) == 0)
                say(SAY_FIRE_A, 25);
            else
                say(SAY_FIRE_B, 45);
            fire_remarked = 1;
        }
        crowd_cheer = 1;
    }
    if (parts_remarked == 0 && cb->wheels_lost > 0 && cb->bonnet_lost == 1 && cb->boot_lost == 1) {
        crowd_cheer = 1;
        parts_remarked = 1;
        if (commentary_busy == 0)
            say(SAY_WRECKAGE, 55);
    }
    if (overlay_state == 0)
        crowd_cheer = 1;
    /* the grandstand's strips count on every level */
    uint16_t at = (uint16_t)car_fd[0].strip_number;
    if (crowd_cheer != 0 && (at < STAND_FIRST || at > STAND_LAST)) {
        crowd(crowd_volume + (SOUND_VOLUME_MAX - crowd_volume) * crowd_level / 64);
        if (crowd_rising == 1)
            crowd_level += 2;
        if (crowd_level == 0x40)
            crowd_rising = 0;
        if (crowd_rising == 0)
            crowd_level--;
        if (crowd_level < 1) {
            crowd_cheer = 0;
            crowd_rising = 1;
            crowd_level = 0;
        }
    }
    for (int car = 0; car < 20; car++)
        if (car_states[car].drawn == 1)
            cars_drawn++;
    if ((frame_count & 0x7f) == 0) {
        if (car_collisions > 8 && cars_drawn > 5)
            crowd_cheer = 1;
        if (commentary_busy == 0) {
            if (pileup_wait == 0 && car_collisions > 0xf && cars_drawn > 5) {
                say(SAY_WRECKAGE, 55);
                pileup_wait = 5000;
            }
            if (commentary_busy == 0 && crowd_remark_wait == 0 && car_collisions > 9 && cars_drawn > 5) {
                say((cb->position[0] & 3) == 0 ? SAY_PILE_UP_A : SAY_PILE_UP_B, 55);
                crowd_remark_wait = 5000;
            }
        }
        car_collisions = 0;
    }
    cars_drawn = 0;
    count_down(&unused_bc8);
    count_down(&crowd_remark_wait);
    count_down(&wrong_way_wait);
    count_down(&pileup_wait);
    count_down(&roof_wait);
    count_down(&air_wait);
    count_down(&slow_remark_wait);
    if (pit_in == 0)
        pit_remarked = 0;
    count_down(&barrier_remark_wait);
    count_down(&skid_sound_wait);
}
