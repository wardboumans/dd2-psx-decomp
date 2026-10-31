#include "game/triggers.h"

#include "game/ai.h"
#include "game/camera.h"
#include "game/car.h"
#include "game/car_motion.h"
#include "game/collision.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/overlays.h"
#include "game/play_game.h"
#include "game/track.h"

/* A car sliding on its body over hard ground throws sparks from what it lies on: two corners and the
 * middle of the other two on its side (ground modes 0 and 1), the middles of the roof's edges (4). */
void Sparking(int ground_mode, int car)
{
    static const int side_corners[2][4] = {{0, 1, 4, 5}, {2, 3, 6, 7}}; /* 0x80070FA0 */
    static const int roof_corners[4] = {4, 5, 6, 7};                    /* 0x80070FE0 */
    if (car_blocks[car].speed < 0x33)
        return;
    if (quad_surface(car_fd[car].plane) != SURFACE_ROAD)
        return;
    VECTOR pos;
    if (ground_mode == 4) {
        for (int i = 0; i < 3; i += 2) {
            const TrackFd *a = &corner_fd[roof_corners[i]], *b = &corner_fd[roof_corners[i + 1]];
            pos.vx = (a->x + b->x) / 2;
            pos.vy = (a->y + b->y) / 2 - 100;
            pos.vz = (a->z + b->z) / 2;
            Spark_At(&pos, car);
        }
        return;
    }
    if (ground_mode != 0 && ground_mode != 1)
        return;
    const int *c = side_corners[ground_mode];
    for (int i = 0; i < 2; i++) {
        pos.vx = corner_fd[c[i]].x;
        pos.vy = corner_fd[c[i]].y - (ground_mode == 0 ? 100 : 0);
        pos.vz = corner_fd[c[i]].z;
        Spark_At(&pos, car);
    }
    const TrackFd *a = &corner_fd[c[2]], *b = &corner_fd[c[3]];
    pos.vx = (a->x + b->x) / 2;
    pos.vy = (a->y + b->y) / 2 - 100;
    pos.vz = (a->z + b->z) / 2;
    Spark_At(&pos, car);
}

static void No_Trigger(int car) /* 0x8004806C, 0x80048204 */
{
    (void)car;
}

/* At the pit lane's mouth: the player turning off the track's direction (by more than an eighth of a
 * turn) goes into the pit lane. The heading read is car 0's whichever car passes, as on the PSX. */
static void Pit_Lane_Entry_Trigger(int car) /* 0x800480B8 */
{
    const CarBlock *cb = &car_blocks[car];
    const Strip *strip = strip_at(car_fd[car].strip);
    int32_t heading = (Get_Car_Angle(car_states[0].angle[1] & ANGLE_MASK) + cb->steering) & ANGLE_MASK;
    int32_t off = heading - road_angle(strip->heading);
    int32_t cosine;
    Get_Direction_Cosines(&off, &cosine);
    if ((off < TURN / 8 || off > TURN - TURN / 8) && car == 0)
        pit_in = 1;
}

/* Past the pit lane: back on the track (and in a stock car race the pit may be used again). */
static void Pit_Lane_Exit_Trigger(int car) /* 0x80048074 */
{
    if (car != 0 || pit_in == 0)
        return;
    pit_in = 0;
    if (race_mode == MODE_STOCK_CAR)
        pit_camera_done = 0;
}

/* In the pit: the repair camera starts, with five seconds to choose. */
static void Pit_Trigger(int car) /* 0x800481B0 */
{
    if (car != 0 || pit_camera_done != 0 || pit_in == 0)
        return;
    pit_camera_active = 1;
    pit_second = 0;
    pit_countdown = 5;
}

/* Each car that has just moved onto a new strip runs the strip's trigger (on tracks, not in the bowls). */
void Strip_Trigger_Handler(void)
{
    static void (*const triggers[7])(int car) = { /* 0x80070F84 */
        No_Trigger, No_Trigger, No_Trigger, Pit_Lane_Entry_Trigger, Pit_Lane_Exit_Trigger, Pit_Trigger, No_Trigger,
    };
    if (current_level >= FIRST_BOWL_LEVEL)
        return;
    for (int car = 0; car < num_cars; car++) {
        if (car_fd[car].step == 0)
            continue;
        unsigned t = strip_at(car_fd[car].strip)->trigger;
        if (t < 7)
            triggers[t](car);
    }
}
