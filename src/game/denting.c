#include "game/denting.h"

#include "game/car.h"
#include "game/car_cluts.h"
#include "game/car_init.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "graphics/object.h"

#include <string.h>

/* The undented vertices of the near model (the other cars) and of the player's detailed model. */
static SVECTOR mid_car_base[CAR_MODEL_VERTICES]; /* 0x80088DA0 */
static SVECTOR hi_car_base[HI_CAR_VERTICES];     /* 0x800907F4 */

uint8_t dent_levels[MAX_CARS][12]; /* 0x800890D0 */

/* A vertex moved by a damage zone: its push at full damage (MAX_DAMAGE). */
typedef struct {
    int16_t dx, dy, dz, vertex;
} DentRecord;

static const DentRecord dent_00[] = { /* 0x80015C04 */
    {0, 0, -24, 14}, {0, 0, -31, 16}, {0, 0, -30, 46}, {0, 0, -25, 50}, {0, 0, -34, 51}, {0, 0, -34, 52}, {0, 0, 0, -1},
};
static const DentRecord dent_01[] = { /* 0x80015C3C */
    {0, 0, -24, 14}, {0, 0, -31, 16}, {0, 0, -30, 46}, {0, 0, -25, 50}, {0, 0, -34, 51}, {0, 0, -34, 52},
    {-9, 24, -10, 60}, {-19, 40, -18, 102}, {0, 0, 0, -1},
};
static const DentRecord dent_02[] = { /* 0x80015C84 */
    {0, 0, -28, 9}, {0, 0, -36, 13}, {0, 0, -35, 47}, {0, 0, -36, 48}, {0, 0, -28, 49}, {0, 0, -37, 53}, {0, 0, 0, -1},
};
static const DentRecord dent_03[] = { /* 0x80015CBC */
    {0, 0, -28, 9}, {0, 0, -36, 13}, {0, 0, -35, 47}, {0, 0, -36, 48}, {0, 0, -28, 49}, {0, 0, -37, 53},
    {10, 23, -12, 59}, {19, 41, -12, 103}, {0, 0, 0, -1},
};
static const DentRecord dent_04[] = { /* 0x80015D04 */
    {27, 0, 0, 39}, {32, 0, 0, 40}, {0, 0, 0, -1},
};
static const DentRecord dent_05[] = { /* 0x80015D1C */
    {0, 0, 18, 1}, {0, 0, 18, 2}, {0, 0, 18, 5}, {0, 0, 18, 6}, {0, 0, 18, 8}, {0, 0, 26, 12}, {0, 0, 29, 67}, {0, 0, 0, -1},
};
static const DentRecord dent_06[] = { /* 0x80015D5C */
    {0, 0, 18, 1}, {0, 0, 18, 2}, {0, 0, 18, 5}, {0, 0, 18, 6}, {0, 0, 18, 8}, {0, 0, 26, 12}, {0, 0, 29, 67},
    {15, 32, 10, 105}, {0, 0, 0, -1},
};
static const DentRecord dent_07[] = { /* 0x80015DA4 */
    {0, 0, 17, 0}, {0, 0, 18, 3}, {0, 0, 18, 4}, {0, 0, 17, 7}, {0, 0, 17, 17}, {0, 0, 31, 19},
    {0, 0, 31, 66}, {0, 0, 0, -1},
};
static const DentRecord dent_08[] = { /* 0x80015DE4 */
    {0, 0, 17, 0}, {0, 0, 18, 3}, {0, 0, 18, 4}, {0, 0, 17, 7}, {0, 0, 17, 17}, {0, 0, 31, 19},
    {0, 0, 31, 66}, {-13, 34, 7, 104}, {0, 0, 0, -1},
};
static const DentRecord dent_09[] = { /* 0x80015E2C */
    {-28, 0, 0, 38}, {-35, 0, 0, 41}, {0, 0, 0, -1},
};
static const DentRecord dent_10[] = { /* 0x80015E44 */
    {0, -28, 0, 106}, {0, 0, 0, -1},
};
static const DentRecord dent_11[] = { /* 0x80015E54 */
    {0, 0, -22, 6}, {0, 0, -30, 8}, {0, 0, -24, 18}, {0, 0, -34, 19}, {0, 0, 0, -1},
};
static const DentRecord dent_12[] = { /* 0x80015E7C */
    {0, 0, -22, 6}, {0, 0, -30, 8}, {0, 0, -24, 18}, {0, 0, -34, 19}, {-19, 40, -18, 52}, {0, 0, 0, -1},
};
static const DentRecord dent_13[] = { /* 0x80015EAC */
    {0, 0, -28, 1}, {0, 0, -35, 5}, {0, 0, -28, 20}, {0, 0, -36, 21}, {0, 0, 0, -1},
};
static const DentRecord dent_14[] = { /* 0x80015ED4 */
    {0, 0, -28, 1}, {0, 0, -35, 5}, {0, 0, -28, 20}, {0, 0, -36, 21}, {20, 40, -12, 53}, {0, 0, 0, -1},
};
static const DentRecord dent_15[] = { /* 0x80015F04 */
    {26, 0, 0, 43}, {0, 0, 0, -1},
};
static const DentRecord dent_16[] = { /* 0x80015F14 */
    {0, 0, 19, 0}, {0, 0, 27, 4}, {0, 0, 29, 33}, {0, 0, 19, 45}, {0, 0, 18, 50}, {0, 0, 18, 51},
    {0, 0, 19, 101}, {0, 0, 0, -1},
};
static const DentRecord dent_17[] = { /* 0x80015F54 */
    {0, 0, 19, 0}, {0, 0, 27, 4}, {0, 0, 29, 33}, {0, 0, 19, 45}, {0, 0, 18, 50}, {0, 0, 18, 51},
    {16, 32, 10, 57}, {0, 0, 19, 101}, {0, 0, 0, -1},
};
static const DentRecord dent_18[] = { /* 0x80015F9C */
    {0, 0, 18, 9}, {0, 0, 31, 11}, {0, 0, 31, 32}, {0, 0, 17, 44}, {0, 0, 18, 48}, {0, 0, 17, 49},
    {0, 0, 18, 100}, {0, 0, 0, -1},
};
static const DentRecord dent_19[] = { /* 0x80015FDC */
    {0, 0, 18, 9}, {0, 0, 31, 11}, {0, 0, 31, 32}, {0, 0, 17, 44}, {0, 0, 18, 48}, {0, 0, 17, 49},
    {-14, 34, 7, 56}, {0, 0, 18, 100}, {0, 0, 0, -1},
};
static const DentRecord dent_20[] = { /* 0x80016024 */
    {0, 0, -22, 6}, {0, 0, -30, 8}, {0, 0, -24, 18}, {0, 0, -34, 19}, {0, 0, 0, -1},
};
static const DentRecord dent_21[] = { /* 0x8001604C */
    {0, 0, -22, 6}, {0, 0, -30, 8}, {0, 0, -24, 18}, {0, 0, -34, 19}, {0, 0, 0, -1},
};

/* Per damage zone (0..10): the detailed model's vertices, then the near model's (0x80015BAC). */
static const DentRecord *const dent_areas[22] = {
    dent_00, dent_01, dent_02, dent_03, dent_04, dent_05, dent_06, dent_07, dent_08, dent_09, dent_10,
    dent_11, dent_12, dent_13, dent_14, dent_15, dent_16, dent_17, dent_18, dent_19, dent_20, dent_21,
};

/* The damage zones each dent reshapes (0x8006DC94). */
static const int8_t dent_groups[7][2] = {
    {0, 2}, {1, 3}, {5, 7}, {6, 8}, {9, -1}, {4, -1}, {10, -1},
};

void InitialiseDenting(void)
{
    const Mesh *mid = (const Mesh *)level_data_section(SECTION_CAR_NEAR);
    const Mesh *hi = (const Mesh *)level_data_section(SECTION_PLAYER_CAR);
    memcpy(mid_car_base, mesh_section(mid, mid->vertices), sizeof(mid_car_base));
    memcpy(hi_car_base, mesh_section(hi, hi->vertices), sizeof(hi_car_base));
    for (int car = 0; car < num_cars; car++) {
        car_blocks[car].damage[0] = 0;
        memset(dent_levels[car], 0, 11);
    }
}

/* A region's damage stage went up: its texture follows. */
static void dent_texture(int car, int parts, int region, int level)
{
    uint8_t *have = &dent_levels[car][region];
    if (*have >= level)
        return;
    if (parts == 0)
        TextureDentHiCar(car, region, *have, level);
    else
        TextureDentMidCar(car, region, *have, level);
    *have = (uint8_t)level;
}

static int32_t max2(int32_t a, int32_t b)
{
    return a < b ? b : a;
}

/* A hit on one side of a car (0 front, 1 back, 2 right, 3 left, 4 top): the textures of every region
 * catch up with the damage, then the zones of that side are reshaped from the undented model by their
 * damage. `parts` 0 for the player's detailed model, 0xB for the near model. The vertices are those
 * of `model`'s mesh: for the other cars the near model's shared mesh, which points at the vertices of
 * the car drawn last (Draw_Car), as on the PSX. */
void Dent_Car_Zone(Object *model, CarBlock *cb, int zone, int parts, int car)
{
    const SVECTOR *base = parts == 0 ? hi_car_base : mid_car_base;
    int group;
    switch (zone) {
    case 0: group = cb->bonnet_lost == 0; break;
    case 1: group = cb->boot_lost != 0 ? 2 : 3; break;
    case 2: group = 4; break;
    case 3: group = 5; break;
    case 4: group = 6; break;
    default: return;
    }
    const int32_t *d = cb->damage;
    dent_texture(car, parts, 0, (int16_t)max2(d[0], d[2]) / 0x334);
    dent_texture(car, parts, 1, d[2] / 0x334);
    dent_texture(car, parts, 3, d[5] / 0x334);
    dent_texture(car, parts, 4, (int16_t)max2(d[5], d[7]) / 0x334);
    dent_texture(car, parts, 5, d[5] / 0x334);
    dent_texture(car, parts, 7, d[0] / 0x334);
    dent_texture(car, parts, 2, d[4] / 0x556);
    dent_texture(car, parts, 6, d[9] / 0x556);
    dent_texture(car, parts, 8, cb->bonnet_lost == 0 ? (int16_t)max2(d[0], d[2]) / 0x556 : 3);
    dent_texture(car, parts, 10, cb->boot_lost == 0 ? (int16_t)max2(d[7], d[5]) / 0x556 : 3);
    dent_texture(car, parts, 9, d[10] / 0x556);
    SVECTOR *v = (SVECTOR *)mesh_section(model->mesh, model->mesh->vertices);
    for (int k = 0; k < 2 && dent_groups[group][k] != -1; k++) {
        for (const DentRecord *r = dent_areas[parts + dent_groups[group][k]]; r->vertex != -1; r++) {
            v[r->vertex].vx = base[r->vertex].vx;
            v[r->vertex].vy = base[r->vertex].vy;
            v[r->vertex].vz = base[r->vertex].vz;
        }
    }
    for (int k = 0; k < 2 && dent_groups[group][k] != -1; k++) {
        int z = dent_groups[group][k];
        for (const DentRecord *r = dent_areas[parts + z]; r->vertex != -1; r++) {
            v[r->vertex].vx = (int16_t)(v[r->vertex].vx + (r->dx * d[z] >> 12));
            v[r->vertex].vy = (int16_t)(v[r->vertex].vy + (r->dy * d[z] >> 12));
            v[r->vertex].vz = (int16_t)(v[r->vertex].vz + (r->dz * d[z] >> 12));
        }
    }
}
