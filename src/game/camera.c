#include "game/camera.h"

#include "game/car.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/replay.h"
#include "game/scene.h"
#include "game/sound_fx.h"
#include "graphics/object.h"
#include "psx/libgte.h"
#include "system/controller.h"

#define BUMPER_HEIGHT 0x50
#define CAMERA_BUTTONS (PAD_L2 | PAD_R2) /* mapped functions that switch bumper / chase view */
#define PIT_SECTIONS 6

/* Pit camera script events (a replay word: the event bits and an 11-bit repeat count) */
#define EVENT_NEXT 0x8000
#define EVENT_PREV 0x4000
#define EVENT_REPAIR 0x1000
#define EVENT_EXIT 0x0800
#define EVENT_REPEAT 0x7ff

int32_t chase_angle;
int32_t chase_height = CHASE_HEIGHT;
int32_t chase_distance = CHASE_DISTANCE;
int32_t chase_angle_offset;
int32_t look_height = LOOK_HEIGHT;
int32_t look_back;
int32_t look_back_count;
int32_t chase_far;
int32_t camera_position[4];
TrackFd camera_fd;
int32_t camera_ground;
TrackFd floaty_fd;

int32_t pit_camera_active;
int32_t pit_camera_state;
int32_t pit_camera_entered;
int32_t old_camera_mode;
int32_t pit_section;
int32_t pit_bounce;
int32_t pit_turns;
int32_t pit_camera_done;
int32_t pit_camera_busy;

/* Camera angle per car section; the original reads one entry beyond either end when the section
 * wraps (pit_bounce takes it), so those neighbours are kept. */
static const int32_t pit_angles[PIT_SECTIONS + 2] = {0x02260007, 512, 1024, 1536, 2560, 3072, 3584, 6000};
#define PIT_ANGLE(section) pit_angles[(section) + 1]

/* The floaty camera's script: mode, -, angle step, height, distance, car, frames; mode -1 ends it.
 * Mode 0 rides on the car, 1 chases it, 2 uses the trackside rails passed in. */
typedef struct {
    int32_t mode, unused, angle_step, height, distance, car, frames;
} FloatyShot;

static const FloatyShot floaty_script[] = { /* 0x8006DDEC */
    {2, 0, 0, 0, 0, 0, 1},
    {-1, 0, 0, 0, 0, 0, 0},
};

/* Bowls: the floaty camera's corner positions per level and quadrant, and each quadrant's area. */
static const int32_t bowl_cameras[4][4][4] = { /* 0x8006E958: levels 8..11 */
    {{6000, 600, 5000, 0}, {-6000, 300, 6000, 0}, {2000, 1000, -2000, 0}, {-5000, 500, -6000, 0}},
    {{6000, 0, 6000, 0}, {-6000, 0, 6000, 0}, {2000, 0, -2000, 0}, {-6000, 0, -6000, 0}},
    {{6000, -1300, 6000, 0}, {-6000, -1000, 6000, 0}, {6000, 0, -6000, 0}, {-6000, -500, -6000, 0}},
    {{6000, 1000, 6000, 0}, {-6000, 1000, 6000, 0}, {3000, 1000, -1000, 0}, {-6000, 1000, -6000, 0}},
};
static const int32_t bowl_quadrants[4][4] = { /* 0x8006EA58: min x, min z, max x, max z */
    {-1000, -1000, 32768, 32768},
    {-32768, -1000, 1000, 32768},
    {-1000, -32768, 32768, 1000},
    {-32768, -32768, 1000, 1000},
};

static int32_t floaty_timer;    /* 0x800791AC */
static int32_t floaty_index = -1; /* 0x800791B0: the first shot is 0, the step comes first */
static int32_t floaty_car;      /* 0x800791B4 */
static int32_t floaty_rail = -1;  /* 0x800791B8 */
static int32_t floaty_quadrant; /* 0x800791BC */

/* From inside the car: at its position, raised, turned to its body angles. */
static void bumper_camera(int car)
{
    const CarState *cs = &car_states[car];
    int32_t pos[4] = {cs->position[0], cs->position[1] + BUMPER_HEIGHT, cs->position[2], cs->position[3]};
    int16_t angles[3];
    Set_World_Position(pos);
    Calc_Object_Angles(&cs->matrix, angles);
    angles[0] = (int16_t)(angles[0] + HALF_TURN);
    angles[1] = (int16_t)(HALF_TURN - angles[1]);
    angles[2] = (int16_t)-angles[2];
    Set_World_View(angles);
}

void Car_Camera(int car)
{
    if (replay_playback != 0) {
        if (current_level >= LEVEL_PINE_HILLS && current_level <= LEVEL_MOTORPLEX)
            Update_Chase_Camera(0);
        else
            Do_The_Floaty_Camera_Thing(NULL, 0);
        return;
    }
    if (camera_mode != 0) {
        Update_Chase_Camera(car);
        return;
    }
    /* Falling off the stadium bowl: switch to the chase camera where the car went over. */
    if (current_level == LEVEL_DEATH_BOWL && car_states[car].position[1] < -100) {
        for (int i = 0; i < 4; i++)
            camera_position[i] = car_states[car].position[i];
        camera_mode = 1;
    }
    overlay_hold = 0;
    bumper_camera(car);
}

void Camera_Pad_Control(int car)
{
    if (pit_camera_active == 0) {
        if (replay_playback == 0 && (pad.mapped_pressed & CAMERA_BUTTONS) != 0) {
            chase_height = CHASE_HEIGHT;
            chase_distance = CHASE_DISTANCE;
            look_height = LOOK_HEIGHT;
            chase_angle = car_blocks[car].angle[1];
            chase_far = 0;
            chase_angle_offset = 0;
            look_back_count = 0;
            look_back = 0;
            camera_mode ^= 1;
        }
    } else if (pit_camera_state == 0) {
        Init_Pit_Camera();
    } else if (pit_camera_state == 1) {
        Pit_Camera_Control(car);
    } else if (pit_camera_state == 2) {
        Exit_Pit_Camera();
    }
}

void Init_Pit_Camera(void)
{
    if (pit_camera_entered == 0) {
        old_camera_mode = camera_mode;
        if (camera_mode == 0)
            camera_mode = 1;
        pit_camera_entered = 1;
        if (replay_pad_type != PAD_DIGITAL)
            replay_action_repeat = 0;
    }
    pit_camera_state = 1;
    chase_angle_offset = 0;
}

static void leave_pit_camera(void)
{
    pit_camera_state = 2;
    if (replay_pad_type != PAD_DIGITAL)
        replay_action_repeat = 0;
    chase_angle_offset = 0;
    SetHighLight(pit_section, 0);
    camera_mode = old_camera_mode;
    pit_section = 0;
    pit_bounce = 0;
    pit_turns = 0;
    pit_camera_entered = 0;
    pit_camera_active = 0;
    pit_camera_done = 1;
    pit_camera_state = 0;
    pit_camera_busy = 0;
}

void Exit_Pit_Camera(void)
{
    leave_pit_camera();
}

static void pit_next_section(void)
{
    SetHighLight(pit_section, 0);
    pit_section++;
    pit_bounce = (int16_t)PIT_ANGLE(pit_section);
    if (pit_section > PIT_SECTIONS - 1) {
        pit_section = 0;
        pit_turns++;
    }
}

static void pit_prev_section(void)
{
    SetHighLight(pit_section, 0);
    pit_section--;
    pit_bounce = (int16_t)PIT_ANGLE(pit_section);
    if (pit_section < 0) {
        pit_section = PIT_SECTIONS - 1;
        pit_turns--;
    }
}

/* Highlights the section, and turns the camera towards it by an eighth of the way each frame. */
static void pit_turn_camera(void)
{
    SetHighLight(pit_section, 1);
    if (pit_bounce > 0)
        pit_bounce -= 0x10;
    else if (pit_bounce < 0)
        pit_bounce += 0x10;
    uint32_t target = (uint32_t)PIT_ANGLE(pit_section);
    int32_t offset = chase_angle_offset;
    if (((uint32_t)offset & ANGLE_MASK) == target)
        return;
    int32_t goal = pit_turns * TURN;
    if (offset < (int32_t)(goal + target)) {
        int d = (int16_t)((uint16_t)target + goal - (offset & 0xffff));
        chase_angle_offset = d > 7 ? offset + (d >> 3) : offset + 1;
    } else {
        int d = (int16_t)((offset & 0xffff) - ((uint16_t)target + goal));
        chase_angle_offset = d > 7 ? offset - (d >> 3) : offset - 1;
    }
}

/* Left / right pick the car section to repair, Cross repairs it, Square leaves. In a replay the same
 * choices come from the script; while racing they are recorded into it. */
void Pit_Camera_Control(int car)
{
    uint16_t event = 0;
    TrackFd *fd = &car_fd[car];
    if (replay_playback != 0) {
        if ((replay_script_ptr == replay_script + REPLAY_SCRIPT_WORDS || replay_script_ptr == replay_script_end) && replay_ended == 0) {
            replay_ended = 1;
            quit_flag = 1;
            return;
        }
        if (replay_action_repeat == 0) {
            replay_script_ptr++;
            replay_action_repeat = *replay_script_ptr & EVENT_REPEAT;
        }
        event = *replay_script_ptr;
        replay_action_repeat--;
        if (event & EVENT_NEXT)
            pit_next_section();
        if (event & EVENT_PREV)
            pit_prev_section();
        if (event & EVENT_REPAIR) {
            Allocate_Sound_Effect(SAMPLE_CRASH, CAR_SOUND_VOLUME, &fd->x);
            UndentCar(car, pit_section);
        }
        pit_turn_camera();
        if (event & EVENT_EXIT)
            leave_pit_camera();
        return;
    }
    if (pad.pressed & PAD_LEFT) {
        pit_next_section();
        event = EVENT_NEXT;
    }
    if (pad.pressed & PAD_RIGHT) {
        pit_prev_section();
        event |= EVENT_PREV;
    }
    if (pad.pressed & PAD_CROSS) {
        Allocate_Sound_Effect(SAMPLE_CRASH, CAR_SOUND_VOLUME, &fd->x);
        event |= EVENT_REPAIR;
        UndentCar(car, pit_section);
    }
    pit_turn_camera();
    if (pad.held & PAD_SQUARE) {
        event |= EVENT_EXIT;
        leave_pit_camera();
    }
    if (replay_pad_type == PAD_DIGITAL)
        Record_Event(event, &pad);
    else
        *++replay_script_ptr = (uint16_t)(event + 1);
}

void Init_The_Floaty_Camera(int car)
{
    chase_height = 0xa0;
    chase_angle_offset = 0x758;
    chase_distance = 0x2e8;
    floaty_fd = car_fd[car];
}

/* A trackside rail: a segment the camera slides along to stay level with the car, used while the car
 * is between two strip numbers. */
typedef struct {
    int32_t start[4];
    int32_t end[3];
    int16_t first_strip, last_strip;
} CameraRail;

/* The original passes the car's fd record to Init_Scene_Objects, which ignores it and loads the
 * scenery around camera_fd_pt. */
static void reload_scenery(void)
{
    Remove_Scene_Objects();
    Init_Scene_Objects();
}

/* Replays and the attract mode: on a track, follows the script (chase or trackside rails); in a bowl,
 * watches from the corner of the quadrant the car is in. Returns the car watched. */
int Do_The_Floaty_Camera_Thing(const int32_t *rails, int count)
{
    const CarState *cs;
    if (current_level >= FIRST_BOWL_LEVEL) {
        cs = &car_states[floaty_car];
        const int32_t *q = bowl_quadrants[floaty_quadrant];
        if (cs->position[0] < q[0] || cs->position[2] < q[1] || q[2] < cs->position[0] || q[3] < cs->position[2]) {
            if (cs->position[0] < 0)
                floaty_quadrant = cs->position[2] < 0 ? 3 : 1;
            else
                floaty_quadrant = cs->position[2] < 0 ? 2 : 0;
        }
        Set_World_Position(bowl_cameras[current_level - FIRST_BOWL_LEVEL][floaty_quadrant]);
        int32_t target[4] = {cs->position[0], cs->position[1] + look_height, cs->position[2], cs->position[3]};
        Point_Camera(target, HALF_TURN);
        return floaty_car;
    }
    if (floaty_timer < 1) {
        floaty_index++;
        if (floaty_script[floaty_index].mode == -1)
            floaty_index = 0;
        chase_height = 0xa0;
        chase_angle_offset = 0x758;
        floaty_car = floaty_script[floaty_index].car;
        chase_distance = 0x2e8;
        floaty_timer = floaty_script[floaty_index].frames;
        floaty_fd = car_fd[floaty_car];
        reload_scenery();
    }
    const FloatyShot *shot = &floaty_script[floaty_index];
    cs = &car_states[floaty_car];
    if (shot->mode == 1) {
        chase_distance = shot->distance;
        chase_height = shot->height;
        chase_angle_offset += shot->angle_step;
        Update_Chase_Camera(floaty_car);
    } else if (shot->mode == 0) {
        bumper_camera(floaty_car);
    } else if (shot->mode == 2) {
        int16_t strip = car_fd[floaty_car].strip_number;
        const CameraRail *rail = (const CameraRail *)rails;
        for (int i = 0; i < count; i++, rail++) {
            if (strip < rail->first_strip || rail->last_strip < strip)
                continue;
            int32_t ax = rail->start[0], ay = rail->start[1], az = rail->start[2];
            int32_t bx = rail->end[0], by = rail->end[1], bz = rail->end[2];
            int32_t len = SquareRoot0((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
            if (len < 1)
                len = 1;
            int32_t along = ((cs->position[0] - ax) * (bx - ax) + (cs->position[2] - az) * (bz - az)) / len;
            int32_t t = (along << 12) / len;
            floaty_fd.x = ((bx - ax) * t >> 12) + ax;
            floaty_fd.y = ((by - ay) * t >> 12) + ay;
            floaty_fd.z = ((bz - az) * t >> 12) + az;
            Set_World_Position(&floaty_fd.x);
            int32_t target[4] = {cs->position[0], cs->position[1] + look_height, cs->position[2], cs->position[3]};
            Point_Camera(target, HALF_TURN);
            Track_Follow(&floaty_fd);
            if (floaty_rail != i) {
                reload_scenery();
                floaty_rail = i;
            }
            return floaty_car;
        }
    }
    return floaty_car;
}
