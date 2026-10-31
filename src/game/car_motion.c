#include "game/car_motion.h"

#include "game/ai.h"
#include "game/car.h"
#include "game/car_cluts.h"
#include "game/car_draw.h"
#include "game/collision.h"
#include "game/debris.h"
#include "game/frontend.h"
#include "game/league.h"
#include "game/main_init.h"
#include "game/particles.h"
#include "game/play_game.h"
#include "game/replay.h"
#include "game/sound_fx.h"
#include "game/stats.h"
#include "game/triggers.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"
#include "psx/libc.h"
#include "psx/libgte.h"
#include "system/maths.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BOWL_GRID_RADIUS 12000
#define NOT_FINISHED 0x15
#define POSITIONS_EVERY 25        /* frames between Get_Race_Positions rankings */
#define LAP_TIME_HOLD 0x40
#define RECORD_FLASH 0x80
#define START_TIMER 300           /* fire_timer of a car at the start (cars 0 and 1 always) */

CarBlock car_blocks[MAX_CARS];
CarState car_states[MAX_CARS];

RaceRecord race_records[RACE_RECORDS];
TrackInfo track_info[12] = {
    {0, 0, 0}, {267, 280, 10}, {560, 604, 5}, {340, 393, 5}, {319, 383, 5}, {23, 234, 8},
    {243, 999, 7}, {449, 999, 5}, {0, 999, 999}, {0, 999, 999}, {0, 999, 999}, {0, 999, 999},
};
TrackFd wheel_fd[RACE_RECORDS][CAR_WHEELS];
int32_t stuck_timer[RACE_RECORDS];
int32_t race_minutes, race_seconds, race_fraction;
int32_t lap_minutes, lap_seconds, lap_fraction;
int32_t timing_lap;
int32_t lap_record_flash;
int32_t lap_time_hold;
int32_t active_cars;

static int32_t positions_countdown = 1; /* 0x80079690 */

/* The grid of the Motorplex: strip number and quad per grid slot. */
static const struct {
    int16_t strip;
    uint8_t segment;
} motorplex_grid[20] = { /* 0x80070CE0 */
    {449, 1}, {448, 2}, {447, 1}, {446, 2}, {445, 1}, {444, 3}, {443, 2}, {442, 3}, {441, 3}, {440, 4},
    {439, 3}, {438, 4}, {437, 4}, {436, 5}, {435, 4}, {434, 5}, {573, 1}, {572, 2}, {571, 1}, {570, 2},
};

/* Bowls: the cars in a circle facing the centre (the stadium: half a circle). */
static void bowl_grid(int span, int angle_offset)
{
    for (int i = 0; i < num_cars; i++) {
        CarState *cs = &car_states[i];
        int a = (i * span) / num_cars;
        cs->position[1] = 0;
        cs->position[0] = rsin(a + angle_offset) * BOWL_GRID_RADIUS / 4096;
        cs->position[2] = rcos(a + angle_offset) * -BOWL_GRID_RADIUS / 4096;
        cs->angle[1] = (int16_t)((-span * i) / num_cars - angle_offset);
        TrackFd *fd = &car_fd[i];
        fd->x = cs->position[0];
        fd->y = cs->position[1];
        fd->z = cs->position[2];
        fd->w = cs->position[3];
        Map_Height(fd);
    }
}

/* Tracks: two cars per strip, alternating between two quads, from the strip of the level's grid
 * backwards in grid order. */
static void track_grid(void)
{
    int strip = 0, first = 0, step = 0;
    switch (current_level) {
    case LEVEL_PINE_HILLS:
        strip = 266, first = 2, step = 1;
        break;
    case LEVEL_CHALK_CANYON:
        strip = 656, first = 1, step = 1;
        break;
    case LEVEL_BLACK_SAIL_VALLEY:
        strip = 535, first = 3, step = -1;
        break;
    case LEVEL_ULTIMATE_DESTRUCTION:
        strip = 614, first = 2, step = -1;
        break;
    case LEVEL_CAPRIO_COUNTY:
        strip = 22, first = 2, step = 1;
        break;
    case LEVEL_LIBERTY_CITY:
        strip = 242, first = 2, step = 1;
        break;
    default:
        break;
    }
    TrackFd fd;
    memset(&fd, 0, sizeof(fd));
    fd.strip_number = (int16_t)strip;
    fd.segment = (int8_t)first;
    Search_For_Strip(&fd);
    for (int i = 0; i < num_cars; i++) {
        int car = car_order[i];
        CarState *cs = &car_states[car];
        cs->angle[0] = 0;
        cs->angle[2] = 0;
        if (current_level == LEVEL_MOTORPLEX) {
            fd.strip_number = motorplex_grid[i].strip;
            fd.segment = (int8_t)motorplex_grid[i].segment;
            Search_For_Strip(&fd);
        } else {
            fd.segment = (int8_t)(first + step * (i & 1));
        }
        Track_Quad_Centre(&fd, cs->position, cs->angle);
        /* PORT: the original never sets the record's point, so the first Track_Follow below runs on
         * whatever was on the stack; the port uses the car's place, which the record follows from. */
        fd.x = cs->position[0];
        fd.y = cs->position[1];
        fd.z = cs->position[2];
        Move_Backward_Strip(&fd, 1);
        car_fd[car] = fd;
        Track_Follow(&car_fd[car]);
        Map_Height(&car_fd[car]);
    }
}

/* Every car at its grid place, at rest and undamaged, with its AI set up. */
void Place_Cars_On_Grid(void)
{
    if (current_level == LEVEL_DEATH_BOWL)
        bowl_grid(HALF_TURN, -QUARTER_TURN);
    else if (current_level >= FIRST_BOWL_LEVEL)
        bowl_grid(TURN, 0);
    else
        track_grid();

    const TrackInfo *info = &track_info[current_level];
    for (int i = 0; i < num_cars; i++) {
        CarBlock *cb = &car_blocks[i];
        const CarState *cs = &car_states[i];
        for (int k = 0; k < 3; k++) {
            cb->angle[k] = cs->angle[k] << 12;
            cb->position[k] = cs->position[k] << 12;
        }
        cb->motion_state = MOTION_DRIVING;
        memset(cb->velocity, 0, sizeof(int32_t) * 3);
        Get_Wheel_Positions(i);
        cb->fly_corner_bias = 0;
        memset(cb->angular_velocity, 0, sizeof(int32_t) * 3);
        memset(cb->velocity, 0, sizeof(int32_t) * 3);
        memset(cb->velocity_change, 0, sizeof(int32_t) * 3);
        cb->speed = 0;
        cb->forward_speed = 0;
        cb->steering = 0;
        cb->throttle = 0;
        cb->motion_state = MOTION_DRIVING;
        cb->roll_side = 0;
        cb->ground_mode = 0;
        for (int w = 0; w < CAR_WHEELS; w++) {
            cb->wheel_rise[w] = 0;
            cb->wheel_ground[w] = wheel_fd[i][w].y;
            cb->suspension[w] = 0;
            cb->suspension_velocity[w] = 0;
            cb->wheel_mode[w] = 0;
        }
        cb->wrecked = 0;
        cb->heavy_damage = 0;
        memset(cb->wheel_lost, 0, sizeof(cb->wheel_lost));
        cb->wheels_lost = 0;
        cb->drive_factor = ONE;
        cb->ground_slope = ONE;
        cb->bonnet_lost = 0;
        cb->boot_lost = 0;
        cb->steer_bias = 0;
        cb->steer_factor = 2;
        cb->car_type = 2;
        cb->brightness = 0x40;
        cb->fire_timer = (rand() & 1) ? START_TIMER : 0;
        cb->part_loss_timer = 0;
        RaceRecord *rec = &race_records[i];
        rec->lap = 0;
        rec->lap_counted = 0;
        int16_t strip = (int16_t)((info->lap_strips + car_fd[i].track_position - info->start_strip) %
                                  info->lap_strips);
        rec->lap_strip = strip;
        rec->strip = strip;
        Car_Landed(i);
        stuck_timer[i] = 0;
    }
    for (int i = 0; i < race_cutoff; i++)
        car_blocks[i].car_type = race_car;
    car_blocks[0].fire_timer = START_TIMER;
    car_blocks[1].fire_timer = START_TIMER;
    race_fraction = 0;
    race_minutes = 0;
    race_seconds = 0;
    timing_lap = 1;
    lap_fraction = 0;
    lap_minutes = 0;
    lap_seconds = 0;
    lap_record_flash = 0;
    lap_time_hold = 0;
    for (int i = 0; i < num_cars; i++)
        InitialiseAI(i & 0xff);
    Init_Car_Graphics();
}

void Init_End_Race(void)
{
    for (int i = RACE_RECORDS - 1; i >= 0; i--)
        race_records[i].finished = 0;
    cars_finished = 0;
}

/* Is the time a new record for the level? Earlier minutes, or the same minutes and earlier seconds,
 * or the same seconds and no later fraction. */
static int beats_record(const TrackRecord *r)
{
    if (race_minutes != r->minutes)
        return race_minutes < r->minutes;
    if (race_seconds != r->seconds)
        return race_seconds < r->seconds;
    return race_fraction <= r->fraction;
}

/* The lap clock, and every car's laps: a car completes a lap when it reaches the line having been
 * on the last strip of the lap; going back over the line takes the lap away again. */
void Calc_Track_Positions(void)
{
    race_records[0].speed = (int16_t)(car_blocks[0].speed * 0x733 / 4096);
    if (overlay_state < 0) {
        if (lap_time_hold != 0)
            lap_time_hold--;
        race_fraction += CLOCK_TICK;
        if (race_fraction >= CLOCK_SECOND) {
            race_seconds++;
            race_fraction -= CLOCK_SECOND;
        }
        if (race_seconds > 59) {
            race_minutes++;
            race_seconds -= 60;
        }
        if (race_minutes > 98) {
            race_minutes = 99;
            race_seconds = 0;
            race_fraction = 0;
        }
        if (lap_time_hold == 0) {
            lap_fraction = race_fraction;
            lap_seconds = race_seconds;
            lap_minutes = race_minutes;
        }
    }
    const TrackInfo *info = &track_info[current_level];
    int n = info->lap_strips;
    int last = n - 1;
    for (int i = 0; i < RACE_RECORDS; i++) {
        RaceRecord *rec = &race_records[i];
        int strip = (n + car_fd[i].track_position - info->start_strip) % n;
        if (strip == 0) {
            if (rec->lap_counted == rec->lap) {
                if (rec->lap_strip == last) {
                    rec->lap_strip = 0;
                    rec->lap++;
                    rec->lap_counted = rec->lap;
                    if (i == 0 && timing_lap == rec->lap - 1) {
                        TrackRecord *r = &track_records[current_level - 1];
                        if (beats_record(r)) {
                            r->minutes = race_minutes;
                            r->seconds = race_seconds;
                            lap_record_flash = RECORD_FLASH;
                            r->fraction = race_fraction;
                        }
                        timing_lap = rec->lap;
                        lap_time_hold = LAP_TIME_HOLD;
                        race_fraction = 0;
                        race_seconds = 0;
                        race_minutes = 0;
                    }
                }
            } else if (rec->strip == last) {
                rec->lap_counted = rec->lap;
            }
        } else {
            if (strip == rec->lap_strip + 1)
                rec->lap_strip = (int16_t)strip;
            if (rec->strip == 0 && strip == last && rec->lap_counted == rec->lap && rec->lap_counted > 0)
                rec->lap_counted--;
        }
        rec->strip = (int16_t)strip;
    }
}

void Init_Race_Records(void)
{
    for (int i = 0; i < num_cars; i++) {
        memset(&race_records[i], 0, sizeof(RaceRecord));
        race_records[i].final_position = NOT_FINISHED;
    }
    positions_countdown = 1;
}

/* Every 25 frames: finishers get their place, then all cars are ranked by finishing place, lap and
 * strip; the race ends when the player is out, a bowl has one car left, or the player has finished
 * (practice keeps going). */
void Get_Race_Positions(void)
{
    if (positions_countdown != 0) {
        positions_countdown--;
        return;
    }
    positions_countdown = POSITIONS_EVERY;
    int laps = track_info[current_level].laps;
    for (int i = 0; i < num_cars; i++) {
        RaceRecord *rec = &race_records[i];
        if (rec->lap_counted == laps + 1 && rec->finished == 0) {
            rec->final_position = rec->position;
            rec->finished = 1;
        }
    }
    int32_t keys[MAX_CARS + 1];
    keys[0] = -1;
    for (int i = 0; i < num_cars; i++) {
        const RaceRecord *rec = &race_records[i];
        keys[i + 1] = (int32_t)((uint32_t)(NOT_FINISHED - rec->final_position) << 25 |
                                (uint32_t)rec->lap_counted << 20 | (uint32_t)rec->strip << 5 |
                                (uint32_t)(num_cars - i - 1));
    }
    for (int i = 2; i <= num_cars; i++) {
        int32_t key = keys[i];
        int j = i;
        while (key < keys[j - 1]) {
            keys[j] = keys[j - 1];
            j--;
        }
        keys[j] = key;
    }
    active_cars = num_cars;
    for (int i = 0; i < num_cars; i++) {
        int car = num_cars - (keys[i + 1] & 0x1f) - 1;
        race_records[car].position = (int16_t)(num_cars - i);
        /* the original tests the car blocks by rank, not by car */
        if ((car_blocks[i].wrecked == 1 || car_blocks[i].motion_state == MOTION_STUCK) && race_cutoff <= i)
            active_cars--;
    }
    if (car_blocks[0].wrecked == 1 || (current_level >= FIRST_BOWL_LEVEL && active_cars < 2))
        cars_finished++;
    if (race_type != RACE_TIME_TRIAL && race_records[0].finished == 1)
        cars_finished++;
}

/* Numbers the strips along the lap. Where the track forks the two branches share numbers, spread
 * over the longer one in proportion. */
void Init_Track_Strip_Numbers(void)
{
    if (current_level >= FIRST_BOWL_LEVEL)
        return;
    int16_t number = 0;
    int32_t offset = 0;
    int32_t next;
    do {
        Strip *s = strip_at(offset);
        s->track_position = number;
        next = s->next;
        number++;
        if (s->type == STRIP_FORK) {
            int main_len = 0;
            for (const Strip *p = strip_at(next); p->type != STRIP_JOIN; p = strip_at(p->next))
                main_len++;
            int branch_len = 0;
            for (const Strip *p = strip_at(s->branch); p->type != STRIP_JOIN; p = strip_at(p->next))
                branch_len++;
            int32_t branch = s->branch;
            int32_t acc = 0x8000;
            int done = 0;
            if (main_len < branch_len) {
                int32_t step = (branch_len * 0x10000 + 0x8000) / main_len;
                for (int k = 0; k < main_len; k++) {
                    acc += step;
                    for (int j = (acc >> 16) - done; j > 0; j--) {
                        Strip *b = strip_at(branch);
                        branch = b->next;
                        b->track_position = number;
                    }
                    Strip *m = strip_at(next);
                    m->track_position = number;
                    next = m->next;
                    number++;
                    done = acc >> 16;
                }
            } else {
                int32_t step = (main_len * 0x10000 + 0x8000) / branch_len;
                for (int k = 0; k < branch_len; k++) {
                    acc += step;
                    for (int j = (acc >> 16) - done; j > 0; j--) {
                        Strip *m = strip_at(next);
                        next = m->next;
                        m->track_position = number;
                    }
                    Strip *b = strip_at(branch);
                    b->track_position = number;
                    branch = b->next;
                    number++;
                    done = acc >> 16;
                }
            }
        }
        offset = next;
    } while (strip_at(next)->number >> 6 != 0);
    track_info[current_level].lap_strips = number;
}

/* Shadows: two quads per car and buffer, the top and bottom halves of the SHADOW sprite. */
static POLY_FT4 shadow_packets[2][MAX_CARS][2]; /* 0x80087830 */
/* Grey, subtractive (the exe holds it initialised; zeroed it would draw an opaque dark quad). */
static Sprite shadow_sprite = {0, 0, 0x40, 0x40, 0x40, 0, 1, 2, 0}; /* 0x80070DCC */
#define SHADOW_OT 0x40                           /* sorted this far behind the car */

void Init_Car_Shadows(void)
{
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < num_cars; i++) {
            POLY_FT4 *p = shadow_packets[b][i];
            Setup_Sprite_Quad(&p[0], "SHADOW", &shadow_sprite);
            Setup_Sprite_Quad(&p[1], "SHADOW", &shadow_sprite);
            Modify_Sprite(&p[0], &shadow_sprite, 0, 0, 0x3f, 0x20);
            Modify_Sprite(&p[1], &shadow_sprite, 0, 0x20, 0x3f, 0x1f);
        }
    }
}

static void shadow_quad(POLY_FT4 *p, const SVECTOR *a, const SVECTOR *b, const SVECTOR *c, const SVECTOR *d)
{
    gte_ldv3(a, b, c);
    gte_rtpt();
    gte_nclip();
    int32_t flag;
    gte_stflg(&flag);
    if ((flag & GTE_CLIP_FLAGS) != 0)
        return;
    gte_stsxy3_xy(p);
    gte_ldv0(d);
    gte_rtps();
    p->x3 = gte.sx[2];
    p->y3 = gte.sy[2];
    gte_avsz4();
    int32_t otz;
    gte_stotz(&otz);
    addPrim(&cdb->ot[otz + SHADOW_OT], p);
}

/* The shadow lies on the four wheels' ground points, a little bigger than they span, shaded by the
 * car's brightness. */
void Draw_Car_Shadow(int car)
{
    static const MATRIX identity = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}}; /* 0x80010828 */
    uint8_t shade = (uint8_t)car_blocks[car].brightness;
    for (int b = 0; b < 2; b++) {
        for (int k = 0; k < 2; k++) {
            shadow_packets[b][car][k].r0 = shade;
            shadow_packets[b][car][k].g0 = shade;
            shadow_packets[b][car][k].b0 = shade;
        }
    }
    const TrackFd *w = wheel_fd[car];
    int32_t centre[3] = {
        (w[0].x + w[1].x + w[2].x + w[3].x) / 4,
        (w[0].y + w[1].y + w[2].y + w[3].y) / 4,
        (w[0].z + w[1].z + w[2].z + w[3].z) / 4,
    };
    SVECTOR c[4];
    for (int i = 0; i < 4; i++) {
        c[i].vx = (int16_t)((w[i].x - centre[0]) * 9 / 8);
        c[i].vy = (int16_t)((w[i].y - centre[1]) * 9 / 8);
        c[i].vz = (int16_t)((w[i].z - centre[2]) * 9 / 8);
        c[i].pad = 0;
    }
    SVECTOR front = {(int16_t)((c[0].vx + c[1].vx) / 2), (int16_t)((c[0].vy + c[1].vy) / 2),
                     (int16_t)((c[0].vz + c[1].vz) / 2), 0};
    SVECTOR back = {(int16_t)((c[2].vx + c[3].vx) / 2), (int16_t)((c[2].vy + c[3].vy) / 2),
                    (int16_t)((c[2].vz + c[3].vz) / 2), 0};
    Set_Object_View(&identity, centre);
    shadow_quad(&shadow_packets[buffer_num][car][0], &c[0], &c[2], &front, &back);
    shadow_quad(&shadow_packets[buffer_num][car][1], &front, &back, &c[1], &c[3]);
}

/* ---- car motion: where the wheels and corners are, the car's attitude, suspension, driving */

TrackFd corner_fd[8];                  /* 0x800A6808: the car being moved, its eight corners */
int32_t corner_pos[8][4];              /* 0x8008485C: the corners themselves (corner_fd has the ground) */
static SVECTOR wheel_axis[MAX_CARS][CAR_WHEELS]; /* 0x800845DC: suspension directions in the world */

/* Car space points: the eight corners of the body box (four low, four high), the wheels, and the
 * suspension directions. */
static const SVECTOR corner_offsets[8] = { /* 0x800709D8 */
    {186, -130, 450, 0}, {186, -130, -450, 0}, {-186, -130, 450, 0}, {-186, -130, -450, 0},
    {186, 130, 450, 0}, {186, 130, -450, 0}, {-186, 130, 450, 0}, {-186, 130, -450, 0},
};
static const SVECTOR wheel_offsets[4] = { /* 0x80070C90 */
    {186, -130, 450, 0}, {186, -130, -450, 0}, {-186, -130, 450, 0}, {-186, -130, -450, 0},
};
static const SVECTOR axis_vectors[4] = { /* 0x80070CB0 */
    {152, -60, 291, 0}, {152, -60, -291, 0}, {-152, -60, 232, 0}, {-152, -60, -232, 0},
};

/* Per surface (flag bits 1-2): grip, and from the third entry on, drive. */
static const int32_t surface_factors[6] = {4096, 1024, 4096, 2048, 65985, 131520}; /* 0x80070CD0 */

/* The near model's vertices around each wheel arch, hidden when the wheel is gone or lowered with
 * the suspension. */
static const int32_t hide_vertices[4][4] = { /* 0x80010858 */
    {99, 94, 97, 88}, {71, 90, 69, 93}, {96, 89, 98, 95}, {68, 92, 79, 91},
};
static const int32_t lower_vertices[4][4] = { /* 0x80010898 */
    {99, 94, 97, 88}, {71, 90, 69, 93}, {96, 89, 98, 95}, {68, 92, 70, 91},
};

#define SUSPENSION_LIMIT 0x1f000
#define WHEEL_REST_Y (-0x3c)

/* A car space point into the world, with the car's transform loaded. */
static void place_fd(TrackFd *fd, const SVECTOR *offset)
{
    VECTOR p;
    gte_ldv0(offset);
    gte_rt();
    gte_stlvnl(&p);
    fd->x = p.vx;
    fd->y = p.vy;
    fd->z = p.vz;
}

static void load_car_transform(int car)
{
    const CarState *cs = &car_states[car];
    gte_SetRotMatrix(&cs->matrix);
    gte_ldtr(cs->position[0], cs->position[1], cs->position[2]);
}

/* The four wheels' track records (from the car's strip), and the suspension directions. */
void Get_Wheel_Positions(int car)
{
    TrackFd *w = wheel_fd[car];
    for (int k = 0; k < 4; k++) {
        w[k].strip = car_fd[car].strip;
        w[k].segment = car_fd[car].segment;
    }
    load_car_transform(car);
    for (int k = 0; k < 4; k++) {
        place_fd(&w[k], &wheel_offsets[k]);
        Track_Follow(&w[k]);
        Map_Height(&w[k]);
    }
    for (int k = 0; k < 4; k++) {
        gte_ldv0(&axis_vectors[k]);
        gte_rtv0();
        wheel_axis[car][k].vx = (int16_t)gte.ir[1];
        wheel_axis[car][k].vy = (int16_t)gte.ir[2];
        wheel_axis[car][k].vz = (int16_t)gte.ir[3];
    }
}

/* All eight body corners, and the ground under each; the low four become the wheels' records. */
void Get_Corner_Positions(int car)
{
    for (int k = 0; k < 8; k++) {
        corner_fd[k].strip = car_fd[car].strip;
        corner_fd[k].segment = car_fd[car].segment;
    }
    load_car_transform(car);
    for (int k = 0; k < 8; k++) {
        place_fd(&corner_fd[k], &corner_offsets[k]);
        corner_pos[k][0] = corner_fd[k].x;
        corner_pos[k][1] = corner_fd[k].y;
        corner_pos[k][2] = corner_fd[k].z;
        corner_pos[k][3] = corner_fd[k].w;
        Track_Follow(&corner_fd[k]);
        Map_Height(&corner_fd[k]);
    }
    for (int k = 0; k < 4; k++) {
        wheel_fd[car][k].x = corner_fd[k].x;
        wheel_fd[car][k].y = corner_fd[k].y;
        wheel_fd[car][k].z = corner_fd[k].z;
        wheel_fd[car][k].w = corner_fd[k].w;
    }
}

/* Pitch and roll from where the wheels touch the ground, plus the suspension. */
void Calc_Car_Angles_From_Wheels(int car)
{
    const TrackFd *w = wheel_fd[car];
    const int32_t *s = car_blocks[car].suspension;
    int32_t dx = (w[0].x + w[2].x) - (w[1].x + w[3].x);
    int32_t dz = (w[0].z + w[2].z) - (w[1].z + w[3].z);
    int32_t dy = (w[0].y + w[2].y) - (w[1].y + w[3].y) + ((s[0] + s[2]) - (s[1] + s[3])) / 4096;
    car_states[car].angle[0] = (int16_t)Calc_Angle(-dy, SquareRoot0(dx * dx + dz * dz));
    dx = (w[0].x + w[1].x) - (w[2].x + w[3].x);
    dz = (w[0].z + w[1].z) - (w[2].z + w[3].z);
    dy = (w[0].y + w[1].y) - (w[2].y + w[3].y) + ((s[0] + s[1]) - (s[2] + s[3])) / 4096;
    car_states[car].angle[2] = (int16_t)Calc_Angle(dy, SquareRoot0(dx * dx + dz * dz));
}

/* The tilt of the low corners about one axis (0 or 1), as Calc_Car_Angles_From_Wheels. */
int Calc_Car_Tilt(int car, int axis)
{
    const TrackFd *c = corner_fd;
    const int32_t *s = car_blocks[car].suspension;
    int32_t dx, dy, dz;
    if (axis == 0) {
        dx = (c[0].x + c[1].x) - (c[2].x + c[3].x);
        dz = (c[0].z + c[1].z) - (c[2].z + c[3].z);
        dy = (c[0].y + c[1].y) - (c[2].y + c[3].y) + ((s[0] + s[1]) - (s[2] + s[3])) / 4096;
    } else if (axis == 1) {
        dx = (c[0].x + c[2].x) - (c[1].x + c[3].x);
        dz = (c[0].z + c[2].z) - (c[1].z + c[3].z);
        dy = -(((c[0].y + c[2].y) - (c[1].y + c[3].y)) + ((s[0] + s[2]) - (s[1] + s[3])) / 4096);
    } else {
        return -1;
    }
    return Calc_Angle(dy, SquareRoot0(dx * dx + dz * dz));
}

/* Pitch and roll from four ground points (two pairs along the car, two across), plus offsets. */
void Calc_Car_Angles_Square(int car, const TrackFd *p0, const TrackFd *p1, const TrackFd *p2, const TrackFd *p3,
                            int16_t pitch, int16_t roll)
{
    int32_t dx = (p0->x + p2->x) - (p1->x + p3->x);
    int32_t dz = (p0->z + p2->z) - (p1->z + p3->z);
    int32_t dy = (p0->y + p2->y) - (p1->y + p3->y);
    car_states[car].angle[0] = (int16_t)(pitch + Calc_Angle(-dy, SquareRoot0(dx * dx + dz * dz)));
    dx = (p0->x + p1->x) - (p2->x + p3->x);
    dz = (p0->z + p1->z) - (p2->z + p3->z);
    dy = (p0->y + p1->y) - (p2->y + p3->y);
    car_states[car].angle[2] = (int16_t)(roll + Calc_Angle(dy, SquareRoot0(dx * dx + dz * dz)));
}

/* The car's own track record follows it. */
static void fd_at_car(int car)
{
    TrackFd *fd = &car_fd[car];
    const CarState *cs = &car_states[car];
    fd->x = cs->position[0];
    fd->y = cs->position[1];
    fd->z = cs->position[2];
    fd->w = cs->position[3];
    Track_Follow(fd);
    Map_Height(fd);
}

/* Ride height from the wheels: the car rises by what its wheels' ground rose, plus the suspension. */
static int32_t height_from_wheels(int car)
{
    const TrackFd *w = wheel_fd[car];
    const int32_t *s = car_blocks[car].suspension;
    int32_t was = (w[0].previous_y + w[1].previous_y + w[2].previous_y + w[3].previous_y) / 4;
    int32_t now = (w[0].y + w[1].y + w[2].y + w[3].y) / 4;
    return car_fd[car].previous_y - was + now + (s[0] + s[1] + s[2] + s[3]) / 0x4000;
}

void Car_Landed(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    for (int i = 0; i < 2; i++) {
        Calc_Object_MatrixYXZ(&cs->matrix, cs->angle);
        Get_Wheel_Positions(car);
        Calc_Car_Angles_From_Wheels(car);
    }
    int32_t y = height_from_wheels(car);
    cs->position[1] = y;
    cb->position[1] = y * ONE;
    cb->motion_state = MOTION_DRIVING;
    for (int k = 0; k < 4; k++) {
        cb->wheel_ground[k] = wheel_fd[car][k].y;
        cb->wheel_rise[k] = NO_RISE;
    }
    cb->ground_timer = 1;
}

/* Off the ground: keeps the attitude it had (relative to the new one) as tumble angles, scaled down
 * the faster it goes. */
static void Make_Car_Fly(CarBlock *cb, int car)
{
    CarState *cs = &car_states[car];
    cb->motion_state = MOTION_AIRBORNE;
    cb->position[1] += cb->velocity[1];
    cs->position[1] = cb->position[1] / 4096;
    cs->previous_matrix = cs->matrix;
    MATRIX back, now;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            back.m[i][j] = cs->previous_matrix.m[j][i];
    Calc_Car_Angles_From_Wheels(car);
    Calc_Object_MatrixYXZ(&now, cs->angle);
    MulMatrix2(&now, &back);
    int16_t a[3];
    Calc_Object_Angles(&back, a);
    int32_t k = 0x28a000 / (cb->speed + 0x28a);
    int32_t pitch = signed_angle(a[0]);
    int32_t roll = signed_angle(a[2]);
    cb->fly_angles[0] = (int16_t)(pitch * k / 4096);
    cb->fly_angles[1] = (int16_t)(cb->angular_velocity[1] / 4096);
    cb->fly_angles[2] = (int16_t)(roll * k / 4096);
    Calc_Object_MatrixYXZ(&cb->matrix, cb->fly_angles);
    cb->fly_corner_bias = -500;
}

/* The suspension of each wheel lowers the near model's vertices around it. */
static void Hide_Car_Vertices(int car, int wheel)
{
    if (car == current_player_car)
        return;
    for (int i = 0; i < 4; i++)
        car_vertices[car][hide_vertices[wheel][i]].vy = -0x61;
}

static void Lower_Car_Vertices(int car, int wheel)
{
    if (car == current_player_car)
        return;
    for (int i = 0; i < 4; i++)
        car_vertices[car][lower_vertices[wheel][i]].vy = (int16_t)(-0x80 - car_blocks[car].suspension[wheel] / 4096);
}

/* One wheel's spring, pushed by the car's change of velocity along the wheel's axis; a wheel coming
 * off drops away. */
static void spring_wheel(int car, int w, const SVECTOR *axis)
{
    CarBlock *cb = &car_blocks[car];
    int32_t push = (axis->vx * cb->velocity_change[0] + axis->vz * cb->velocity_change[2]) / 4096;
    if (cb->wheel_mode[w] == 0) {
        int32_t acc = (cb->suspension[w] + cb->suspension_velocity[w]) * 0x280 / 4096;
        cb->suspension[w] += cb->suspension_velocity[w];
        int32_t v = cb->suspension_velocity[w];
        cb->suspension_velocity[w] = push * 4 - acc - v / 8 + v;
        if (cb->suspension[w] > SUSPENSION_LIMIT) {
            cb->suspension[w] = SUSPENSION_LIMIT;
            cb->suspension_velocity[w] = 0;
        }
        if (cb->suspension[w] < -SUSPENSION_LIMIT) {
            cb->suspension[w] = -SUSPENSION_LIMIT;
            cb->suspension_velocity[w] = 0;
        }
    } else {
        if (cb->wheel_mode[w] == 1) {
            cb->suspension[w] -= 0x3000;
            if (cb->suspension[w] < -SUSPENSION_LIMIT) {
                cb->wheel_mode[w] = 2;
                Hide_Car_Vertices(car, w);
            }
        }
        if (cb->wheel_mode[w] == 2) {
            cb->suspension[w] = -SUSPENSION_LIMIT;
            cb->suspension_velocity[w] = 0;
        }
    }
    Lower_Car_Vertices(car, w);
}

/* A wheel in the air hangs down to full extension. */
static void extend_wheel(int car, int w)
{
    int32_t *s = &car_blocks[car].suspension[w];
    *s += (SUSPENSION_LIMIT - *s) / 4;
}

void Calc_Suspension_All_Wheels(int car)
{
    for (int w = 0; w < 4; w++)
        spring_wheel(car, w, &wheel_axis[car][w]);
}

void Calc_Suspension_Left_Wheels(int car)
{
    for (int w = 0; w < 2; w++)
        spring_wheel(car, w, &wheel_axis[car][w]);
    for (int w = 2; w < 4; w++)
        extend_wheel(car, w);
}

void Calc_Suspension_Right_Wheels(int car)
{
    for (int w = 2; w < 4; w++)
        spring_wheel(car, w, &wheel_axis[car][w]);
    for (int w = 0; w < 2; w++)
        extend_wheel(car, w);
}

void Calc_Null_Suspension(int car)
{
    for (int w = 0; w < 4; w++) {
        car_blocks[car].suspension[w] -= car_blocks[car].suspension[w] / 4;
        Lower_Car_Vertices(car, w);
    }
}

void Calc_Extended_Suspension(int car)
{
    for (int w = 0; w < 4; w++) {
        extend_wheel(car, w);
        Lower_Car_Vertices(car, w);
    }
}

/* The wheels' heights follow the suspension, they turn with the car's speed, and the car moves by its
 * velocity and turns by its yaw rate. */
void Integrate_Car_Position(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    cs->wheel[0].centre[1] = (int16_t)(WHEEL_REST_Y - cb->suspension[3] / 4096);
    cs->wheel[1].centre[1] = (int16_t)(WHEEL_REST_Y - cb->suspension[1] / 4096);
    cs->wheel[2].centre[1] = (int16_t)(WHEEL_REST_Y - cb->suspension[2] / 4096);
    cs->wheel[3].centre[1] = (int16_t)(WHEEL_REST_Y - cb->suspension[0] / 4096);
    cs->wheel[0].spin = (int16_t)(cs->wheel[0].spin - (int16_t)cb->forward_speed);
    cs->wheel[2].spin = (int16_t)(cs->wheel[2].spin - (int16_t)cb->forward_speed);
    uint16_t front = (uint16_t)cs->wheel[0].spin, back = (uint16_t)cs->wheel[2].spin;
    cs->wheel[0].spin = (int16_t)(front & 0x1fe);
    cs->wheel[1].spin = (int16_t)(front & 0x1fe);
    cs->wheel[2].spin = (int16_t)(back & 0x1fe);
    cs->wheel[3].spin = (int16_t)(back & 0x1fe);
    cb->angle[1] += cb->angular_velocity[1];
    cb->position[0] += cb->velocity[0];
    cb->position[2] += cb->velocity[2];
    cs->angle[1] = (int16_t)(cb->angle[1] / 4096);
    cs->position[0] = cb->position[0] / 4096;
    cs->position[2] = cb->position[2] / 4096;
}

/* Wheel lift masks (bit per wheel) that throw the car into the air (the original's jump table). */
static int lift_makes_fly(unsigned lifted)
{
    static const uint8_t fly[16] = {0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1};
    return fly[lifted];
}

/* On four wheels: the car follows the ground under its wheels; a wheel dropping away faster than the
 * ground falls can throw the car into the air. */
void Car_Drive_Motion_3D_(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    fd_at_car(car);
    Calc_Object_MatrixYXZ(&cs->matrix, cs->angle);
    Get_Wheel_Positions(car);
    int32_t y = height_from_wheels(car);
    cs->position[1] = y;
    cb->position[1] = y * ONE;
    Calc_Car_Angles_From_Wheels(car);
    TrackFd *w = wheel_fd[car];
    int32_t rise[4];
    for (int k = 0; k < 4; k++)
        rise[k] = w[k].y - cb->wheel_ground[k];
    unsigned lifted = 0;
    if (cb->ground_timer == 0) {
        for (int k = 0; k < 4; k++) {
            if (cb->wheel_rise[k] - rise[k] > 0x18) {
                lifted |= 1u << k;
                w[k].y += -0x18 + (cb->wheel_rise[k] - rise[k]);
            }
        }
    } else {
        cb->ground_timer = 0;
    }
    if (lift_makes_fly(lifted)) {
        Make_Car_Fly(cb, car);
        return;
    }
    for (int k = 0; k < 4; k++) {
        cb->wheel_ground[k] = w[k].y;
        cb->wheel_rise[k] = rise[k];
    }
    cb->velocity[1] = (rise[0] + rise[1] + rise[2] + rise[3]) * 0x400;
}

static void track_dust(VECTOR *pos, const TrackFd *a, const TrackFd *b)
{
    pos->vx = (a->x + b->x) / 2 + ((rand() & 0x7f) - 0x40);
    pos->vy = (a->y + b->y) / 2;
    pos->vz = (a->z + b->z) / 2 + ((rand() & 0x7f) - 0x40);
}

static void dust(const VECTOR *pos, int32_t amount, int32_t r, int32_t g, int32_t b)
{
    int32_t p = projection_distance;
    ParticleParams pp = {amount * p * 2, amount * p, (amount * p * 2 - amount * p) / 16, 0, r, g, b, r, g, b, 0, 0, 0, 1};
    Smoke(pos, &particles[Pop_Particle_Slot()], &pp);
}

int32_t skid_sound_wait; /* 0x80079C14: frames before the next skid sound */

/* Skidding wheels raise smoke, and on loose ground dust (or flames) in their tracks; one axle each
 * frame. */
static void skid_effects(int car, const StripQuad *plane, int32_t amount, int32_t front, int32_t rear)
{
    const TrackFd *w = wheel_fd[car];
    int surface = quad_surface(plane);
    const TrackFd *a = buffer_num == 0 ? &w[0] : &w[1];
    const TrackFd *b = buffer_num == 0 ? &w[2] : &w[3];
    int32_t slide = buffer_num == 0 ? rear : front;
    VECTOR pos;
    if (abs(slide) > 0x14000) {
        track_dust(&pos, a, b);
        if (surface == SURFACE_ROAD) {
            dust(&pos, amount, 0x18000, 0x18000, 0x18000);
            if (buffer_num != 0 && car == current_player_car && skid_sound_wait == 0) {
                Allocate_Sound_Effect(SAMPLE_SKID, CAR_SOUND_VOLUME, &pos.vx);
                skid_sound_wait = 50;
            }
        }
    }
    if (surface != SURFACE_LOOSE || abs(rear) < 0x5001)
        return;
    track_dust(&pos, a, b);
    if (current_level == LEVEL_CHALK_CANYON) {
        dust(&pos, amount, 0x18000, 0x18000, 0x10000);
    } else if (current_level == LEVEL_BLACK_SAIL_VALLEY) {
        dust(&pos, amount, 0xc000, 0xc000, 0x9000);
    } else {
        int32_t p = projection_distance;
        ParticleParams pp = {amount * p * 2, amount * p, (amount * p * 2 - amount * p) / 16, 0,
                             0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0, 0, 0, 2};
        Fire(&pos, &particles[Pop_Particle_Slot()], &pp);
    }
}

/* The tyre model of a car on its four wheels: slope, drive, the grip of the front and back axles
 * against their sideways slip, drag; it sets the yaw rate and velocity. */
void Car_Drive_Motion(int car)
{
    CarBlock *cb = &car_blocks[car];
    const StripQuad *plane = car_fd[car].plane;
    int32_t grip = cb->grip;
    int32_t vx0 = cb->velocity[0], vz0 = cb->velocity[2];
    int16_t nx = plane->normal[0][0], ny = plane->normal[0][1], nz = plane->normal[0][2];
    if (ny < 0xdac && cb->ground_slope > 0xed8) {
        /* hit a wall-steep face */
        for (int k = 0; k < 3; k++)
            cb->velocity[k] /= 8;
        for (int k = 0; k < 4; k++)
            cb->wheel_rise[k] = NO_RISE;
    }
    int32_t drive = cb->throttle * cb->drive_factor / 4096;
    cb->ground_slope = ny;
    if (cb->wrecked == 0) {
        cb->velocity[0] += nx * 0xccb0 / 4096;
        cb->velocity[2] += nz * 0xccb0 / 4096;
    } else {
        /* a wreck brakes itself */
        int32_t fs = cb->forward_speed;
        if (fs >= 5)
            drive = fs * 0x800 > 0x2000 ? -0x2000 : fs * -0x800;
        else if (fs < -4)
            drive = fs * 0x800 < -0x2000 ? 0x2000 : fs * -0x800;
        else
            drive = 0;
    }
    int heading = cb->angle[1] / 4096;
    int32_t vx = cb->velocity[0] / 16;
    int32_t vz = cb->velocity[2] / 16;
    int32_t yaw = cb->angular_velocity[1] / 16;
    int s = quad_surface(plane);
    grip = grip * surface_factors[s] / 4096;
    drive = drive * surface_factors[s + 2] / 4096;
    int32_t steer = cb->steering + cb->steer_bias;
    int32_t rear_limit = grip * (cb->steer_factor + 2) / 4;
    int32_t front_k, rear_k;
    if (cb->car_type == 0) {
        if (drive != 0)
            front_k = rear_k = 0x800;
        else
            front_k = 0x862, rear_k = 0x79e;
        drive = drive * 0xf3c / 4096;
        grip = grip * 5000 / 4096;
    } else if (cb->car_type == 1) {
        if (drive < 0)
            rear_k = 0x942, front_k = 0x6be;
        else if (drive > 0)
            rear_k = front_k = 0x800;
        else
            rear_k = 0x76c, front_k = 0x894;
        drive = drive * 4000 / 4096;
        grip = grip * 0x1194 / 4096;
    } else {
        if (drive < 0)
            rear_k = 0xa6e, front_k = 0x592;
        else if (drive > 0)
            rear_k = front_k = 0x800;
        else
            rear_k = 0x71c, front_k = 0x8e4;
    }
    int32_t c = rcos(heading), sn = rsin(heading);
    int turned = heading - steer * cb->steer_factor / 2;
    int32_t c2 = rcos(turned), s2 = rsin(turned);
    int32_t speed = SquareRoot0((vx / 256) * vx / 256 + (vz / 256) * vz / 256);
    cb->speed = speed;
    int32_t k = 0xc000 / (speed + 0x10) + 0x400;
    int32_t front_slip = ((vx * c - vz * sn) / 4096) * k / 256 + (yaw * 0x1c2 / 0x28c) * -8;
    int32_t f = front_slip;
    if (grip < f)
        f = grip;
    else if (f < -grip)
        f = -grip;
    f = f * front_k / 4096;
    int32_t fx = -f * c / 4096;
    int32_t fz = f * sn / 4096;
    int32_t rear_slip = ((vx * c2 - vz * s2) / 4096) * k / 256 +
                        ((yaw * ((c2 * c + s2 * sn) / 4096) / 4096) * -0x1c2 / 0x28c) * -8;
    int32_t r;
    if (rear_limit < rear_slip)
        r = rear_limit * rear_k;
    else if (rear_slip < -rear_limit)
        r = -rear_limit * rear_k;
    else
        r = rear_slip * rear_k;
    r /= 4096;
    int32_t rx = -r * c2 / 4096;
    int32_t rz = r * s2 / 4096;
    int32_t cs_ = rcos(steer);
    int32_t torque = ((fz - rz * cs_ / 4096) * sn - (fx - rx * cs_ / 4096) * c) / 4096;
    cb->angular_velocity[1] += torque;
    cb->velocity[0] += fx + rx - vx * 0x32 / 256 + drive * sn / 4096;
    cb->velocity[2] += fz + rz - vz * 0x32 / 256 + drive * c / 4096;
    cb->forward_speed = ((vx / 256) * sn + (vz / 256) * c) / 4096;
    if (car_states[car].drawn != 0) {
        int32_t amount = abs(speed) + abs(yaw / 64);
        if (amount > 0xf0)
            amount = 0xf0;
        skid_effects(car, plane, amount, front_slip, rear_slip);
    }
    cb->velocity_change[0] = cb->velocity[0] - vx0;
    cb->velocity_change[2] = cb->velocity[2] - vz0;
    Calc_Suspension_All_Wheels(car);
}

/* ---- the car off its wheels: on two wheels, on an edge, on one corner, in the air, on its roof */

int32_t corner_clearance[8]; /* 0x80080044: each corner's height above the ground under it */

/* The ground under each corner on the ground in each grounded mode, and the attitude offsets it lies
 * at (on its side, nose, tail, roof). */
static const int32_t ground_corners[5][4] = { /* 0x80070A18 */
    {4, 5, 0, 1}, {2, 3, 6, 7}, {0, 2, 4, 6}, {1, 3, 5, 7}, {6, 7, 4, 5},
};
static const int32_t ground_angles[5][2] = { /* 0x80070A68 */
    {0, -1024}, {0, 1024}, {1024, 0}, {-1024, 0}, {0, 2048},
};

/* A car on one corner hitting the ground with another: what it does next, by [new corner][old corner]
 * (-1: it pivots on the new corner). */
typedef struct {
    int32_t motion_state;
    int32_t roll_side;
} CornerLanding;

static const CornerLanding corner_landings[8][8] = { /* 0x80070A90 */
    {{-1, -1}, {1, 0}, {2, 2}, {0, -1}, {-1, -1}, {5, 0}, {5, 2}, {-1, -1}},
    {{1, 0}, {-1, -1}, {0, -1}, {2, 3}, {5, 0}, {-1, -1}, {-1, -1}, {5, 3}},
    {{2, 2}, {0, -1}, {-1, -1}, {1, 1}, {5, 2}, {-1, -1}, {-1, -1}, {5, 1}},
    {{0, -1}, {2, 3}, {1, 1}, {-1, -1}, {-1, -1}, {5, 3}, {5, 1}, {-1, -1}},
    {{-1, -1}, {5, 0}, {5, 2}, {-1, -1}, {-1, -1}, {2, 4}, {2, 6}, {5, 4}},
    {{5, 0}, {-1, -1}, {-1, -1}, {5, 3}, {2, 4}, {-1, -1}, {5, 4}, {2, 7}},
    {{5, 2}, {-1, -1}, {-1, -1}, {5, 1}, {2, 6}, {5, 4}, {-1, -1}, {2, 5}},
    {{-1, -1}, {5, 3}, {5, 1}, {-1, -1}, {5, 4}, {2, 7}, {2, 5}, {-1, -1}},
};

/* (angle - edge) on the circle lies more than a quarter turn below the edge. */
static int past(int32_t angle, int32_t edge)
{
    return ((uint32_t)(angle - edge) & ANGLE_MASK) > HALF_TURN + QUARTER_TURN;
}

static int32_t damp(int32_t v, int32_t k)
{
    return v + (v / 4096) * k;
}

/* Height from the ground under two corners the car rests on. */
static void edge_height(int car, int a, int b)
{
    int32_t y = car_fd[car].previous_y - (corner_fd[a].previous_y + corner_fd[b].previous_y) / 2 +
                (corner_fd[a].y + corner_fd[b].y) / 2;
    car_states[car].position[1] = y;
    car_blocks[car].position[1] = y * ONE;
}

static void thud(int car)
{
    Allocate_Sound_Effect(SAMPLE_CRASH, CAR_SOUND_VOLUME, &car_fd[car].x);
}

/* Falls back onto its four wheels, the springs taking the roll it fell with. */
static void back_on_wheels(int car, int32_t s0, int32_t s1, int32_t s2, int32_t s3)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    cb->motion_state = MOTION_DRIVING;
    Get_Wheel_Positions(car);
    for (int k = 0; k < 4; k++) {
        cb->wheel_ground[k] = wheel_fd[car][k].y;
        cb->wheel_rise[k] = NO_RISE;
    }
    cb->suspension_velocity[0] = s0;
    cb->suspension_velocity[1] = s1;
    cb->suspension_velocity[2] = s2;
    cb->suspension_velocity[3] = s3;
    Calc_Car_Angles_From_Wheels(car);
    Calc_Object_MatrixYXZ(&cs->matrix, cs->angle);
}

static void lie_on(int car, int mode)
{
    car_blocks[car].motion_state = MOTION_STUCK;
    car_blocks[car].ground_mode = mode;
    thud(car);
}

/* The roll (or pitch) keeps turning, slowing a little. */
static void settle_roll(int car)
{
    CarBlock *cb = &car_blocks[car];
    car_states[car].angle[2] = (int16_t)(cb->angle[2] / 4096);
    cb->angular_velocity[2] -= cb->angular_velocity[2] / 64;
    cb->angle[2] += cb->angular_velocity[2];
}

static void settle_pitch(int car)
{
    CarBlock *cb = &car_blocks[car];
    car_states[car].angle[0] = (int16_t)(cb->angle[0] / 4096);
    cb->angular_velocity[0] -= cb->angular_velocity[0] / 64;
    cb->angle[0] += cb->angular_velocity[0];
}

/* Turns `m` by `angle` about the level axis at right angles to (dx, dz) (of length d): the body tipping
 * over a corner that far from its centre. */
static void tip_about_corner(MATRIX *m, int32_t dx, int32_t dz, int32_t d, int angle)
{
    int16_t c = (int16_t)(dz * ONE / d), s = (int16_t)(dx * ONE / d), ns = (int16_t)(dx * -ONE / d);
    int16_t ac = (int16_t)rcos(angle), as = (int16_t)rsin(angle);
    MATRIX to = {{{c, 0, s}, {0, ONE, 0}, {ns, 0, c}}, {0, 0, 0}};
    MATRIX tip = {{{ONE, 0, 0}, {0, ac, as}, {0, (int16_t)-as, ac}}, {0, 0, 0}};
    MATRIX back = {{{c, 0, ns}, {0, ONE, 0}, {s, 0, c}}, {0, 0, 0}};
    MulMatrix2(&to, &tip);
    MulMatrix2(&tip, &back);
    MulMatrix2(&back, m);
}

/* A corner hits the ground: the velocity into the ground goes, and turns into a tumble about it. */
void Car_Corner_Impact(int car, int corner)
{
    CarBlock *cb = &car_blocks[car];
    const int16_t *n = corner_fd[corner].plane->normal[0];
    int32_t into = (n[0] * (cb->velocity[0] / 4096) + n[1] * (cb->velocity[1] / 4096) +
                    n[2] * (cb->velocity[2] / 4096)) / 4096;
    for (int k = 0; k < 3; k++)
        cb->velocity[k] -= n[k] * into;
    int32_t dx = corner_fd[corner].x - car_fd[car].x;
    int32_t dz = corner_fd[corner].z - car_fd[car].z;
    int32_t d = SquareRoot0(dx * dx + dz * dz);
    if (d != 0)
        tip_about_corner(&cb->matrix, dx, dz, d, -into * d / 512);
}

/* The corner lowest below (`below`) the ground, other than `skip`, recording every corner's
 * clearance. */
static int lowest_corner(int skip, int32_t below)
{
    int best = -1;
    for (int k = 0; k < 8; k++) {
        int32_t d = corner_pos[k][1] - corner_fd[k].y;
        corner_clearance[k] = d;
        if (k != skip && d < below) {
            best = k;
            below = d;
        }
    }
    return best;
}

static void ease_to_zero(int16_t *a)
{
    if (*a < 0)
        (*a)++;
    else if (*a > 0)
        (*a)--;
}

/* In the air: tumbles by its fly matrix (the tumble dying away), falls, and lands on the first corner
 * to go below the ground while moving into it. */
void Car_Fly_Motion_3D(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    fd_at_car(car);
    MulMatrix2(&cb->matrix, &cs->matrix);
    if ((frame_count & 7) == 0) {
        ease_to_zero(&cb->fly_angles[0]);
        ease_to_zero(&cb->fly_angles[2]);
        Calc_Object_MatrixYXZ(&cb->matrix, cb->fly_angles);
    }
    Get_Corner_Positions(car);
    int best = lowest_corner(-1, cb->fly_corner_bias);
    if (cb->fly_corner_bias < 0)
        cb->fly_corner_bias += 0xfa;
    const StripQuad *q = car_fd[car].plane;
    int32_t into = (q->normal[0][0] + q->normal[1][0]) / 2 * (cb->velocity[0] / 4096) +
                   (q->normal[0][1] + q->normal[1][1]) / 2 * (cb->velocity[1] / 4096) +
                   (q->normal[0][2] + q->normal[1][2]) / 2 * (cb->velocity[2] / 4096);
    if (into < 0 && best >= 0) {
        int32_t y = car_fd[car].previous_y - corner_fd[best].previous_y + corner_fd[best].y;
        cs->position[1] = y;
        cb->position[1] = y * ONE;
        cb->motion_state = MOTION_ONE_CORNER;
        cb->pivot_corner = best;
        Car_Corner_Impact(car, best);
        Allocate_Sound_Effect(SAMPLE_CRASH, LANDING_VOLUME, &corner_fd[best].x);
    } else {
        cb->velocity[1] -= 0x6000;
        cb->position[1] += cb->velocity[1];
        cs->position[1] = cb->position[1] / 4096;
    }
}

/* Lying on its side, nose, tail or roof: rests on the four corners of that face. */
void Car_Grounded_Motion_3D(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    const int32_t *c = ground_corners[cb->ground_mode];
    fd_at_car(car);
    Calc_Object_MatrixYXZ(&cs->matrix, cs->angle);
    Get_Corner_Positions(car);
    int32_t was = (corner_fd[c[0]].previous_y + corner_fd[c[1]].previous_y + corner_fd[c[2]].previous_y +
                   corner_fd[c[3]].previous_y) / 4;
    int32_t now = (corner_fd[c[0]].y + corner_fd[c[1]].y + corner_fd[c[2]].y + corner_fd[c[3]].y) / 4;
    int32_t y = car_fd[car].previous_y - was + now;
    cs->position[1] = y;
    cb->position[1] = y * ONE;
    Calc_Car_Angles_Square(car, &corner_fd[c[0]], &corner_fd[c[1]], &corner_fd[c[2]], &corner_fd[c[3]],
                           (int16_t)ground_angles[cb->ground_mode][0], (int16_t)ground_angles[cb->ground_mode][1]);
}

/* On the two wheels of one side (roll_side 0 left, 1 right): gravity rolls it back onto four wheels or
 * over onto its side. */
void Car_Side_Roll_Motion(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    int side = cb->roll_side;
    Calc_Object_MatrixYXZ(&cs->matrix, cs->angle);
    fd_at_car(car);
    Get_Corner_Positions(car);
    int32_t c = rcos(-cs->angle[1]), s = rsin(-cs->angle[1]);
    int32_t roll = cb->angle[2] / 4096;
    if (side == 0) {
        edge_height(car, 0, 1);
        cs->angle[0] = (int16_t)Calc_Car_Tilt(car, 1);
        int32_t t = Calc_Car_Tilt(car, 0);
        int32_t slide = (rcos(-cb->angle[2] / 4096 + 0x18e) - rcos(0x18e - cs->angle[2])) * 0xe2 / 4096;
        cb->position[0] -= slide * c;
        cb->position[2] -= slide * s;
        cb->angular_velocity[2] += rcos(0x18e - cs->angle[2]) * 8;
        if (past(roll, t + HALF_TURN + QUARTER_TURN) && cb->angular_velocity[2] < 1) {
            cb->angular_velocity[2] = damp(cb->angular_velocity[2], -0x5dc);
            if (cb->angular_velocity[2] > -0x1e000) {
                lie_on(car, 0);
                return;
            }
            cb->motion_state = MOTION_TWO_CORNER_ROLL;
            cb->roll_side = 4;
            thud(car);
        } else if (past(roll, t + QUARTER_TURN) && cb->angular_velocity[2] >= 0) {
            int32_t v = cb->angular_velocity[2] = damp(cb->angular_velocity[2], -3000);
            if (v < 0x1e000) {
                back_on_wheels(car, v, v, -v, -v);
                return;
            }
            cb->roll_side = 1;
            thud(car);
        }
    } else if (side == 1) {
        edge_height(car, 2, 3);
        cs->angle[0] = (int16_t)Calc_Car_Tilt(car, 1);
        int32_t t = Calc_Car_Tilt(car, 0);
        int32_t slide = (rcos(0x672 - roll) - rcos(0x672 - cs->angle[2])) * 0xe2 / 4096;
        cb->position[0] -= slide * c;
        cb->position[2] -= slide * s;
        cb->angular_velocity[2] += rcos(0x672 - cs->angle[2]) * 8;
        if (!past(roll, t + TURN) || cb->angular_velocity[2] > 0) {
            if (past(roll, t + HALF_TURN) && cb->angular_velocity[2] >= 0) {
                cb->angular_velocity[2] = damp(cb->angular_velocity[2], -0x5dc);
                if (cb->angular_velocity[2] < 0x1e000) {
                    lie_on(car, 1);
                    return;
                }
                cb->motion_state = MOTION_TWO_CORNER_ROLL;
                cb->roll_side = 5;
                thud(car);
            }
        } else {
            int32_t v = cb->angular_velocity[2] = damp(cb->angular_velocity[2], -3000);
            if (v > -0x1e000) {
                back_on_wheels(car, v, v, -v, -v);
                return;
            }
            cb->roll_side = 0;
            thud(car);
        }
    } else {
        return;
    }
    settle_roll(car);
}

/* Upside down onto its roof. */
static void turn_over(int car)
{
    CarBlock *cb = &car_blocks[car];
    cb->motion_state = MOTION_STUCK;
    cb->ground_mode = 4;
    cb->angle[0] += 0x800000;
    cb->angle[2] += 0x800000;
    cb->angle[1] += 0x800000;
    car_states[car].angle[1] = (int16_t)(cb->angle[1] / 4096);
    car_states[car].angle[2] = (int16_t)(cb->angle[2] / 4096);
}

/* Balanced on an edge: roll_side 2/3 on the left/right wheels' ends (pitching), 4/5 the front/back of
 * the roof (rolling), 6/7 on the roof's sides (pitching). It falls one way or the other. */
void Car_2pt_Motion_3D(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    int side = cb->roll_side;
    fd_at_car(car);
    switch (side) {
    case 2:
    case 3:
    case 6:
    case 7: {
        static const uint8_t edges[8][2] = {[2] = {0, 2}, [3] = {1, 3}, [6] = {4, 6}, [7] = {5, 7}};
        int sign = side == 2 || side == 6 ? -1 : 1;
        Calc_Object_MatrixYZX(&cs->matrix, cs->angle);
        Get_Corner_Positions(car);
        edge_height(car, edges[side][0], edges[side][1]);
        cs->angle[2] = (int16_t)Calc_Car_Tilt(car, 0);
        int32_t t = Calc_Car_Tilt(car, 1);
        int32_t slide = (rcos(cb->angle[0] / 4096 + 0xb7) - rcos(cs->angle[0] + 0xb7)) * 0x1d4 / 4096;
        cb->position[0] += sign * slide * rsin(cs->angle[1]);
        cb->position[2] += sign * slide * rcos(cs->angle[1]);
        cb->angular_velocity[0] += sign * rcos(cb->angle[0] / 4096) * 8;
        int32_t pitch = cb->angle[0] / 4096;
        int32_t v = cb->angular_velocity[0];
        if (side == 2) {
            if (past(pitch, t + TURN) && v < 1) {
                back_on_wheels(car, -v, v, -v, v);
                return;
            }
            if (past(pitch, t + HALF_TURN) && v >= 0) {
                cb->roll_side = 6;
                thud(car);
                return;
            }
        } else if (side == 3) {
            if (past(pitch, t + QUARTER_TURN) && v >= 0) {
                back_on_wheels(car, -v, v, -v, v);
                return;
            }
            if (past(pitch, t + HALF_TURN + QUARTER_TURN) && v < 1) {
                cb->roll_side = 7;
                thud(car);
                return;
            }
        } else if (side == 6) {
            if (past(pitch, QUARTER_TURN - t) && v < 1) {
                cb->roll_side = 2;
                thud(car);
                return;
            }
            if (past(pitch, HALF_TURN + QUARTER_TURN - t) && v >= 0) {
                thud(car);
                cb->angular_velocity[0] = damp(v, -2000);
                if (cb->angular_velocity[0] > 0x13fff)
                    cb->roll_side = 7;
                else
                    turn_over(car);
                return;
            }
        } else {
            if (past(pitch, TURN - t) && v >= 0) {
                cb->roll_side = 3;
                thud(car);
                return;
            }
            if (past(pitch, HALF_TURN - t) && v < 1) {
                thud(car);
                cb->angular_velocity[0] = damp(v, -2000);
                if (cb->angular_velocity[0] < -0x13fff)
                    cb->roll_side = 6;
                else
                    turn_over(car);
                return;
            }
        }
        settle_pitch(car);
        return;
    }
    case 4:
    case 5: {
        int front = side == 4;
        Calc_Object_MatrixYXZ(&cs->matrix, cs->angle);
        Get_Corner_Positions(car);
        edge_height(car, front ? 4 : 6, front ? 5 : 7);
        cs->angle[0] = (int16_t)Calc_Car_Tilt(car, 1);
        int32_t t = Calc_Car_Tilt(car, 0);
        int32_t roll = cb->angle[2] / 4096;
        int32_t slide, push;
        if (front) {
            slide = (rcos(-cb->angle[2] / 4096 - 0x18e) - rcos(-0x18e - cs->angle[2])) * 0xe2 / 4096;
            push = rcos(-0x18e - cs->angle[2]);
        } else {
            slide = (rcos(0x98e - roll) - rcos(0x98e - cs->angle[2])) * 0xe2 / 4096;
            push = rcos(0x98e - cs->angle[2]);
        }
        cb->position[0] += -slide * rcos(cs->angle[1]);
        cb->position[2] += slide * rsin(cs->angle[1]);
        cb->angular_velocity[2] += push * 8;
        int32_t near_edge = front ? HALF_TURN - t : QUARTER_TURN - t;
        int32_t far_edge = front ? TURN - t : HALF_TURN + QUARTER_TURN - t;
        if (!past(roll, near_edge) || cb->angular_velocity[2] > 0) {
            if (past(roll, far_edge) && cb->angular_velocity[2] >= 0) {
                cb->angular_velocity[2] = damp(cb->angular_velocity[2], -0x5dc);
                if (cb->angular_velocity[2] < 0x1e000) {
                    lie_on(car, front ? 0 : 4);
                    return;
                }
                if (front) {
                    cb->motion_state = MOTION_SIDE_ROLL;
                    cb->roll_side = 0;
                } else {
                    cb->roll_side = 4;
                }
                thud(car);
            }
        } else {
            cb->angular_velocity[2] = damp(cb->angular_velocity[2], -0x5dc);
            if (cb->angular_velocity[2] > -0x1e000) {
                lie_on(car, front ? 4 : 1);
                return;
            }
            if (front) {
                cb->roll_side = 5;
            } else {
                cb->motion_state = MOTION_SIDE_ROLL;
                cb->roll_side = 1;
            }
            thud(car);
        }
        settle_roll(car);
        return;
    }
    default:
        return;
    }
}

/* Where a car-space corner points with the given body rotation. */
static void corner_direction(const MATRIX *m, int corner, SVECTOR *out)
{
    gte_SetRotMatrix(m);
    gte_ldv0(&corner_offsets[corner]);
    gte_rtv0();
    out->vx = (int16_t)gte.ir[1];
    out->vy = (int16_t)gte.ir[2];
    out->vz = (int16_t)gte.ir[3];
}

/* Spinning on one corner (pivot_corner) with its tumble matrix, held at the corner, toppling away from
 * the centre; when another corner hits the ground it lands on two, four wheels or a face. */
void Car_1pt_Motion_3D(int car)
{
    CarBlock *cb = &car_blocks[car];
    CarState *cs = &car_states[car];
    int pivot = cb->pivot_corner;
    SVECTOR before, after;
    corner_direction(&cs->matrix, pivot, &before);
    MulMatrix2(&cb->matrix, &cs->matrix);
    if ((frame_count & 3) == 0) {
        int16_t a[3];
        Calc_Object_Angles(&cs->matrix, a);
        Calc_Object_MatrixYXZ(&cs->matrix, a);
    }
    corner_direction(&cs->matrix, pivot, &after);
    fd_at_car(car);
    Get_Corner_Positions(car);
    int32_t dx = corner_fd[pivot].x - car_fd[car].x;
    int32_t dz = corner_fd[pivot].z - car_fd[car].z;
    int32_t d = SquareRoot0(dx * dx + dz * dz);
    if (d != 0)
        tip_about_corner(&cb->matrix, dx, dz, d, d / 32);
    int32_t y = car_fd[car].previous_y - corner_fd[pivot].previous_y + corner_fd[pivot].y;
    cs->position[1] = y;
    cb->position[1] = y * ONE;
    cb->position[0] += (before.vx - after.vx) * ONE;
    cb->position[2] += (before.vz - after.vz) * ONE;
    /* The corners drop by the pivot's height above its ground; the original rereads the pivot inside the
     * loop, so the corners after it keep their height. */
    for (int k = 0; k < 8; k++)
        corner_pos[k][1] += corner_fd[pivot].y - corner_pos[pivot][1];
    int best = lowest_corner(pivot, 0);
    int16_t tumble[3];
    Calc_Object_Angles(&cb->matrix, tumble);
    if (best >= 0 && corner_clearance[pivot] > corner_clearance[best]) {
        for (int k = 0; k < 3; k++)
            tumble[k] = (int16_t)signed_angle(tumble[k]);
        Allocate_Sound_Effect(SAMPLE_CRASH, CAR_SOUND_VOLUME, &corner_fd[best].x);
        const CornerLanding *landing = &corner_landings[best][pivot];
        switch (landing->motion_state) {
        case MOTION_DRIVING: {
            MATRIX next;
            int16_t a[3];
            Calc_Object_Angles(&cs->matrix, cs->angle);
            MulMatrix0(&cb->matrix, &cs->matrix, &next);
            Calc_Object_Angles(&next, a);
            int32_t yaw = a[1] - cs->angle[1];
            yaw = signed_angle(yaw);
            if (abs(yaw) > 0x1ff)
                yaw = 0;
            cb->angular_velocity[1] = yaw * ONE;
            for (int k = 0; k < 3; k++)
                cb->angle[k] = cs->angle[k] * ONE;
            int32_t v = cb->velocity[1];
            if (v > -0x20000)
                v = -0x20000;
            Car_Landed(car);
            for (int k = 0; k < 4; k++)
                cb->suspension_velocity[k] = v;
            break;
        }
        case MOTION_SIDE_ROLL:
        case MOTION_TWO_CORNER_ROLL:
        case MOTION_STUCK: {
            MATRIX next;
            int16_t a[3];
            Calc_Object_Angles(&cs->matrix, cs->angle);
            MulMatrix0(&cb->matrix, &cs->matrix, &next);
            Calc_Object_Angles(&next, a);
            for (int k = 0; k < 3; k++) {
                int16_t turn = (int16_t)signed_angle((int16_t)(a[k] - cs->angle[k]));
                if (abs(turn) > 0x1ff)
                    turn = 0;
                cb->angle[k] = cs->angle[k] * ONE;
                cb->angular_velocity[k] = turn * ONE;
            }
            cb->motion_state = landing->motion_state;
            cb->roll_side = landing->roll_side;
            break;
        }
        case -1:
            cb->pivot_corner = best;
            tumble[0] = (int16_t)(tumble[0] - tumble[0] / 2);
            tumble[2] = (int16_t)(tumble[2] - tumble[2] / 2);
            break;
        default:
            break;
        }
    }
    Calc_Object_MatrixYXZ(&cb->matrix, tumble);
}

/* Off its wheels and still for a while: put back on them. The player's car anywhere, the others only
 * away from the player. */
static int stuck(int car)
{
    if (car_blocks[car].motion_state == MOTION_STUCK)
        stuck_timer[car]++;
    else
        stuck_timer[car] = 0;
    return stuck_timer[car] == 50 && car_blocks[car].wrecked == 0;
}

void Recover_Stuck_Cars(void)
{
    if (stuck(0)) {
        car_blocks[0].motion_state = MOTION_DRIVING;
        Car_Landed(0);
    }
    for (int car = 1; car < num_cars; car++) {
        if (!stuck(car))
            continue;
        int32_t dx = car_states[car].position[0] - car_states[0].position[0];
        int32_t dz = car_states[car].position[2] - car_states[0].position[2];
        if ((dx / 64) * dx / 64 + (dz / 64) * dz / 64 > 0x4000) {
            car_blocks[car].motion_state = MOTION_DRIVING;
            Car_Landed(car);
        }
    }
}

/* On two wheels of one side: the tyre model on those two, the roll pushed by their slip. */
void Car_Drive_2pt_Motion(int car)
{
    CarBlock *cb = &car_blocks[car];
    const StripQuad *plane = car_fd[car].plane;
    int32_t vx0 = cb->velocity[0], vz0 = cb->velocity[2];
    cb->velocity[0] += plane->normal[0][0] * 0xccb0 / 4096;
    cb->velocity[2] += plane->normal[0][2] * 0xccb0 / 4096;
    int heading = cb->angle[1] / 4096;
    int32_t vx = cb->velocity[0] / 4096, vz = cb->velocity[2] / 4096;
    int32_t yaw = cb->angular_velocity[1];
    int32_t drive = cb->throttle * cb->drive_factor / 4096;
    int32_t steer = cb->steering + cb->steer_bias;
    if (cb->wrecked != 0)
        drive = 0;
    int32_t grip = cb->grip * rcos(cb->angle[2] / 4096) / 8192;
    drive = drive * rcos(cb->angle[2] / 4096) / 8192;
    int32_t front_k, rear_k;
    if (cb->car_type == 0) {
        if (drive == 0)
            rear_k = 0x79e, front_k = 0x862;
        else
            rear_k = front_k = 0x800;
    } else if (cb->car_type == 1) {
        if (drive < 0)
            rear_k = 0x942, front_k = 0x6be;
        else if (drive == 0)
            rear_k = 0x76c, front_k = 0x894;
        else
            rear_k = front_k = 0x800;
    } else {
        if (drive < 0)
            rear_k = 0xa6e, front_k = 0x592;
        else if (drive == 0)
            rear_k = 0x71c, front_k = 0x8e4;
        else
            rear_k = front_k = 0x800;
    }
    int32_t c = rcos(heading), sn = rsin(heading);
    int turned = heading - steer * cb->steer_factor / 2;
    int32_t c2 = rcos(turned), s2 = rsin(turned);
    int32_t speed = SquareRoot0(vx * vx + vz * vz);
    cb->speed = speed;
    int32_t k = 0xc000 / (speed + 0x10) + 0x400;
    int32_t front = ((vx * c - vz * sn) / 64) * k / 64 + ((yaw / 4096) * 0x1c2 / 0x28c) * -0x800;
    if (grip < front)
        front = grip;
    else if (front < -grip)
        front = -grip;
    int32_t f = front * front_k / 4096;
    int32_t fx = -f * c, fz = f * sn;
    int32_t rear = ((vx * c2 - vz * s2) / 64) * k / 64 +
                   (((yaw / 4096) * ((c2 * c + s2 * sn) / 4096) / 4096) * -0x1c2 / 0x28c) * -0x800;
    if (grip < rear)
        rear = grip;
    else if (rear < -grip)
        rear = -grip;
    int32_t r = rear * rear_k / 4096;
    int32_t rx = -r * c2, rz = r * s2;
    int32_t cs_ = rcos(steer);
    int32_t torque = ((fz / 4096 - (rz / 4096) * cs_ / 4096) * sn - (fx / 4096 - (rx / 4096) * cs_ / 4096) * c) / 4096;
    cb->angular_velocity[1] += torque;
    cb->velocity[0] += fx / 4096 + rx / 4096 + vx * -0x32 + drive * sn / 4096;
    cb->velocity[2] += fz / 4096 + rz / 4096 + vz * -0x32 + drive * c / 4096;
    cb->forward_speed = (vx * sn + vz * c) / 4096;
    cb->velocity_change[0] = cb->velocity[0] - vx0;
    cb->velocity_change[2] = cb->velocity[2] - vz0;
    if (cb->roll_side == 0) {
        cb->angular_velocity[2] -= rcos(cb->angle[2] / 4096 + 0x272) * ((front + rear) / 0x4000);
        Calc_Suspension_Left_Wheels(car);
    } else {
        cb->angular_velocity[2] -= rcos(cb->angle[2] / 4096 - 0x272) * ((front + rear) / 0x4000);
        Calc_Suspension_Right_Wheels(car);
    }
}

/* With no wheels on the ground: slides on the body, rubbing off speed and spin, and an edge it rests on
 * rocks it. */
void Car_Drive_1pt_Motion(int car)
{
    CarBlock *cb = &car_blocks[car];
    const StripQuad *plane = car_fd[car].plane;
    cb->velocity[0] += plane->normal[0][0] * 0x6658 / 4096;
    cb->velocity[2] += plane->normal[0][2] * 0x6658 / 4096;
    int32_t g = surface_factors[quad_surface(plane)];
    int32_t rub = g * 4;
    int32_t speed = SquareRoot0((cb->velocity[0] / 4096) * (cb->velocity[0] / 4096) +
                                (cb->velocity[2] / 4096) * (cb->velocity[2] / 4096));
    cb->speed = speed;
    int moving = rub < speed * ONE;
    if (moving) {
        cb->velocity[0] -= rub * (cb->velocity[0] / 4096) / speed;
        cb->velocity[2] -= rub * (cb->velocity[2] / 4096) / speed;
    } else {
        cb->velocity[0] = 0;
        cb->velocity[2] = 0;
    }
    int32_t spin = cb->angular_velocity[1];
    if (rub / 2 < spin)
        cb->angular_velocity[1] = spin - rub / 2;
    else if (spin < g * -4 / 2)
        cb->angular_velocity[1] = spin + rub / 2;
    else
        cb->angular_velocity[1] = 0;
    if (cb->motion_state == MOTION_TWO_CORNER_ROLL && moving) {
        int dir = Calc_Angle(cb->velocity[0] / 4096, cb->velocity[2] / 4096);
        if (cb->roll_side == 4)
            cb->angular_velocity[2] -= (rcos(cb->angle[2] / 4096 + 0x272) * rcos(dir) / 4096) * (rub / 8192);
        else if (cb->roll_side == 5)
            cb->angular_velocity[2] -= (rcos(cb->angle[2] / 4096 - 0x272) * rcos(dir) / 4096) * (rub / 8192);
    }
    Calc_Null_Suspension(car);
}

/* One frame of a car's motion in its current state, then the barriers. */
void Car_Movement(int car)
{
    CarBlock *cb = &car_blocks[car];
    int state = cb->motion_state;
    switch (state) {
    case MOTION_DRIVING: {
        if (overlay_state < 1)
            Car_Drive_Motion(car);
        Integrate_Car_Position(car);
        Car_Drive_Motion_3D_(car);
        uint8_t hit = 0;
        for (int w = 0; w < 4; w++)
            hit |= (uint8_t)Barrier_Collision(w, car);
        cb->barrier_hit = hit;
        break;
    }
    case MOTION_SIDE_ROLL:
        Car_Drive_2pt_Motion(car);
        Integrate_Car_Position(car);
        Car_Side_Roll_Motion(car);
        break;
    case MOTION_TWO_CORNER_ROLL:
        Car_Drive_1pt_Motion(car);
        Integrate_Car_Position(car);
        Car_2pt_Motion_3D(car);
        break;
    case MOTION_ONE_CORNER:
        Car_Drive_1pt_Motion(car);
        Integrate_Car_Position(car);
        Car_1pt_Motion_3D(car);
        break;
    case MOTION_AIRBORNE:
        cb->velocity[0] -= cb->velocity[0] * 0x14 / 4096;
        cb->velocity[2] -= cb->velocity[2] * 0x14 / 4096;
        Calc_Extended_Suspension(car);
        Integrate_Car_Position(car);
        Car_Fly_Motion_3D(car);
        break;
    case MOTION_STUCK:
        Car_Drive_1pt_Motion(car);
        Integrate_Car_Position(car);
        Car_Grounded_Motion_3D(car);
        break;
    default:
        break;
    }
    if (state >= MOTION_SIDE_ROLL && state <= MOTION_STUCK) {
        /* off its wheels the body's corners meet the barriers */
        cb->barrier_hit = 0;
        for (int k = 0; k < 8; k++)
            Barrier_Corner_Collision(k, car);
        if (state == MOTION_STUCK)
            Sparking(cb->ground_mode, car);
    }
    if (cb->damage[0] >= MAX_DAMAGE || cb->damage[2] >= MAX_DAMAGE)
        cb->wrecked = 1;
    else if (cb->damage[0] > 0xe63 || cb->damage[2] > 0xe63)
        cb->heavy_damage = 1;
    if (current_level == LEVEL_DEATH_BOWL) {
        for (int i = 0; i < num_cars; i++)
            if (car_states[i].position[1] < -300)
                car_blocks[i].wrecked = 1;
    }
}

void Control_Player_Car_From_Pad(int car, const PadState *p)
{
    Control_Car_Replay(car, p);
}

/* The near model with its boot gone: the rear corners of the boot lid drop to the body. Car 0's
 * vertices are those the shared near model points at (the car drawn last, see Dent_Car_Zone), the other
 * cars' (except the player's) their own copy. */
void Boot_Lost_Geometry(int car)
{
    if (car == 0) {
        Mesh *mesh = car_objects[0][1].mesh;
        SVECTOR *v = (SVECTOR *)mesh_section(mesh, mesh->vertices);
        for (int i = 3; i < 5; i++) {
            v[i].vx = 0x8d;
            v[i].vy = 0x1a;
            v[i].vz = -0x19e;
        }
        for (int i = 2; i < 6; i += 3) {
            v[i].vx = -0x8d;
            v[i].vy = 0x1a;
            v[i].vz = -0x19e;
        }
    }
    if (car != current_player_car) {
        for (int i = 0x32; i < 0x34; i++) {
            car_vertices[car][i].vx = -0x82;
            car_vertices[car][i].vy = 0x1b;
            car_vertices[car][i].vz = -0x197;
        }
        for (int i = 0x2c; i < 0x32; i += 5) {
            car_vertices[car][i].vx = 0x98;
            car_vertices[car][i].vy = 0x1b;
            car_vertices[car][i].vz = -0x19e;
        }
    }
}

static int part_request; /* 0x80079A68: the last Request_Flying_Object */

static void wheel_handling(CarBlock *cb, int32_t bias, int32_t factor, int32_t drive)
{
    cb->steer_bias = bias;
    cb->steer_factor = factor;
    cb->drive_factor = drive;
}

static void wheel_remark(int car, int sample, int busy)
{
    if (car == current_player_car && commentary_busy == 0) {
        Play_Sound(COMMENTARY_CHANNEL, sample_bank, sample, SOUND_VOLUME_MAX, VOICE_PITCH, 0);
        commentary_busy = busy;
    }
}

/* A wheel whose zone is nearly wrecked comes off (a random chance until it is fully wrecked), at most
 * two per car and never two diagonally opposite. What is left decides how the car pulls and drives. */
void Detach_Damaged_Wheel(int car, int wheel)
{
    static const int wheel_zone[4] = {4, 5, 9, 7}; /* 0x80070DF4 */
    static const int wheel_slot[4] = {2, 0, 3, 1}; /* 0x80070DE4: its wheel_lost entry */
    CarBlock *cb = &car_blocks[car];
    int32_t *damage = &cb->damage[wheel_zone[wheel]];
    int slot = wheel_slot[wheel];
    if (*damage <= 0xf31)
        return;
    int32_t *lost = &cb->wheel_lost[slot];
    if (*lost == 1)
        return;
    part_request = Request_Flying_Object(FLYING_WHEEL);
    if (!((cb->position[0] & 3) == 0 || *damage > 4000) || cb->wheels_lost >= 2 || part_request != 1)
        return;
    if (cb->wheels_lost == 1) {
        *lost = 1;
        if (cb->wheel_lost[slot ^ 3] == 1) {
            *lost = 0;
            return;
        }
        cb->wheel_mode[wheel ^ 2] = 1;
        cb->wheels_lost = 2;
        *damage = MAX_DAMAGE;
        Setup_Flying_Objects(FLYING_WHEEL, cb->velocity, slot, car);
        if (cb->wheel_lost[0] == 1) {
            if (cb->wheel_lost[1] == 1) {
                wheel_handling(cb, 0, 2, ONE / 8);
                wheel_remark(car, SAY_REAR_WHEELS_LOST, 50);
            }
            if (cb->wheel_lost[0] == 1 && cb->wheel_lost[2] == 1)
                wheel_handling(cb, 0x80, 1, ONE / 4);
        }
        if (cb->wheel_lost[1] == 1 && cb->wheel_lost[3] == 1)
            wheel_handling(cb, -0x80, 1, ONE / 4);
        if (cb->wheel_lost[2] == 1 && cb->wheel_lost[3] == 1) {
            wheel_handling(cb, 0, 0, ONE / 2);
            wheel_remark(car, SAY_FRONT_WHEELS_LOST, 50);
        }
        return;
    }
    *lost = 1;
    cb->wheel_mode[wheel ^ 2] = 1;
    cb->wheels_lost = 1;
    Setup_Flying_Objects(FLYING_WHEEL, cb->velocity, slot, car);
    *damage = MAX_DAMAGE;
    switch (slot) {
    case 0:
    case 1:
        wheel_handling(cb, slot == 1 ? -0x80 : 0x80, 2, ONE / 2);
        wheel_remark(car, SAY_REAR_WHEEL_LOST, 55);
        break;
    case 2:
    case 3:
        wheel_handling(cb, slot == 2 ? 0x40 : -0x40, 1, ONE * 3 / 4);
        wheel_remark(car, SAY_FRONT_WHEEL_LOST, 50);
        break;
    }
}

void Check_Bonnet_Removal(int car)
{
    CarBlock *cb = &car_blocks[car];
    part_request = Request_Flying_Object(FLYING_BONNET);
    if (cb->bonnet_lost == 0 && part_request == 1) {
        cb->part_loss_timer = 100;
        cb->bonnet_lost = 1;
        int32_t velocity[4];
        memcpy(velocity, cb->velocity, sizeof(velocity));
        Setup_Flying_Objects(FLYING_BONNET, velocity, 0, car);
        Change_Bonnet_Clut(car, bonnet_index);
    }
}

void Check_Boot_Removal(int car)
{
    CarBlock *cb = &car_blocks[car];
    part_request = Request_Flying_Object(FLYING_BOOT);
    if (cb->boot_lost == 0 && part_request == 1) {
        Boot_Lost_Geometry(car);
        cb->boot_lost = 1;
        int32_t velocity[4];
        memcpy(velocity, cb->velocity, sizeof(velocity));
        Setup_Flying_Objects(FLYING_BOOT, velocity, 0, car);
        Change_Boot_Clut(car, boot_index);
    }
}
