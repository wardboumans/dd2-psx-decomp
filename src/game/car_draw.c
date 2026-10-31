#include "game/car_draw.h"

#include "game/car.h"
#include "game/car_motion.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/particles.h"
#include "game/play_game.h"
#include "game/scene.h"
#include "game/wheels.h"
#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"

#include <stdio.h>
#include <string.h>

#define NEAR_Z 5000          /* closer than this the near model is drawn, with wheels and shadow */
#define CRITICAL_DAMAGE 0xe64
#define HALF_DAMAGE 0x732

/* ---- the damage indicator: a CLUT of the DAMAGE sprite whose entries fade with each zone's damage */

static int16_t damage_ramp1[2][4];        /* 0x80089484: second colours minus first */
static int16_t damage_ramp2[2][4];        /* 0x8009EA2C: third minus second */
static RECT damage_rect;                  /* 0x80079B84 */
static uint16_t damage_clut[16];          /* 0x8009561C */
static int32_t shown_damage[6];           /* 0x80079C4C */
static int32_t flash_timer[6];            /* 0x80079C64 */
static int32_t flash_period[6];           /* 0x80079C7C */

/* Two colours per zone shade: green, then yellow, then red, flashing red/white at the end. */
static const int16_t damage_colours[4][2][4] = { /* 0x8006EA98, r g b in 5:5:5 bit positions */
    {{0, 960, 0, 0}, {0, 480, 0, 0}},
    {{30, 960, 0, 0}, {15, 480, 0, 0}},
    {{30, 0, 0, 0}, {15, 0, 0, 0}},
    {{31, 512, 16384, 0}, {24, 256, 8192, 0}},
};

void Init_Damage_Indicator(void)
{
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < 3; k++) {
            damage_ramp1[i][k] = (int16_t)(damage_colours[1][i][k] - damage_colours[0][i][k]);
            damage_ramp2[i][k] = (int16_t)(damage_colours[2][i][k] - damage_colours[1][i][k]);
        }
    }
    Sprite s;
    Setup_Sprite_Quad(NULL, "DAMAGE", &s);
    damage_rect.w = 16;
    damage_rect.h = 1;
    damage_rect.x = s.clut_x;
    damage_rect.y = s.clut_y;
    StoreImage(&damage_rect, (u_long *)damage_clut);
}

/* A CLUT entry's top bit: semi-transparency, which also keeps black from being transparent. */
#define STP_BIT 0x8000

static uint16_t to_555(const int16_t c[4])
{
    return (uint16_t)((c[0] & 0x1f) | (c[1] & 0x3e0) | (c[2] & 0x7c00) | STP_BIT);
}

/* Each of six damage zones shades two CLUT entries; only changes are uploaded. */
void Update_Damage_Indicator(int car)
{
    static const int zones[6] = {0, 2, 9, 4, 7, 5}; /* CarBlock.damage entries shown */
    const CarBlock *cb = &car_blocks[car];
    int changed[6] = {0, 0, 0, 0, 0, 0};
    int any = 0;
    for (int k = 0; k < 6; k++) {
        int32_t d = cb->damage[zones[k]];
        if (d != shown_damage[k] || d >= CRITICAL_DAMAGE) {
            any = 1;
            changed[k] = 1;
            shown_damage[k] = d;
        }
    }
    for (int k = 0; k < 6; k++) {
        if (!changed[k])
            continue;
        uint16_t *entry = &damage_clut[1 + 2 * k];
        int32_t d = shown_damage[k];
        if (d < CRITICAL_DAMAGE) {
            int16_t c[2][4];
            const int16_t (*ramp)[4];
            int32_t t;
            if (d < HALF_DAMAGE) {
                memcpy(c, damage_colours[0], sizeof(c));
                ramp = damage_ramp1;
                t = (d << 12) / HALF_DAMAGE;
            } else {
                memcpy(c, damage_colours[1], sizeof(c));
                ramp = damage_ramp2;
                t = ((d - HALF_DAMAGE) << 12) / HALF_DAMAGE;
            }
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 3; j++)
                    c[i][j] = (int16_t)(c[i][j] + (ramp[i][j] * t >> 12));
                entry[i] = to_555(c[i]);
            }
        } else if (d == MAX_DAMAGE) {
            entry[0] = STP_BIT;
            entry[1] = STP_BIT;
        } else {
            int32_t timer = flash_timer[k];
            if (timer == 0) {
                timer = 0x24 - (d - CRITICAL_DAMAGE) / 0xc;
                flash_timer[k] = timer;
                flash_period[k] = timer;
            }
            flash_timer[k] = timer - 1;
            const int16_t (*c)[4] = timer - 1 < flash_period[k] / 2 ? damage_colours[2] : damage_colours[3];
            entry[0] = to_555(c[0]);
            entry[1] = to_555(c[1]);
        }
    }
    if (any)
        LoadImage(&damage_rect, (u_long *)damage_clut);
}

/* ---- brake lights: one CLUT entry pair of each car's paint, lit while it brakes */

typedef struct {
    int16_t x, y;
    uint16_t colour[4];
} BrakeLight;

static BrakeLight brake_lights[MAX_CARS]; /* 0x800889C8 */
static const uint8_t car_textures[20] = {1, 0, 7, 13, 17, 35, 37, 40, 42, 47, 50, 52, 53, 64, 66, 69, 77, 82, 88, 99};

void Init_Brake_Lights(void)
{
    const SpriteInfo *info = NULL;
    char name[24];
    for (int i = 0; i < num_cars; i++) {
        const char *n = name;
        if (i == 18) {
            n = "BUMP88A";
        } else if (i != current_player_car) {
            sprintf(name, "CLUT%02dA", car_textures[i]);
        } else if (car_blocks[i].car_type == 1) {
            sprintf(name, "CLT%02dA2", car_textures[i]);
        } else if (car_blocks[i].car_type == 2) {
            sprintf(name, "CLT%02dA3", car_textures[i]);
        } else if (car_blocks[i].car_type == 0) {
            sprintf(name, "CLUT%02dA", car_textures[i]);
        } else {
            n = NULL; /* the original keeps the previous car's CLUT */
        }
        if (n != NULL)
            info = Search_For_Sprite(n);
        RECT r = {(int16_t)(info->clut_x + 0x60), info->clut_y, 4, 1};
        StoreImage(&r, (u_long *)brake_lights[i].colour);
        DrawSync(0);
        brake_lights[i].x = r.x;
        brake_lights[i].y = r.y;
    }
}

void Update_Brake_Lights(int car)
{
    const CarBlock *cb = &car_blocks[car];
    RECT r = {brake_lights[car].x, brake_lights[car].y, 4, 1};
    int level = 10;
    if (((cb->throttle < 0 && cb->forward_speed > 0) || (cb->throttle > 0 && cb->forward_speed < 0)) && cb->speed > 0x1e)
        level = 0xff;
    uint16_t colour = (uint16_t)((level >> 3) | STP_BIT);
    brake_lights[car].colour[0] = colour;
    brake_lights[car].colour[2] = colour;
    LoadImage(&r, (u_long *)brake_lights[car].colour);
}

/* ---- smoke, fire and sparks from a damaged car */

static const SVECTOR wheel_spark_points[4] = { /* 0x8006EAF8 */
    {-180, -170, -400, 0}, {180, -170, -400, 0}, {-180, -170, 400, 0}, {180, -170, 400, 0},
};
static const int wheel_spark_wheels[4] = {3, 1, 2, 0}; /* 0x8006EB48 */
static const SVECTOR bonnet_point = {0, 0, 300, 0};     /* 0x800791D0 */

static void car_point(int car, const SVECTOR *local, VECTOR *out)
{
    const CarState *cs = &car_states[car];
    gte_SetRotMatrix(&cs->matrix);
    gte_ldv0(local);
    gte_rtv0();
    gte_stlvnl(out);
    out->vx += cs->position[0];
    out->vy += cs->position[1];
    out->vz += cs->position[2];
}

static void fire(const VECTOR *pos)
{
    int32_t p = projection_distance;
    ParticleParams pp = {p << 7, (p * 3) << 7, ((p << 7) - ((p * 3) << 7)) / 16, 0,
                         0x100000, 0x100000, 0x100000, 0x40000, 0x40000, 0x40000,
                         (int32_t)0xffff4000, (int32_t)0xffff4000, (int32_t)0xffff4000, 2};
    Fire(pos, &particles[Pop_Particle_Slot()], &pp);
}

void Bonnet_Smoke(int car)
{
    CarBlock *cb = &car_blocks[car];
    int32_t p = projection_distance;
    VECTOR pos;
    if (cb->speed > 0x32 && cb->motion_state == MOTION_DRIVING) {
        for (int i = 0; i < 4; i++) {
            int32_t lost = cb->wheel_lost[i];
            if (lost != 1 || quad_surface(wheel_fd[car][wheel_spark_wheels[i]].plane) != SURFACE_ROAD)
                continue;
            car_point(car, &wheel_spark_points[i], &pos);
            Particle *part = &particles[Pop_Particle_Slot()];
            ParticleParams pp = {p * 25, p * 50, (p * 25 - p * 50) / 128, 0,
                                 0xff000, 0xff000, 0xff000, 0xe000, 0xe000, 0xe000,
                                 (int32_t)0xfffe7e67, (int32_t)0xfffe7e67, (int32_t)0xfffe7e67, lost};
            Sparks(&pos, cb->velocity, part, &pp);
        }
    }
    if (cb->part_loss_timer > 0) {
        car_point(car, &bonnet_point, &pos);
        if (cb->part_loss_timer >= 93 && cb->part_loss_timer <= 99)
            for (int i = 0; i < 3; i++)
                fire(&pos);
    }
    if (cb->wrecked == 1) {
        if ((frame_count & 3) == 0) {
            car_point(car, &bonnet_point, &pos);
            ParticleParams pp = {(p * 3) << 7, p << 7, ((p * 3) << 7) - (p << 7), 0,
                                 0x40000, 0x40000, 0x40000, 0x20000, 0x20000, 0x20000, -0x1000, -0x1000, -0x1000, 2};
            pp.grow /= 40;
            Steam(&pos, &particles[Pop_Particle_Slot()], &pp);
            if (cb->speed < 0x28 && cb->fire_timer > 0 && cb->bonnet_lost == 1) {
                fire(&pos);
                cb->fire_timer--;
            }
        }
    } else if (cb->heavy_damage == 1 && (frame_count & 1) == 0) {
        car_point(car, &bonnet_point, &pos);
        ParticleParams pp = {(p * 3) << 7, p << 7, (((p * 3) << 7) - (p << 7)) / 32, 0,
                             0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0, 0, 0, cb->heavy_damage};
        Steam(&pos, &particles[Pop_Particle_Slot()], &pp);
    }
}

/* ---- lighting by the track: street lamps (level 6), tunnels (levels 2 and 4) */

static const VECTOR street_lamps[17] = { /* 0x8006EB58 */
    {-38978, 936, 7074, 0}, {-36877, 936, 9598, 0}, {-37128, 936, 12969, 0}, {-34673, 936, 15572, 0},
    {-34340, 936, 19193, 0}, {-30740, 936, 22303, 0}, {-30336, 936, 24878, 0}, {-27060, 936, 26445, 0},
    {-25467, 936, 29566, 0}, {-22147, 1355, 30563, 0}, {-19890, 1355, 33431, 0}, {-15912, 1355, 33233, 0},
    {-13153, 1355, 35589, 0}, {-9601, 1355, 34378, 0}, {-6746, 1355, 36568, 0}, {-3409, 1355, 34768, 0},
    {-477, 1355, 37076, 0},
};

#define BRIGHTNESS_DARK 0x18
#define BRIGHTNESS_DAY 0x40
#define LAMP_REACH 0xc00
#define LAMP_FULL 0x300

/* The three lamps nearest the car's strip light it, each by its distance. */
static int strip_in(unsigned s, unsigned first, unsigned last)
{
    return s >= first && s <= last;
}

void Street_Lighting(int car)
{
    if (current_level != LEVEL_LIBERTY_CITY)
        return;
    unsigned s = (uint16_t)car_fd[car].strip_number;
    unsigned set = strip_in(s, 62, 64);
    static const struct { unsigned first, count, set; } ranges[] = {
        {65, 4, 2}, {69, 3, 3}, {72, 3, 4}, {75, 3, 5}, {78, 3, 6}, {81, 3, 7}, {84, 2, 8},
        {86, 3, 9}, {89, 3, 10}, {92, 3, 11}, {95, 3, 12}, {98, 3, 13}, {101, 8, 14},
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++)
        if (s - ranges[i].first < ranges[i].count)
            set = ranges[i].set;
    CarBlock *cb = &car_blocks[car];
    MATRIX *colour = &car_colour_matrix[car];
    if (!strip_in(s, 55, 108)) {
        colour->m[0][0] = 0;
        colour->m[1][0] = 0;
        colour->m[2][0] = 0;
        car_light_matrix = scene_light_matrix;
        cb->brightness = BRIGHTNESS_DARK;
        return;
    }
    const CarState *cs = &car_states[car];
    for (int i = 0; i < 3; i++) {
        const VECTOR *lamp = &street_lamps[set + i];
        int32_t dx = lamp->vx - cs->position[0];
        int32_t dy = lamp->vy - cs->position[1];
        int32_t dz = lamp->vz - cs->position[2];
        int32_t len = SquareRoot0(dx * dx + dy * dy + dz * dz) + 2;
        car_light_matrix.m[i][0] = (int16_t)((dx << 12) / len);
        car_light_matrix.m[i][1] = (int16_t)((dy << 12) / len);
        car_light_matrix.m[i][2] = (int16_t)((dz << 12) / len);
        if (len < LAMP_REACH) {
            colour->m[0][i] = (int16_t)(ONE - (len - LAMP_FULL) * ONE / (LAMP_REACH - LAMP_FULL));
            colour->m[1][i] = (int16_t)(ONE - (len - LAMP_FULL) * ONE / (LAMP_REACH - LAMP_FULL));
            colour->m[2][i] = (int16_t)(0x830 - (len - LAMP_FULL) * 0x830 / (LAMP_REACH - LAMP_FULL));
            int32_t avg = (colour->m[0][0] + colour->m[0][1] + colour->m[0][2]) / 3;
            cb->brightness = avg < 0x101 ? BRIGHTNESS_DARK : (avg - 0x100) * 0x28 / (LAMP_REACH - LAMP_FULL) + BRIGHTNESS_DARK;
        } else {
            colour->m[0][i] = 0;
            colour->m[1][i] = 0;
            colour->m[2][i] = 0;
        }
    }
}

/* Tunnel mouths: pairs of points across each mouth (index, index + 1). */
static const VECTOR tunnel_points[29] = { /* 0x8001016C */
    {0, 0, 0, 0}, {92816, 2493, -7812, 0}, {89499, 2493, -5579, 0}, {92985, 2871, 485, 0},
    {96541, 2871, -1342, 0}, {11334, 12734, 49454, 0}, {14742, 12734, 49538, 0}, {14646, 14096, 30110, 0},
    {12647, 14096, 30096, 0}, {51792, 399, 47917, 0}, {52657, 399, 44012, 0}, {-42737, -728, 13757, 0},
    {-43687, -728, 15516, 0}, {53523, 340, 42276, 0}, {55474, 340, 39997, 0}, {37244, -443, 24389, 0},
    {35293, -443, 26668, 0}, {-26762, -502, 10478, 0}, {-26344, -502, 7508, 0}, {-39680, -523, 7699, 0},
    {-39620, -523, 10697, 0}, {-63631, -2984, -7540, 0}, {-60947, -2984, -5342, 0}, {-53911, -3493, -8442, 0},
    {-54862, -3493, -12326, 0}, {-41272, -4932, -23451, 0}, {-37902, -4932, -21304, 0}, {-7620, -366, -22426, 0},
    {-6001, -366, -24951, 0},
};

/* How far the car is past the mouth's line, in the line's normal direction. */
static int32_t past_mouth(int car, int mouth)
{
    const VECTOR *a = &tunnel_points[mouth];
    const VECTOR *b = &tunnel_points[mouth + 1];
    int32_t nx = (b->vz - a->vz) / 16;
    int32_t nz = -((b->vx - a->vx) / 16);
    int32_t len = (nx * nx + nz * nz) / 64;
    return ((nx * (car_states[car].position[0] - a->vx) + nz * (car_states[car].position[2] - a->vz)) * 0x40) / len;
}

static void set_colour_column(int car, int16_t r, int16_t g, int16_t b)
{
    car_colour_matrix[car].m[0][0] = r;
    car_colour_matrix[car].m[1][0] = g;
    car_colour_matrix[car].m[2][0] = b;
}

/* Brightness from the light's red, scaled to the level's full light. The original reads the colour
 * table here by level, not level - 1. */
static void set_brightness(int car, int dark, int32_t red)
{
    if (dark)
        car_blocks[car].brightness = BRIGHTNESS_DARK;
    else if (red < light_colours[current_level][0])
        car_blocks[car].brightness = (red - 0x100) * 0x28 / (light_colours[current_level][0] - 0x100) + BRIGHTNESS_DARK;
}

/* In and near tunnels the light fades with the car's distance from the mouth. */
void Car_Segment_Lighting(int car)
{
    unsigned mouth = 0;
    int32_t inside = 0, outside = 0;
    int32_t red = 0;
    int dark;
    if (current_level == LEVEL_CHALK_CANYON) {
        const int16_t *light = light_colours[2];
        unsigned s = (uint16_t)car_fd[car].strip_number;
        if (!strip_in(s, 133, 143) && !strip_in(s, 271, 292)) {
            car_blocks[car].brightness = BRIGHTNESS_DAY;
            set_colour_column(car, light[0], light[1], light[2]);
        } else {
            if (s == 133) {
                outside = -0x4000;
                mouth = 1;
            }
            if (s == 143) {
                mouth = 3;
                inside = -0x8000;
                outside = -0xcb84;
            }
            if (s == 271) {
                mouth = 5;
                inside = 0;
                outside = -0x5800;
            }
            if (s == 292) {
                mouth = 7;
                inside = -0x8000;
                outside = -0x10000;
            }
            if (mouth == 0) {
                set_colour_column(car, 0, 0, 0);
                dark = 1;
            } else {
                int32_t t = past_mouth(car, (int)mouth);
                if (t < outside) {
                    set_colour_column(car, light[0], light[1], light[2]);
                    dark = 1;
                } else if (inside < t) {
                    set_colour_column(car, 0, 0, 0);
                    dark = 1;
                } else {
                    int32_t f = inside - t, span = inside - outside;
                    set_colour_column(car, (int16_t)(f * light[0] / span), (int16_t)(f * light[1] / span),
                                      (int16_t)(f * light[2] / span));
                    red = car_colour_matrix[car].m[0][0];
                    dark = red < 0x100;
                }
            }
            set_brightness(car, dark, red);
        }
    }
    if (current_level != LEVEL_ULTIMATE_DESTRUCTION)
        return;
    const int16_t *light = light_colours[4];
    unsigned s = (uint16_t)car_fd[car].strip_number;
    if (!(strip_in(s, 361, 489) || strip_in(s, 58, 83) || strip_in(s, 151, 165) || strip_in(s, 198, 208) ||
          strip_in(s, 503, 545))) {
        car_blocks[car].brightness = BRIGHTNESS_DAY;
        set_colour_column(car, light[0], light[0], light[0]);
        return;
    }
    static const struct { int16_t strip; uint8_t mouth; int32_t inside, outside; } mouths[] = {
        {0x169, 9, 0, -0x4000}, {0x1e9, 0xb, 0, -0x6c00}, {0x3a, 0xd, 0, -0x5400}, {0x53, 0xf, 0, -0x5400},
        {0x97, 0x11, 0x5400, 0}, {0xa5, 0x13, -0x5400, -43000}, {0xc6, 0x15, 0, -0x5400}, {0xd0, 0x17, 0, -0x4000},
        {0x1f7, 0x19, 0, -0x4000}, {0x221, 0x1b, 0, -0x5400},
    };
    for (size_t i = 0; i < sizeof(mouths) / sizeof(mouths[0]); i++) {
        if ((int16_t)s == mouths[i].strip) {
            mouth = mouths[i].mouth;
            inside = mouths[i].inside;
            outside = mouths[i].outside;
        }
    }
    if (mouth == 0) {
        set_colour_column(car, (int16_t)red, (int16_t)red, (int16_t)red);
    } else {
        int32_t t = past_mouth(car, (int)mouth);
        if (outside < t) {
            if (t < inside) {
                int32_t f = inside - t, span = inside - outside;
                set_colour_column(car, (int16_t)(f * light[0] / span), (int16_t)(f * light[1] / span),
                                  (int16_t)(f * light[2] / span));
                red = car_colour_matrix[car].m[0][0];
            } else {
                set_colour_column(car, 0, 0, 0);
            }
        } else {
            set_colour_column(car, light[0], light[1], light[2]);
        }
    }
    set_brightness(car, red < 0x100, red);
}

/* ---- drawing */

static void light_car(int car)
{
    MATRIX m;
    Street_Lighting(car);
    Car_Segment_Lighting(car);
    MulMatrix0(&car_light_matrix, &car_states[car].matrix, &m);
    SetLightMatrix(&m);
    SetColorMatrix(&car_colour_matrix[car]);
    Set_Object_View(&car_states[car].matrix, car_states[car].position);
}

/* A car other than the player's: the distant model, or close up the near one with its own dents,
 * wheels and shadow. */
void Draw_Car(int car)
{
    CarState *cs = &car_states[car];
    light_car(car);
    int near = object_matrix.t[2] < NEAR_Z;
    Mesh *shared = (Mesh *)level_data_section(16);
    shared->vertices = mesh_offset_of(shared, car_vertices[car]);
    cs->drawn = VVDraw_Object(&car_objects[car][near ? 1 : 0]);
    if (cs->drawn == 0)
        return;
    if (near) {
        Update_Brake_Lights(car);
        cs->wheel[3].angle[1] = (int16_t)-car_blocks[car].steering;
        TransformEnemyWheels(car);
        Draw_Car_Shadow(car);
    }
    Bonnet_Smoke(car);
}

/* The player's car seen from the chase camera: always the detailed model. */
void Draw_Player_Car_Near(int car)
{
    CarState *cs = &car_states[car];
    light_car(car);
    cs->drawn = VVDraw_Object(&car_objects[car][0]);
    if (cs->drawn == 0)
        return;
    Update_Brake_Lights(car);
    cs->wheel[3].angle[1] = (int16_t)-car_blocks[car].steering;
    TransformWheels_(car, 3, 2);
    TransformWheels_(car, 0, 1);
    Draw_Car_Shadow(car);
    Bonnet_Smoke(car);
}
