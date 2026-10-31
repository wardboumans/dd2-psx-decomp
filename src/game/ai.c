#include "game/ai.h"

#include "game/car.h"
#include "game/car_motion.h"
#include "game/frontend.h"
#include "game/league.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/track.h"
#include "graphics/object.h"
#include "psx/libgte.h"
#include "system/maths.h"

#include <stdlib.h>

PointyRecord pointy_bits[20];
int32_t ai_target_car;
int32_t ai_segment;

/* Command scripts the computer drivers run when they get stuck or knocked off line, one word per
 * command and its operands. The two forward scripts are the same; Determine_AI tells them apart to
 * alternate. */
enum {
    AI_BRAKE = 1,         /* brake to a stop */
    AI_HOLD = 2,          /* no handler: the script stays on it */
    AI_CHOOSE = 3,        /* Determine_AI picks the next script */
    AI_THROTTLE = 4,      /* n: set the throttle */
    AI_TURN_TO_ROAD = 5,  /* drive on until lined up with the road */
    AI_REVERSE_RIGHT = 6, /* reverse steering right until lined up */
    AI_REVERSE_LEFT = 7,
    AI_WAIT = 8,          /* n: wait n frames */
    AI_LOCK_RIGHT = 9,    /* full lock right until slow */
    AI_LOCK_LEFT = 10,
    AI_SPEED_LIMIT = 11,  /* n: set the speed limit */
    AI_END = 13,          /* back to driving */
};

const int32_t ai_unstick_script[6] = {AI_BRAKE, AI_CHOOSE, AI_HOLD, AI_END, AI_CHOOSE, AI_END}; /* 0x8006F8F0 */
static const int32_t cmd_forward_1[] = {AI_SPEED_LIMIT, 80, AI_THROTTLE, 5000, AI_TURN_TO_ROAD, AI_END}; /* 0x8006F908 */
static const int32_t cmd_forward_2[] = {AI_SPEED_LIMIT, 80, AI_THROTTLE, 5000, AI_TURN_TO_ROAD, AI_END}; /* 0x8006F920 */
static const int32_t cmd_reverse_right[] = {AI_SPEED_LIMIT, -80, AI_THROTTLE, -5000, AI_REVERSE_RIGHT, AI_END}; /* 0x8006F938 */
static const int32_t cmd_reverse_left[] = {AI_SPEED_LIMIT, -80, AI_THROTTLE, -5000, AI_REVERSE_LEFT, AI_END}; /* 0x8006F950 */
static const int32_t cmd_dodge_right[] = {AI_THROTTLE, -48000, AI_LOCK_RIGHT, AI_CHOOSE, AI_END}; /* 0x8006F968 */
static const int32_t cmd_dodge_left[] = {AI_THROTTLE, -48000, AI_LOCK_LEFT, AI_CHOOSE, AI_END};   /* 0x8006F97C */

#define SCRIPT_FRAMES 175   /* ai_timer after Determine_AI starts a script */
#define AIM_FRAMES 125      /* ai_aim_timer after a script ends: steer by the strip's heading */
#define SWERVE_COOLDOWN 75  /* ai_swerve_cooldown after a swerve (PERSONALITY_COOLDOWN) */

/* Per car (by grid number). */
static const int32_t ai_grip[20] = { /* 0x8006F990 */
    44000, 54000, 52000, 54000, 54000, 52000, 50000, 54000, 52000, 54000,
    52000, 52000, 52000, 52000, 52000, 52000, 54000, 52000, 54000, 54000,
};
/* The speed handicap, from the lowest division to the highest. */
static const int32_t handicap_low[20] = { /* 0x8006F9E0 */
    0, -1756, -2063, -1756, -1960, -2268, -2780, -1858, -1858, -2575,
    -1960, -2165, -2370, -2268, -2063, -2472, -1858, -2165, -1960, -1960,
};
static const int32_t handicap_high[20] = { /* 0x8006FA30 */
    0, 256, 0, 256, 0, 0, -1024, 0, 0, -256, -256, 103, -512, 0, -512, -1024, -1024, 0, -512, -512,
};
/* Driving habits (bits), by division, then for the stock car and derby race modes. */
#define PERSONALITY_COOLDOWN 0x01       /* waits SWERVE_COOLDOWN before swerving again */
#define PERSONALITY_DODGE 0x04          /* dodges a much slower car 8 strips ahead */
#define PERSONALITY_BRAKE 0x08          /* brakes hard behind a slower car */
#define PERSONALITY_BLOCK 0x10          /* moves across in front of the player */
#define PERSONALITY_BLOCK_HARD 0x20     /* the same, twice as far */
#define PERSONALITY_IGNORE_CROSSING 0x40 /* ignores a car ahead that is crossing the road */
#define PERSONALITY_WEAVE 0x80          /* weaves about its line */
#define PERSONALITY_BLOCK_CLOSE 0x100   /* blocks only from 3 strips behind instead of 8 */
static const int32_t personalities[5][20] = { /* 0x8006FA80 */
    {0, 8, 16, 8, 21, 96, 128, 64, 0, 36, 0, 0, 16, 0, 0, 64, 8, 0, 24, 0},
    {0, 280, 16, 280, 5, 96, 128, 336, 0, 36, 0, 0, 17, 16, 13, 96, 8, 0, 280, 0},
    {0, 280, 17, 280, 5, 353, 128, 96, 72, 37, 68, 17, 273, 272, 13, 96, 8, 32, 24, 272},
    {0, 280, 17, 280, 5, 353, 128, 96, 72, 293, 68, 273, 273, 272, 13, 96, 8, 32, 24, 272},
    {0, 8, 1, 8, 0, 5, 128, 0, 12, 4, 0, 13, 13, 8, 1, 8, 12, 8, 4, 4},
};

/* The corners of the track levels: from each level's start, the speed to brake down to in a corner,
 * then (24 words on) the speed above which the car steers hard into it, then (48 on) which way it
 * turns (1 left, 2 right). The levels' lists overlap; those of levels 2, 3 and 7 run on into the bowl
 * radius and barrier edge tables that follow in memory (0x8006FD10), whose words are repeated here. */
static const int32_t corner_data[88] = { /* 0x8006FC10 */
    0, 460, 8192, 8192, 0, 8192, 460, 8192, 8192, 340, 0, 380, 360, 8192, 380, 8192,
    0, 490, 0, 512, 0, 640, 0, 640, 8192, 460, 8192, 8192, 8192, 360, 350, 8192,
    8192, 8192, 8192, 380, 360, 8192, 240, 340, 8192, 480, 8192, 512, 8192, 640, 8192, 640,
    0, 1, 1, 1, 0, 1, 1, 2, 1, 1, 0, 2, 2, 2, 2, 2,
    0, 2, 0, 1, 0, 2, 0, 2, 14990, 14600, 14400, 14400, 0, 0, 0, 0,
    1, 0, 0, 0, 1, 0, 0, 1,
};
static const uint8_t corner_start[8] = {0, 0, 20, 16, 10, 4, 18, 22};
#define CORNER_LIMITS 24
#define CORNER_TYPES 48

static const int32_t *corner_speeds; /* 0x80079B18 */
static const int32_t *corner_limits; /* 0x80079AA4 */
static const int32_t *corner_types;  /* 0x80079B20 */

/* The bowls: the cars still driving (number + 1), refilled every frame. */
static uint8_t bowl_cars[20];    /* 0x800A7028 */
static uint8_t *bowl_cars_end;   /* 0x80079B44 */

#define BOWL_BACK_OFF 100 /* ai_bowl_target while backing away after a hit */

/* The lanes of the strip a car is on (quad numbers carried across strips). */
typedef struct {
    int lane, last, first, mid, width;
} Lanes;

/* The quad of a lane; the original reads past either end of the strip's quads for the neighbours of
 * the outer lanes. */
static const StripQuad *lane_quad(const Strip *s, int seg)
{
    return (const StripQuad *)(s + 1) + seg;
}

/* The heading measured the other way round (half a turn stays). */
int32_t Get_Car_Angle(int32_t a)
{
    if (a > HALF_TURN || a < HALF_TURN)
        a = TURN - a;
    return a;
}

/* An angle difference made positive, and its distance from a full turn. */
void Get_Direction_Cosines(int32_t *angle, int32_t *cosine)
{
    *cosine = TURN - abs(*angle);
    if (*angle < 0) {
        *cosine = abs(*angle);
        *angle += TURN;
    }
}

static int Angle_Quadrant(int32_t a)
{
    if (a > HALF_TURN && a < HALF_TURN + QUARTER_TURN)
        return 1;
    if (a <= HALF_TURN)
        return a > QUARTER_TURN ? 3 : 0;
    return 2;
}

/* Turns the wheel left by `step` (0: by how far off the car is), no further than `angle`. */
void Interpolate_Direction_Vectors_Left(CarBlock *cb, int32_t angle, int32_t step)
{
    if (step == 0)
        step = 0x42 - rsin(angle / 2) * 64 / 4096;
    int32_t was = cb->steering;
    cb->steering = was - step;
    if (was - step < -STEER_LOCK)
        cb->steering = -STEER_LOCK;
    if (cb->steering - was < -angle)
        cb->steering = was - angle;
}

void Interpolate_Direction_Vectors_Right(CarBlock *cb, int32_t angle, int32_t step)
{
    if (step == 0)
        step = 0x42 - rsin(angle / 2) * 64 / 4096;
    int32_t was = cb->steering;
    cb->steering = was + step;
    if (was + step > STEER_LOCK)
        cb->steering = STEER_LOCK;
    if (cb->steering - was > angle)
        cb->steering = was + angle;
}

/* Strips from `from` forward to `to`, round the lap. */
static int32_t Strip_Distance(int32_t from, int32_t to)
{
    int32_t d = to - from;
    if (d < 0)
        d = num_strips - from + to;
    return d;
}

/* The throttle for a speed `limit` (the strip's): brake hard above it, else drive at the car's own
 * pace. */
static int32_t Recommended_Acceleration(int32_t limit, int32_t speed, int32_t pace)
{
    if (limit * 4 < speed)
        return (limit * 4 - speed) * 0x200;
    return pace + limit * 0x80;
}

static int32_t car_heading(int car)
{
    return Get_Car_Angle(car_states[car].angle[1] & ANGLE_MASK);
}

static void start_script(CarBlock *cb, const int32_t *script)
{
    cb->ai_commands = script;
    cb->ai_command = script;
}

/* Runs `script`, or `other` when that one is already running. */
static void pick(CarBlock *cb, const int32_t *script, const int32_t *other)
{
    start_script(cb, cb->ai_commands == script ? other : script);
    cb->ai_timer = SCRIPT_FRAMES;
}

static void keep(CarBlock *cb, const int32_t *script)
{
    if (cb->ai_commands == script)
        return;
    start_script(cb, script);
    cb->ai_timer = SCRIPT_FRAMES;
}

/* How to get going again, from which way the car faces against the road and which lane it is in. */
static void Determine_AI(CarBlock *cb, int32_t angle, int32_t cosine, const Lanes *l)
{
    switch (Angle_Quadrant(angle)) {
    case 0:
        if (l->lane == l->first && angle > 0x80)
            keep(cb, cmd_reverse_right);
        else
            pick(cb, cmd_forward_2, cmd_reverse_right);
        break;
    case 1:
        if (l->lane == l->first) {
            pick(cb, cmd_forward_1, cmd_reverse_left);
        } else if (l->mid < l->lane) {
            if (l->lane == l->last ? cosine < 0x781 : l->width < 6)
                keep(cb, cmd_reverse_left);
            else
                pick(cb, cmd_forward_2, cmd_reverse_right);
        } else if (l->width < 6) {
            keep(cb, cmd_reverse_right);
        } else {
            pick(cb, cmd_forward_1, cmd_reverse_left);
        }
        break;
    case 2:
        if (l->lane == l->last && cosine > 0x80)
            keep(cb, cmd_reverse_left);
        else
            pick(cb, cmd_forward_1, cmd_reverse_left);
        break;
    default:
        if (l->lane == l->first) {
            if (angle < 0x781)
                keep(cb, cmd_reverse_right);
            else
                pick(cb, cmd_forward_1, cmd_reverse_left);
        } else if (l->lane != l->last) {
            keep(cb, l->mid < l->lane ? cmd_reverse_left : cmd_reverse_right);
        } else {
            pick(cb, cmd_forward_2, cmd_reverse_right);
        }
        break;
    }
}

/* The car's heading (with its steering, if asked) against the road's, made positive, and its distance
 * from a full turn. */
static void heading_error(int car, int32_t road, int steered, int32_t *angle, int32_t *cosine)
{
    *angle = ((car_heading(car) + (steered ? car_blocks[car].steering : 0)) & ANGLE_MASK) - road;
    Get_Direction_Cosines(angle, cosine);
}

static void choose_script(int car, int32_t road, const Lanes *l)
{
    int32_t a, c;
    heading_error(car, road, 1, &a, &c);
    Determine_AI(&car_blocks[car], a, c, l);
}

static void steer_towards(CarBlock *cb, int32_t target)
{
    int32_t s = cb->steering;
    if (s < target) {
        cb->steering = s + 0x20;
        if (target < s + 0x20)
            cb->steering = target;
    } else {
        cb->steering = s - 0x20;
        if (s - 0x20 < target)
            cb->steering = target;
    }
}

/* Reversing: speeds up backwards to the speed limit, slows to a crawl going forwards. */
static void reverse_throttle(CarBlock *cb)
{
    if (cb->forward_speed < -0x31) {
        if (cb->forward_speed < cb->ai_speed_limit)
            cb->throttle += 2000;
    } else {
        cb->throttle -= 100;
    }
}

/* One frame of the car's command script (`road` the direction of the road). */
static void run_script(int car, int32_t road, const Lanes *l)
{
    CarBlock *cb = &car_blocks[car];
    for (;;) {
        if (cb->barrier_hit != 0 && cb->ai_timer == 0) {
            cb->barrier_hit = 0;
            choose_script(car, road, l);
        }
        if (cb->ai_wait_state == AI_WAIT) {
            if (--cb->ai_wait_count == -1)
                cb->ai_wait_state = 0;
            return;
        }
        const int32_t *cmd = cb->ai_command;
        int32_t a, c;
        switch (cmd[0]) {
        case AI_BRAKE: {
            int32_t t = cb->forward_speed * -0x20;
            cb->throttle = t;
            if (t >= -9999 && t < 0)
                cb->throttle = -10000;
            else if (t > 0 && t <= 9999)
                cb->throttle = 10000;
            if (cb->speed > 0x22)
                return;
            cb->forward_speed = 0;
            cb->throttle = 0;
            cb->ai_command = cmd + 1;
            break;
        }
        case AI_CHOOSE:
            choose_script(car, road, l);
            return;
        case AI_THROTTLE:
            cb->throttle = cmd[1];
            cb->ai_command = cmd + 2;
            break;
        case AI_TURN_TO_ROAD: {
            if (cb->throttle < 5000)
                cb->throttle = 5000;
            if (cb->forward_speed < 0x32)
                cb->throttle += 100;
            else if (cb->ai_speed_limit < cb->forward_speed)
                cb->throttle = -5000;
            int32_t a2, c2;
            heading_error(car, road, 1, &a, &c);
            heading_error(car, road, 0, &a2, &c2);
            if (a2 > 0xff && c2 > 0xff) {
                if (c >= HALF_TURN)
                    Interpolate_Direction_Vectors_Left(cb, a, 0x30);
                else
                    Interpolate_Direction_Vectors_Right(cb, c, 0x30);
                return;
            }
            cb->ai_command = cmd + 1;
            break;
        }
        case AI_REVERSE_RIGHT:
            reverse_throttle(cb);
            heading_error(car, road, 0, &a, &c);
            if (a < 0x40 || c < 0x40) {
                cb->forward_speed = 0;
                cb->throttle = 10000;
                cb->ai_command = cmd + 1;
                break;
            }
            if (a > 0x100)
                a = 0x200;
            if (a < 0x20)
                a = 0x20;
            steer_towards(cb, a);
            return;
        case AI_REVERSE_LEFT:
            reverse_throttle(cb);
            heading_error(car, road, 0, &a, &c);
            if (a > 0x3f && c > 0x3f) {
                if (c > 0x100)
                    c = 0x200;
                if (c < 0x20)
                    c = 0x20;
                steer_towards(cb, -c);
                return;
            }
            cb->throttle = 10000;
            cb->ai_command = cmd + 1;
            break;
        case AI_WAIT:
            cb->ai_wait_state = AI_WAIT;
            cb->ai_wait_count = cmd[1];
            cb->ai_command = cmd + 2;
            break;
        case AI_LOCK_RIGHT:
        case AI_LOCK_LEFT: {
            int32_t lock = cmd[0] == AI_LOCK_RIGHT ? STEER_LOCK : -STEER_LOCK;
            if (cb->steering != lock) {
                cb->steering += lock / 0x10;
                if (lock > 0 ? cb->steering > lock : cb->steering < lock)
                    cb->steering = lock;
                return;
            }
            if (cb->forward_speed > 0x30)
                return;
            cb->throttle = 0;
            cb->ai_command = cmd + 1;
            break;
        }
        case AI_SPEED_LIMIT:
            cb->ai_speed_limit = cmd[1];
            cb->ai_command = cmd + 2;
            break;
        case AI_END:
            cb->ai_commands = NULL;
            cb->ai_command = NULL;
            cb->ai_aim_timer = AIM_FRAMES;
            return;
        default:
            return;
        }
    }
}

/* The other car ahead in the same lane and route (within 0x18 strips), if it is in the way: the
 * heading to pass it, else 0. Also sets the dodge and braking reactions of the car's personality. */
static int32_t Obstacle_Ahead(int car, const Lanes *l)
{
    CarBlock *cb = &car_blocks[car];
    int32_t mine = car_fd[car].strip_number;
    int32_t reach = mine + 0x18, wrap = 0, nearest = num_strips - 1;
    int ahead = -1;
    if (num_strips < reach)
        wrap = reach - num_strips;
    for (int k = 0; k < 20; k++) {
        int32_t at = car_fd[k].strip_number;
        if (k != car && cb->ai_route == car_blocks[k].ai_route && l->lane == car_blocks[k].lane &&
            (at < wrap || (at < reach && mine <= at)) && Strip_Distance(mine, at) < nearest) {
            ahead = k;
            nearest = Strip_Distance(mine, at);
        }
    }
    if (ahead < 0)
        return 0;
    const CarBlock *ob = &car_blocks[ahead];
    if (cb->speed + 0x78 < ob->speed && ob->ai_off_line == 0)
        return 0;
    if (cb->ai_personality & PERSONALITY_IGNORE_CROSSING) {
        if (ob->ai_heading_diff > 0x200 && ob->ai_heading_diff < 0x600)
            return 0;
        if (ob->ai_heading_diff > 0xa00 && ob->ai_heading_diff < 0xe18 && nearest > 4)
            return 0;
    }
    int32_t closing = cb->speed - ob->speed;
    if (nearest == 8 && closing > 399 && cb->ai_swerving == 0 && (cb->ai_personality & PERSONALITY_DODGE)) {
        start_script(cb, l->mid < cb->lane ? cmd_dodge_right : cmd_dodge_left);
        return 0;
    }
    if (nearest < 8 && closing > 0x167 && (cb->ai_personality & PERSONALITY_BRAKE))
        cb->throttle = -0x1000;
    cb->ai_swerving = 1;
    if (cb->ai_personality & PERSONALITY_COOLDOWN)
        cb->ai_swerve_cooldown = SWERVE_COOLDOWN;
    int32_t mine_dir = road_angle(lane_quad(strip_at(car_fd[car].strip), ai_segment)->heading);
    if (closing < 0x40)
        closing = 0x40;
    else if (closing > 0x200)
        closing = 0x200;
    int left_taken = 0, right_taken = 0;
    for (int k = 0; k < 20; k++) {
        if (car_fd[k].strip_number != car_fd[ahead].strip_number)
            continue;
        if (car_blocks[k].lane == l->lane + 1)
            left_taken = 1;
        else if (car_blocks[k].lane == l->lane - 1)
            right_taken = 1;
    }
    int32_t its_dir = lane_quad(strip_at(car_fd[ahead].strip), ai_segment)->heading * 0x10;
    int32_t aim = its_dir + closing;
    if (l->lane != l->last &&
        (l->lane == l->first || (l->lane <= l->mid && !left_taken) ||
         ((l->lane <= l->first || right_taken) && l->lane < l->mid)))
        aim = its_dir - closing;
    if (nearest < 1)
        return 0;
    return mine_dir + (((aim + QUARTER_TURN) & ANGLE_MASK) - mine_dir) / nearest;
}

static void restart(CarBlock *cb)
{
    cb->ai_timer = 0;
    start_script(cb, ai_unstick_script);
    cb->ai_swerving = 0;
}

/* Driving on a track: throttle from the strip's speed and the corners, steering by the road's heading
 * plus a lane bias (keep off the edges, swerve round cars, crowd the player). Returns 1/2 to put the
 * wheel on full left/right lock for a corner taken too fast. */
static int drive(int car, const Strip *s, int seg, const Lanes *l)
{
    CarBlock *cb = &car_blocks[car];
    int32_t bias = 0, crowd = 0;
    int lock = 0;
    if (cb->forward_speed < 10 && cb->barrier_hit != 0)
        start_script(cb, ai_unstick_script);
    int flags = s->ai_flags;
    if (flags & STRIP_AI_RESET)
        cb->ai_speed_offset = 0;
    if (flags & STRIP_AI_CORNER) {
        int k = flags >> 4;
        cb->ai_corner_speed = corner_speeds[k];
        cb->ai_corner_index = k;
        cb->ai_corner_type = corner_types[k];
    }
    if (corner_limits[cb->ai_corner_index] < cb->forward_speed) {
        lock = cb->ai_corner_type == 1;
        if (cb->ai_corner_type == 2)
            lock = 2;
    }
    if (((flags & STRIP_AI_BRAKE_LEFT) || (flags & STRIP_AI_BRAKE_RIGHT)) && cb->forward_speed > cb->ai_corner_speed) {
        int clear = flags & STRIP_AI_BRAKE_LEFT ? l->lane <= flags >> 4 : flags >> 4 <= l->lane;
        if (clear)
            cb->ai_speed_offset = 0;
        else
            cb->ai_speed_offset -= 0x100;
    }
    cb->throttle = Recommended_Acceleration(s->ai_speed, cb->speed, cb->ai_handicap + cb->ai_speed_offset);
    int32_t pers = cb->ai_personality;
    if (pers & (PERSONALITY_BLOCK | PERSONALITY_BLOCK_HARD)) {
        int32_t range = pers & PERSONALITY_BLOCK_CLOSE ? 3 : 8;
        int32_t d = Strip_Distance(car_fd[0].strip_number, car_fd[car].strip_number);
        if (d <= range && car_blocks[0].forward_speed - cb->forward_speed > -0x50 &&
            (ai_target_car == 0 || d <= Strip_Distance(car_fd[0].strip_number, car_fd[ai_target_car].strip_number))) {
            if (cb->lane < car_blocks[0].lane && cb->ai_corner_type != 2 && quad_surface(lane_quad(s, seg + 1)) == SURFACE_ROAD) {
                crowd = d - range;
                ai_target_car = car;
            } else if (car_blocks[0].lane < cb->lane && cb->ai_corner_type != 1 &&
                       quad_surface(lane_quad(s, seg - 1)) == SURFACE_ROAD) {
                crowd = range - d;
                ai_target_car = car;
            }
            if (pers & PERSONALITY_BLOCK_HARD)
                crowd *= 2;
        }
    }
    int32_t a, c, aim;
    heading_error(car, road_angle(lane_quad(s, seg)->heading), 0, &a, &c);
    if ((s->ai_lane & STRIP_LANE_NO_SWERVE) == 0 && (a < TURN / 8 || c < TURN / 8) && (aim = Obstacle_Ahead(car, l)) != 0 &&
        ai_target_car == 0) {
        a = ((car_heading(car) + cb->steering) & ANGLE_MASK) - aim;
    } else {
        cb->ai_swerving = 0;
        if (l->lane == l->last && cb->barrier_hit != 0)
            bias = cb->ai_corner_type != 0 ? 0x14 : 8;
        if (l->lane == l->first && cb->barrier_hit != 0)
            bias = cb->ai_corner_type != 0 ? -0x14 : -8;
        if (quad_surface(lane_quad(s, seg)) != SURFACE_ROAD)
            bias = l->mid <= l->lane ? 0xc : -0xc;
        if (cb->ai_swerve_cooldown != 0)
            cb->ai_swerve_cooldown--;
        int t = s->ai_lane;
        if ((t & STRIP_LANE_KEEP_LEFT) && cb->ai_swerve_cooldown == 0 && l->lane < l->first + (t >> 4)) {
            int32_t v = 0x10 - cb->forward_speed / 32;
            bias = v < 4 ? -4 : -v;
        }
        if ((t & STRIP_LANE_KEEP_RIGHT) && cb->ai_swerve_cooldown == 0 && l->first + (t >> 4) < l->lane) {
            int32_t v = 0x10 - cb->forward_speed / 32;
            bias = v < 4 ? 4 : v;
        }
        if ((pers & PERSONALITY_WEAVE) && (cb->position[0] & 8))
            bias += frame_count & 0x20 ? -(frame_count & 0xf) : frame_count & 0xf;
        int heading;
        if (cb->ai_aim_timer != 0) {
            cb->ai_aim_timer--;
            heading = s->heading;
        } else if ((current_level == LEVEL_BLACK_SAIL_VALLEY && cb->ai_prev_route == 4) || (current_level == LEVEL_ULTIMATE_DESTRUCTION && cb->ai_prev_route == 3)) {
            heading = s->heading;
        } else {
            heading = lane_quad(s, seg)->heading;
        }
        bias += crowd;
        a = ((car_heading(car) + cb->steering) & ANGLE_MASK) - road_angle(bias + heading);
    }
    Get_Direction_Cosines(&a, &c);
    if (a <= HALF_TURN) {
        int32_t step = 0;
        if (a > QUARTER_TURN) {
            step = 0x40;
            restart(cb);
        }
        Interpolate_Direction_Vectors_Left(cb, a, step);
    } else if (a < HALF_TURN + QUARTER_TURN) {
        restart(cb);
        Interpolate_Direction_Vectors_Right(cb, c, 0x40);
    } else {
        Interpolate_Direction_Vectors_Right(cb, c, 0);
    }
    return lock;
}

/* Bowls, with a damaged front: reverse into the target. */
static void ram_backwards(int car, int target)
{
    CarBlock *cb = &car_blocks[car];
    const CarBlock *tb = &car_blocks[target];
    int32_t bearing = Calc_Angle((tb->position[0] - cb->position[0]) >> 12, (tb->position[2] - cb->position[2]) >> 12);
    int32_t a = (car_heading(car) & ANGLE_MASK) - TURN + bearing, c;
    Get_Direction_Cosines(&a, &c);
    if (a < c) {
        int32_t v = HALF_TURN - c;
        cb->steering = v > STEER_LOCK ? STEER_LOCK : v;
    } else {
        int32_t v = HALF_TURN - a;
        cb->steering = v <= STEER_LOCK ? -v : -STEER_LOCK;
    }
    int32_t lock = abs(cb->steering);
    if (lock <= STEER_LOCK)
        cb->throttle = rsin(lock * 2) * 7 - 0x8000;
    if (cb->forward_speed >= -99)
        cb->throttle -= 0x400;
    else if (lock == STEER_LOCK && cb->forward_speed <= -0x105)
        cb->throttle = -0x1000;
    if (cb->throttle < -0x8000)
        cb->throttle = -0x8000;
}

/* Bowls: drive at where the target will be. */
static void ram(int car, int target)
{
    CarBlock *cb = &car_blocks[car];
    const CarBlock *tb = &car_blocks[target];
    int32_t dx = (tb->position[0] - cb->position[0]) >> 13;
    int32_t dz = (tb->position[2] - cb->position[2]) >> 13;
    int32_t dist = SquareRoot0(dx * dx + dz * dz);
    int32_t lead = cb->forward_speed == 0 ? 0 : dist / cb->forward_speed;
    int32_t bearing = Calc_Angle((lead * tb->velocity[0] + tb->position[0] - cb->position[0]) >> 12,
                                 (lead * tb->velocity[2] + tb->position[2] - cb->position[2]) >> 12);
    int32_t a = ((car_heading(car) + cb->steering) & ANGLE_MASK) - TURN + bearing, c, off;
    Get_Direction_Cosines(&a, &c);
    if (a < c) {
        Interpolate_Direction_Vectors_Left(cb, a, 0x40);
        if (a <= QUARTER_TURN)
            cb->throttle = rsin(a) * -7 + 0x8000;
        off = a;
    } else {
        Interpolate_Direction_Vectors_Right(cb, c, 0x40);
        if (c <= QUARTER_TURN)
            cb->throttle = rsin(c) * -7 + 0x8000;
        off = c;
    }
    if (cb->forward_speed < 100)
        cb->throttle += 0x400;
    else if (off > TURN / 8 && cb->forward_speed >= 0x105)
        cb->throttle = 0x1000;
    if (cb->throttle > 0x8000)
        cb->throttle = 0x8000;
}

/* Whether car `n` can be a target. PORT: the original's pick can be 20, one past the cars, where it
 * reads unrelated memory; such a pick is refused here. */
static int bowl_target_ok(int n)
{
    return n < MAX_CARS && car_blocks[n].motion_state == MOTION_DRIVING && car_blocks[n].wrecked == 0;
}

static int bowl_pick(int32_t a, int32_t b)
{
    int n = (a & 0xf) + ((b & 0xf) - 10);
    return n < 1 ? 1 : n;
}

/* The bowls: back off after hitting the wall, ram the target, and every 0x7D frames a new target
 * (in total destruction always the player). */
static void bowl_ai(int car)
{
    CarBlock *cb = &car_blocks[car];
    if (cb->barrier_hit != 0) {
        if ((cb->forward_speed > 0 && cb->heavy_damage == 0) || (cb->forward_speed < 0 && cb->heavy_damage != 0)) {
            cb->ai_bowl_target = BOWL_BACK_OFF;
            cb->ai_timer = 0x28;
        } else {
            cb->ai_timer = 0;
        }
        return;
    }
    if (cb->ai_timer != 0) {
        cb->ai_timer--;
        if (cb->ai_bowl_target == BOWL_BACK_OFF) {
            cb->throttle = cb->heavy_damage != 0 ? 0x4000 : -0x4000;
            cb->steering = 0;
            return;
        }
        int target = cb->ai_bowl_target - 1;
        if (pointy_bits[target].cause == car && pointy_bits[target].timer != 0) {
            /* it is still spinning from our hit: leave it */
            cb->ai_bowl_target = BOWL_BACK_OFF;
            cb->ai_timer = 0x28;
            return;
        }
        if (cb->heavy_damage != 0)
            ram_backwards(car, target);
        else
            ram(car, target);
        return;
    }
    if (race_type != RACE_TOTAL_DESTRUCTION) {
        int n = bowl_pick(car_blocks[0].position[0], car_blocks[0].position[2]);
        if (!bowl_target_ok(n)) {
            n = bowl_pick(car_blocks[1].position[0], car_blocks[2].position[0]);
            /* PORT: with the list used up the original reads the byte before it; the player instead */
            if (!bowl_target_ok(n))
                n = bowl_cars_end > bowl_cars ? *--bowl_cars_end : 1;
        }
        cb->ai_bowl_target = n - 1 == car ? 1 : n;
    } else {
        cb->ai_bowl_target = 1;
    }
    cb->ai_timer = 0x7d;
    cb->throttle = 0x1000;
}

/* One frame of a computer driver: its steering and throttle. */
void AI_Com_Server(int car)
{
    car &= 0xff;
    CarBlock *cb = &car_blocks[car];
    if (cb->wrecked != 0 || cb->motion_state == MOTION_STUCK) {
        cb->throttle = 0;
        cb->steering = 0;
        return;
    }
    if (current_level >= FIRST_BOWL_LEVEL) {
        bowl_ai(car);
        return;
    }
    int seg = car_fd[car].segment;
    const Strip *s = strip_at(car_fd[car].strip);
    Lanes l;
    l.first = s->lane_base;
    l.width = s->width;
    l.last = (uint8_t)(l.first + l.width - 1);
    l.mid = (uint8_t)(l.last - l.width / 2);
    l.lane = (uint8_t)cb->lane;
    ai_segment = seg;
    int lock = 0;
    if (cb->ai_command != NULL) {
        if (cb->ai_timer != 0)
            cb->ai_timer--;
        int heading = quad_surface(lane_quad(s, seg)) == SURFACE_ROAD ? lane_quad(s, seg)->heading : s->heading;
        run_script(car, road_angle(heading), &l);
    } else {
        lock = drive(car, s, seg, &l);
    }
    if (lock == 1)
        cb->steering = -STEER_LOCK;
    else if (lock == 2)
        cb->steering = STEER_LOCK;
}

void InitialiseAI(int car)
{
    car &= 0xff;
    CarBlock *cb = &car_blocks[car];
    cb->ai_handicap = handicap_low[car] + (handicap_high[car] - handicap_low[car]) * (division + 1) / 4;
    if (division >= 0 && division <= 3)
        cb->ai_personality = personalities[race_mode != MODE_WRECKING_RACING ? 4 : division][car];
    ai_target_car = 0;
    cb->ai_route = 0;
    cb->ai_prev_route = 0;
    cb->grip = ai_grip[car];
    if (current_level < FIRST_BOWL_LEVEL) {
        cb->ai_timer = 0;
    } else {
        cb->ai_bowl_target = race_type == RACE_TOTAL_DESTRUCTION ? 1 : (car + num_cars / 2) % num_cars + 1;
        cb->ai_timer = 0x7d;
    }
    cb->ai_corner_index = 0;
    cb->ai_corner_type = 0;
    cb->ai_speed_offset = 0;
    cb->ai_aim_timer = 0;
    cb->ai_wait_state = 0;
    cb->ai_wait_count = 0;
    cb->barrier_hit = 0;
    start_script(cb, ai_unstick_script);
    cb->ai_swerving = 0;
    cb->ai_swerve_cooldown = 0;
    pointy_bits[car].timer = 0;
    pointy_bits[car].cause = 0;
    race_records[car].points = 0;
    for (int k = 0; k < 11; k++)
        cb->damage[k] = 0;
    if (current_level >= LEVEL_PINE_HILLS && current_level <= LEVEL_MOTORPLEX) {
        corner_speeds = &corner_data[corner_start[current_level]];
        corner_limits = corner_speeds + CORNER_LIMITS;
        corner_types = corner_speeds + CORNER_TYPES;
    }
}

/* Each frame before the drivers: every car's lane, how far off the road's heading it points, and the
 * route it is on; in the bowls, the list of cars still driving. */
void AI_Update_Strip_Info(void)
{
    if (current_level >= FIRST_BOWL_LEVEL) {
        bowl_cars_end = bowl_cars;
        for (int car = 0; car < num_cars; car++)
            if (car_blocks[car].motion_state == MOTION_DRIVING && car_blocks[car].wrecked == 0)
                *bowl_cars_end++ = (uint8_t)(car + 1);
        return;
    }
    ai_target_car = 0;
    for (int car = 0; car < 20; car++) {
        CarBlock *cb = &car_blocks[car];
        const Strip *s = strip_at(car_fd[car].strip);
        cb->lane = s->lane_base + car_fd[car].segment;
        int32_t diff = (car_heading(car) & ANGLE_MASK) - road_angle(s->heading), c;
        Get_Direction_Cosines(&diff, &c);
        cb->ai_heading_diff = diff;
        cb->ai_off_line = diff > QUARTER_TURN && c > QUARTER_TURN;
        if (s->route & 1) {
            uint8_t route = (uint8_t)((int8_t)s->route >> 1);
            if (route != cb->ai_route)
                cb->ai_prev_route = cb->ai_route;
            cb->ai_route = route;
        }
    }
}
