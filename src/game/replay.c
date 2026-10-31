#include "game/replay.h"

#include "game/ai.h"
#include "game/camera.h"
#include "game/car.h"
#include "game/car_cluts.h"
#include "game/denting.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/track.h"

#include <stdio.h>

int32_t replay_pad_type;
int32_t replay_record_pad_type;
int32_t replay_action_repeat;
uint16_t *replay_script_ptr;
uint16_t *replay_script_end;
int32_t replay_ended;
int32_t replay_started;
int32_t replay_invalid;
int32_t replay_level;

/* Playback starts one word before the script (it reads ahead), so the buffer has a word in front. */
static uint16_t replay_words[1 + REPLAY_SCRIPT_WORDS];
uint16_t *const replay_script = &replay_words[1];

/* Event bits. Digital: left, right, sharp steering (L1/R1), accelerate, brake; analog: the steering in
 * the low 6 bits, then brake and accelerate. */
#define EVENT_LEFT 0x8000
#define EVENT_RIGHT 0x4000
#define EVENT_SHARP 0x2000
#define EVENT_ACCELERATE 0x1000
#define EVENT_BRAKE 0x800
#define EVENT_ANALOG_ACCELERATE 0x80
#define EVENT_ANALOG_BRAKE 0x40
#define EVENT_BITS 0xf800  /* digital: the event bits of a word ... */
#define EVENT_COUNT 0x7ff  /* ... and how many frames it lasts */

/* Game functions (PadState.mapped), on the bits of their default buttons. */
#define BUTTON_SHARP_STEER (PAD_L1 | PAD_R1)
#define BUTTON_BRAKE PAD_SQUARE
#define BUTTON_ACCELERATE PAD_CROSS

void Init_Replay_Script(void)
{
    if (replay_playback == 0) {
        for (int i = 0; i < REPLAY_SCRIPT_WORDS; i++)
            replay_script[i] = 0;
    }
    replay_script_ptr = replay_playback != 0 ? replay_script - 1 : replay_script;
    replay_started = 0;
    replay_action_repeat = 0;
    replay_ended = 0;
    replay_invalid = 0;
    replay_level = current_level;
}

/* Adds a frame of `event`: counts up the current word while the event repeats, else starts a new one. */
void Record_Event(uint16_t event, const PadState *p)
{
    if (replay_invalid != 0)
        return;
    if (replay_script_ptr == replay_script + REPLAY_SCRIPT_WORDS) {
        if (replay_ended == 0) {
            printf("Out Of Replay Memory!\n\n");
            replay_ended = 1;
        }
        replay_record_pad_type = p->type;
        return;
    }
    uint16_t *w = replay_script_ptr;
    if (p->type == PAD_DIGITAL) {
        if (event != (*w & EVENT_BITS) && replay_started != 0) {
            *++w = (uint16_t)(event + 1);
        } else {
            int16_t count = (int16_t)((*w & EVENT_COUNT) + 1);
            if (count == EVENT_COUNT) {
                *w = (uint16_t)(event + EVENT_COUNT);
                *++w = (uint16_t)(event + 1);
            } else {
                *w = (uint16_t)(event + count);
            }
        }
    } else {
        int16_t count = (int16_t)((*w & 0xff00) + 0x100);
        if ((event == (*w & 0xff) || replay_started == 0) && count != -0x100) {
            *w = (uint16_t)(event + count);
        } else {
            if (event == (*w & 0xff) || replay_started == 0)
                *w = (uint16_t)(event - 0x100);
            *++w = (uint16_t)(event + 0x100);
        }
    }
    replay_script_ptr = w;
    replay_script_end = w;
    replay_record_pad_type = p->type;
}

/* Digital steering: a press turns the wheel 0x20 a frame (0x40 with the sharp-steer buttons) up to
 * lock; with neither direction it centres by 0x20 a frame. */
static void digital_steer(CarBlock *cb, int left, int right, int sharp)
{
    int32_t step = sharp ? 0x40 : 0x20, lock = sharp ? 0x1ff : 0x100;
    if (left) {
        cb->steering -= step;
        if (cb->steering < -lock)
            cb->steering = -lock;
    }
    if (right) {
        cb->steering += step;
        if (cb->steering > lock)
            cb->steering = lock;
    }
    if (!left && !right) {
        int32_t s = cb->steering;
        if (s < -0x20 || s > 0x20)
            cb->steering = s >= 0 ? s - 0x20 : s + 0x20;
        else
            cb->steering = 0;
    }
}

/* The car's steering and throttle from the pad, recorded into the replay; in a replay, from the
 * script. */
void Control_Car_Replay(int car, const PadState *p)
{
    CarBlock *cb = &car_blocks[car];
    int type = (int8_t)p->type;
    if (replay_playback == 0) {
        uint16_t buttons = p->mapped;
        uint16_t event = 0;
        if (type == PAD_DIGITAL) {
            replay_pad_type = type;
            int sharp = (buttons & BUTTON_SHARP_STEER) != 0;
            if (buttons & PAD_LEFT)
                event = sharp ? EVENT_LEFT | EVENT_SHARP : EVENT_LEFT;
            if (buttons & PAD_RIGHT)
                event |= sharp ? EVENT_RIGHT | EVENT_SHARP : EVENT_RIGHT;
            digital_steer(cb, buttons & PAD_LEFT, buttons & PAD_RIGHT, sharp);
        } else if (type == PAD_NEGCON || type == PAD_ANALOG) {
            int32_t u = p->steer - 0x80;
            event = (uint16_t)(((uint32_t)u >> 2) & 0x3f);
            cb->steering = (u & ~3) * 4;
            replay_pad_type = type;
        }
        if (buttons & BUTTON_BRAKE) {
            cb->throttle = -0x8000;
            event |= type == PAD_DIGITAL ? EVENT_BRAKE : EVENT_ANALOG_BRAKE;
        } else if (buttons & BUTTON_ACCELERATE) {
            cb->throttle = 0x8000;
            event |= type == PAD_DIGITAL ? EVENT_ACCELERATE : EVENT_ANALOG_ACCELERATE;
        } else {
            cb->throttle = 0;
        }
        Record_Event(event, p);
        replay_started = 1;
        return;
    }
    if (replay_invalid != 0) {
        if (replay_ended == 0) {
            printf("Invalid Replay Data!!\n\n");
            replay_ended = 1;
        }
        return;
    }
    if (replay_script_ptr == replay_script + REPLAY_SCRIPT_WORDS || replay_script_ptr == replay_script_end) {
        if (replay_ended == 0) {
            replay_ended = 1;
            quit_flag = 1;
            return;
        }
        replay_started = 1;
        return;
    }
    uint16_t event;
    if (replay_pad_type == PAD_DIGITAL) {
        if (replay_action_repeat == 0)
            replay_action_repeat = *++replay_script_ptr & EVENT_COUNT;
        event = *replay_script_ptr & EVENT_BITS;
    } else {
        if (replay_action_repeat == 0)
            replay_action_repeat = *++replay_script_ptr >> 8;
        event = *replay_script_ptr & 0xff;
    }
    replay_action_repeat--;
    if (replay_pad_type == PAD_DIGITAL) {
        digital_steer(cb, event & EVENT_LEFT, event & EVENT_RIGHT, (event & EVENT_SHARP) != 0);
        if (event & EVENT_ACCELERATE)
            cb->throttle = 0x8000;
        else if (event & EVENT_BRAKE)
            cb->throttle = -0x8000;
        else
            cb->throttle = 0;
    } else {
        int32_t steer = event & 0x3f;
        cb->steering = (steer >= 0x20 ? steer - 0x40 : steer) * 16;
        if (event & EVENT_ANALOG_ACCELERATE)
            cb->throttle = 0x8000;
        else if (event & EVENT_ANALOG_BRAKE)
            cb->throttle = -0x8000;
        else
            cb->throttle = 0;
    }
    replay_started = 1;
}

void Terminate_Replay(const PadState *p)
{
    *++replay_script_ptr = p->type == PAD_DIGITAL ? 0xa001 : 0x8080;
    replay_script_end = replay_script_ptr;
}

/* A replay with no events (nothing was recorded): one frame of an idle event. */
void Terminate_Replay_Bodge(const PadState *p)
{
    replay_script_ptr = replay_script;
    replay_script[0] = p->type == PAD_DIGITAL ? 0xa001 : 0x8080;
    replay_script_end = replay_script;
}

/* One step of the pit crew's repair: a zone's damage down by 400. */
static void repair(int32_t *damage)
{
    if (*damage <= 400)
        *damage = 0;
    else
        *damage -= 400;
}

/* The pit crew works on one section of the car (the pit camera's sections, 0..5), and the dents of the
 * side they belong to are reshaped. The model reshaped is always car 0's, as on the PSX. */
void UndentCar(int car, int section)
{
    int32_t *damage = car_blocks[car].damage;
    int zone;
    switch (section) {
    case 0:
        repair(&damage[7]);
        repair(&damage[8]);
        zone = 1;
        break;
    case 1:
        repair(&damage[9]);
        zone = 2;
        break;
    case 2:
        repair(&damage[0]);
        repair(&damage[1]);
        zone = 0;
        break;
    case 3:
        repair(&damage[2]);
        repair(&damage[3]);
        zone = 0;
        break;
    case 4:
        repair(&damage[4]);
        zone = 3;
        break;
    case 5:
        repair(&damage[5]);
        repair(&damage[6]);
        zone = 1;
        break;
    default:
        zone = -1;
        break;
    }
    if (zone >= 0) {
        if (current_player_car == 0)
            Dent_Car_Zone(&car_objects[0][0], &car_blocks[0], zone, 0, 0);
        else
            Dent_Car_Zone(&car_objects[0][1], &car_blocks[0], zone, 0xb, 0);
    }
    if (damage[0] < 0xe64 && damage[2] < 0xe64)
        car_blocks[car].heavy_damage = 0;
}

static int32_t highlight_level = 0xff; /* 0x8007972C */

/* Lights the near model's areas that make up one pit section. */
static void highlight_section(int section, uint8_t level)
{
    static const int8_t areas[6][4] = {
        {0, 8, 9, -1}, {1, -1}, {2, 3, 4, -1}, {3, 4, 5, -1}, {6, -1}, {7, 8, 9, -1},
    };
    if (section < 0 || section > 5)
        return;
    for (const int8_t *a = areas[section]; *a != -1; a++)
        Highlight_Area(0, *a, level);
}

/* The pit section being repaired pulses (`flashing`), or goes back to normal. */
void SetHighLight(int section, int flashing)
{
    if (flashing == 0) {
        highlight_section(section, 0x80);
        highlight_level = 0xff;
        return;
    }
    highlight_level -= 10;
    if (highlight_level > 0x80)
        highlight_section(section, (uint8_t)highlight_level);
    if (highlight_level < 0x80)
        highlight_level = 0xff;
}

/* The player's car steers itself down the pit lane (keeping to the lane next to the strip's edge) and
 * stops in the pit while the repair camera runs; after it, it pulls away hard. Stuck against a barrier
 * for 30 frames, it gives up the pit. Some reads are car 0's whichever car is driven, as on the PSX. */
void Drive_Car_Into_Pit(int car)
{
    CarBlock *player = &car_blocks[0];
    const Strip *strip = strip_at(car_fd[car].strip);
    if (player->barrier_hit != 0 && ++pit_camera_busy >= 0x1f) {
        pit_in = 0;
        pit_camera_busy = 0;
        return;
    }
    int lane = strip->lane_base + 1;
    int bias = 0;
    if (player->lane > lane)
        bias = -0x10;
    if (player->lane < lane)
        bias = 0x10;
    int32_t angle = Get_Car_Angle(car_states[0].angle[1] & ANGLE_MASK);
    int32_t off = ((angle + car_blocks[car].steering) & ANGLE_MASK) - road_angle(bias + strip->heading);
    int32_t cosine;
    Get_Direction_Cosines(&off, &cosine);
    if (off <= HALF_TURN) {
        if (off <= QUARTER_TURN)
            Interpolate_Direction_Vectors_Left(&car_blocks[car], off, 0);
    } else if (off >= HALF_TURN + QUARTER_TURN) {
        Interpolate_Direction_Vectors_Right(&car_blocks[car], cosine, 0);
    }
    if (pit_camera_active != 0) {
        player->throttle = 0;
        player->forward_speed = 0;
        player->velocity[0] = 0;
    } else if (pit_camera_done != 0) {
        player->throttle = 0x3000;
    } else {
        player->throttle = player->forward_speed < 0x65 || bias != 0 ? 0x1000 : -0x2000;
    }
}
