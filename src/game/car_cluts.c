#include "game/car_cluts.h"

#include "game/car.h"
#include "game/debris.h"
#include "game/frontend.h"
#include "graphics/draw_face.h"
#include "graphics/object.h"
#include "psx/libgpu.h"

#include <stdio.h>

CarPaint car_paint[20];
PartTextures part_textures;
uint8_t door_state[20][2];

static const int8_t no_faces[] = {-1};

const uint8_t car_textures[20] = {1, 0, 7, 13, 17, 35, 37, 40, 42, 47, 50, 52, 53, 64, 66, 69, 77, 82, 88, 99};
#define NO_PAINT_CAR 18 /* the car whose paint the sprites don't have; it keeps the model's */

static const char *const player_paint[5] = {"A%d", "B%d", "C%d", "D%d", "E%d"}; /* 0x80079560 */

static uint16_t sprite_clut(const char *name)
{
    Sprite s;
    Setup_Sprite_Quad(NULL, name, &s);
    return GetClut(s.clut_x, s.clut_y);
}

static void load_part(PartTexture *part, const char *name, int with_clut)
{
    Sprite s;
    Setup_Sprite_Quad(NULL, name, &s);
    part->tpage = s.tpage;
    part->u = s.u;
    part->v = s.v;
    if (with_clut)
        part->clut = s.clut;
}

/* Each car's body CLUTs (the player's in their car class's colours) and door sprites; then the body
 * parts' damage stages. */
void Init_Car_Cluts(void)
{
    char name[16], cls[5][8];
    for (int i = 0; i < 20; i++) {
        CarPaint *paint = &car_paint[i];
        unsigned tex = car_textures[i];
        if (i != NO_PAINT_CAR) {
            if (i == 0 && race_car != 0) {
                for (int k = 0; k < 5; k++)
                    sprintf(cls[k], player_paint[k], race_car + 1);
                static const int order[5] = {1, 0, 3, 2, 4}; /* B, A, D, C, E */
                for (int k = 0; k < 5; k++) {
                    sprintf(name, "CLT%02d%s", car_textures[0], cls[order[k]]);
                    paint->clut[k] = sprite_clut(name);
                }
                sprintf(name, "P1D1T%d", race_car + 1);
                paint->clut[7] = sprite_clut(name);
            } else {
                static const char parts[5] = {'B', 'A', 'D', 'C', 'E'};
                for (int k = 0; k < 5; k++) {
                    sprintf(name, "CLUT%02u%c", tex, parts[k]);
                    paint->clut[k] = sprite_clut(name);
                }
            }
            sprintf(name, "SMCL%02uB", tex);
            paint->clut[5] = sprite_clut(name);
            sprintf(name, "SMCL%02uA", tex);
            paint->clut[6] = sprite_clut(name);
        }
        sprintf(name, "DR%02uA", tex);
        Setup_Sprite_Quad(NULL, name, &paint->door[0]);
        if (i != 0 || race_car == 0)
            paint->clut[7] = GetClut(paint->door[0].clut_x, paint->door[0].clut_y);
        sprintf(name, "DR%02uB", tex);
        Setup_Sprite_Quad(NULL, name, &paint->door[1]);
        sprintf(name, "DR%02uC", tex);
        Setup_Sprite_Quad(NULL, name, &paint->door[2]);
    }
    static const char *const back_window[5] = {"BKWN88A", "BKWN88B", "BKWN88C", "BKWN88D", "BKWN88E"};
    static const char *const bonnet[4] = {"BON88A", "BON88B", "BON88C", "ENGINE"};
    static const char *const bumper[5] = {"BUMP88A", "BUMP88B", "BUMP88C", "BUMP88D", "BUMP88E"};
    static const char *const boot[4] = {"BOOT88A", "BOOT88B", "BOOT88C", "BOOT"};
    static const char *const front[5] = {"FRNT88A", "FRNT88B", "FRNT88C", "FRNT88D", "FRNT88E"};
    static const char *const front_window[5] = {"FRWN88A", "FRWN88B", "FRWN88C", "FRWN88D", "FRWN88E"};
    static const char *const roof[3] = {"ROOF88A", "ROOF88B", "ROOF88C"};
    for (int k = 0; k < 5; k++)
        load_part(&part_textures.back_window[k], back_window[k], 0);
    for (int k = 0; k < 4; k++)
        load_part(&part_textures.bonnet[k], bonnet[k], 1);
    for (int k = 0; k < 5; k++)
        load_part(&part_textures.bumper[k], bumper[k], 0);
    for (int k = 0; k < 4; k++)
        load_part(&part_textures.boot[k], boot[k], 1);
    for (int k = 0; k < 5; k++)
        load_part(&part_textures.front[k], front[k], 0);
    for (int k = 0; k < 5; k++)
        load_part(&part_textures.front_window[k], front_window[k], 0);
    for (int k = 0; k < 3; k++)
        load_part(&part_textures.roof[k], roof[k], 0);
}

/* Walks a model's face runs alongside its packets in both buffers. */
typedef struct {
    const FaceRun *run;
    uint8_t *prim[2];
} RunCursor;

static RunCursor first_run(const Object *obj)
{
    RunCursor c;
    c.run = (const FaceRun *)mesh_section(obj->mesh, obj->mesh->faces);
    c.prim[0] = prim_buffer[0] + obj->prim_offset;
    c.prim[1] = prim_buffer[1] + obj->prim_offset;
    return c;
}

static void next_run(RunCursor *c)
{
    size_t bytes = (size_t)c->run->count * face_packet_size(c->run->kind);
    c->prim[0] += bytes;
    c->prim[1] += bytes;
    c->run = (const FaceRun *)((const uint8_t *)(c->run + 1) + c->run->count * c->run->record_size);
}

/* The lit textured runs: three-point (0x19, 0x1B) and four-point (0x1D, 0x1F). */
#define LIT_PICT3 0x19
#define LIT_PICT4 0x1d
#define LIT_FLAT3 0x01

static void shift_uv(uint8_t *u, uint8_t *v, const Sprite *from, const Sprite *to)
{
    *u = (uint8_t)(*u - from->u + to->u);
    *v = (uint8_t)(*v - from->v + to->v);
}

/* Moves the listed faces' texture from one sprite to another (same layout, other place in VRAM). */
static void Retexture_Tris(uint8_t *prim0, uint8_t *prim1, const int8_t *list, const Sprite *from, const Sprite *to)
{
    for (; *list != -1; list++) {
        for (int b = 0; b < 2; b++) {
            POLY_GT3 *p = (POLY_GT3 *)(b == 0 ? prim0 : prim1) + *list;
            p->tpage = to->tpage;
            shift_uv(&p->u0, &p->v0, from, to);
            shift_uv(&p->u1, &p->v1, from, to);
            shift_uv(&p->u2, &p->v2, from, to);
        }
    }
}

static void Retexture_Quads(uint8_t *prim0, uint8_t *prim1, const int8_t *list, const Sprite *from, const Sprite *to)
{
    for (; *list != -1; list++) {
        for (int b = 0; b < 2; b++) {
            POLY_GT4 *p = (POLY_GT4 *)(b == 0 ? prim0 : prim1) + *list;
            p->tpage = to->tpage;
            shift_uv(&p->u0, &p->v0, from, to);
            shift_uv(&p->u1, &p->v1, from, to);
            shift_uv(&p->u2, &p->v2, from, to);
            shift_uv(&p->u3, &p->v3, from, to);
        }
    }
}

typedef struct {
    int32_t clut;           /* index into the car's CarPaint.clut */
    const int8_t *faces;
} PartClut;

static void Set_Tri_Cluts(uint8_t *prim0, uint8_t *prim1, const PartClut *parts, int paint)
{
    for (; parts->clut != -1; parts++) {
        uint16_t clut = car_paint[paint].clut[parts->clut];
        for (const int8_t *f = parts->faces; *f != -1; f++) {
            ((POLY_GT3 *)prim0)[*f].clut = clut;
            ((POLY_GT3 *)prim1)[*f].clut = clut;
        }
    }
}

static void Set_Quad_Cluts_GT4(uint8_t *prim0, uint8_t *prim1, const PartClut *parts, int paint)
{
    for (; parts->clut != -1; parts++) {
        uint16_t clut = car_paint[paint].clut[parts->clut];
        for (const int8_t *f = parts->faces; *f != -1; f++) {
            ((POLY_GT4 *)prim0)[*f].clut = clut;
            ((POLY_GT4 *)prim1)[*f].clut = clut;
        }
    }
}

/* The car models' faces by part, as packet indices within the lit textured runs. */
static const int8_t list_70640[] = {1, 73, 54, 72, 55, 63, 62, 29, -1};
static const int8_t list_7064c[] = {6, 69, 68, 30, 70, 71, 31, 3, -1};
static const int8_t list_70658[] = {25, 26, 27, 28, 5, -1};
static const int8_t list_70660[] = {4, 22, 23, 24, 21, -1};
static const int8_t list_70668[] = {50, 40, 57, 39, 34, 59, 37, 61, 48, -1};
static const int8_t list_70674[] = {47, 60, 36, 58, 35, 33, 56, 38, 49, -1};
static const int8_t list_70680[] = {67, 52, 46, 32, 64, 42, 43, 53, -1};
static const int8_t list_7068c[] = {41, 45, 65, 44, 51, 66, -1};
static const int8_t list_70694[] = {8, 11, 14, 10, 2, 12, 13, 9, -1};
static const int8_t list_706a0[] = {0, 20, 17, 16, 7, 19, 18, 15, -1};
static const int8_t list_706ac[] = {12, 18, 19, 4, -1};
static const int8_t list_706b4[] = {1, 11, -1};
static const int8_t list_706b8[] = {2, 7, -1};
static const int8_t list_706bc[] = {13, 8, -1};
static const int8_t list_706c0[] = {0, 26, 15, 14, 25, 22, 21, -1};
static const int8_t list_706c8[] = {16, 3, 17, 20, 27, -1};
static const int8_t list_706d0[] = {24, 6, 9, -1};
static const int8_t list_706d4[] = {23, 5, 10, -1};
static const int8_t list_706d8[] = {23, 24, -1};
static const int8_t list_706dc[] = {7, 11, 8, 9, 10, 2, -1};
static const int8_t list_706e4[] = {15, 4, -1};
static const int8_t list_706e8[] = {18, 3, -1};
static const int8_t list_706ec[] = {0, 1, -1};
static const int8_t list_706f0[] = {5, 13, 12, -1};
static const int8_t list_706f4[] = {6, -1};
static const int8_t list_706f8[] = {21, -1};
static const int8_t list_706fc[] = {20, -1};
static const int8_t list_70700[] = {14, 22, -1};
static const int8_t list_70704[] = {19, 17, 16, -1};
static const int8_t list_70708[] = {3, -1};
static const int8_t list_7070c[] = {5, 18, -1};
static const int8_t list_70710[] = {21, 6, -1};
static const int8_t list_70714[] = {2, -1};
static const int8_t list_70718[] = {10, -1};
static const int8_t list_7071c[] = {9, 1, -1};
static const int8_t list_70720[] = {0, 8, 7, -1};
static const int8_t list_70724[] = {12, -1};
static const int8_t list_70728[] = {4, -1};
static const int8_t list_7072c[] = {22, 23, -1};
static const int8_t list_70730[] = {24, -1};
static const int8_t list_70734[] = {25, 19, 16, -1};
static const int8_t list_70738[] = {26, 20, 17, -1};
static const int8_t list_7073c[] = {11, 13, -1};
static const int8_t list_70740[] = {14, 15, -1};
static const int8_t list_70744[] = {2, -1};
static const int8_t list_70748[] = {5, -1};
static const int8_t list_7074c[] = {3, -1};
static const int8_t list_70750[] = {1, -1};
static const int8_t list_70754[] = {6, -1};
static const int8_t list_70758[] = {10, -1};
static const int8_t list_7075c[] = {7, -1};
static const int8_t list_70760[] = {14, 12, 0, 9, -1};
static const int8_t list_70768[] = {13, 11, 4, 8, -1};
static const int8_t list_70984[] = {0, 1, 2, 3, -1};
static const int8_t list_7098c[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1};
static const int8_t list_709c0[] = {0, 1, 2, 3, 4, 5, -1};
static const PartClut parts_70770[] = {
    {0, list_70640},
    {0, list_7064c},
    {4, list_70658},
    {4, list_70660},
    {1, list_70668},
    {1, list_70674},
    {1, list_70680},
    {1, list_7068c},
    {7, list_70694},
    {7, list_706a0},
    {-1, NULL},
};
static const PartClut parts_707c8[] = {
    {0, list_706d8},
    {1, list_706dc},
    {1, list_706e4},
    {0, list_706e8},
    {0, list_706ec},
    {2, list_706f0},
    {3, list_706f4},
    {1, list_706f8},
    {1, list_706fc},
    {1, list_70700},
    {1, list_70704},
    {-1, NULL},
};
static const PartClut parts_70828[] = {
    {0, list_706ac},
    {2, list_706b4},
    {4, list_706b8},
    {4, list_706bc},
    {1, list_706c0},
    {1, list_706c8},
    {1, list_706d0},
    {1, list_706d4},
    {-1, NULL},
};
static const PartClut parts_70870[] = {
    {0, list_70708},
    {0, list_7070c},
    {0, list_70710},
    {3, list_70714},
    {2, list_70718},
    {1, list_7071c},
    {1, list_70720},
    {4, list_70724},
    {4, list_70728},
    {0, list_7072c},
    {1, list_70730},
    {1, list_70734},
    {1, list_70738},
    {7, list_7073c},
    {7, list_70740},
    {-1, NULL},
};
static const PartClut parts_708f8[] = {
    {5, list_70744},
    {5, list_70748},
    {5, list_7074c},
    {5, list_70750},
    {5, list_70754},
    {5, list_70758},
    {5, list_7075c},
    {6, list_70760},
    {6, list_70768},
    {-1, NULL},
};

/* The doors start on the paint's first door texture; the unlit flat faces (the windows) go black. The
 * models' door UVs are laid out on car 18's first door (DR88A, 0x8007BB24 = car_paint[18].door[0]). */
static void init_model_doors(const Object *obj, const int8_t *tri_a, const int8_t *tri_b, const int8_t *quad_a,
                             const int8_t *quad_b, const Sprite *door)
{
    const Sprite *model_door = &car_paint[NO_PAINT_CAR].door[0];
    for (RunCursor c = first_run(obj); c.run->record_size != 0; next_run(&c)) {
        int kind = c.run->kind & 0xfd;
        if (kind == LIT_PICT3) {
            Retexture_Tris(c.prim[0], c.prim[1], tri_a, model_door, door);
            Retexture_Tris(c.prim[0], c.prim[1], tri_b, model_door, door);
        } else if (kind == LIT_FLAT3) {
            uint8_t *rec = (uint8_t *)(c.run + 1);
            for (int i = 0; i < c.run->count; i++, rec += c.run->record_size) {
                FaceFlat *f = (FaceFlat *)rec;
                f->rgbc[0] = 0;
                f->rgbc[1] = 0;
                f->rgbc[2] = 0;
            }
        } else if (kind == LIT_PICT4) {
            Retexture_Quads(c.prim[0], c.prim[1], quad_a, model_door, door);
            Retexture_Quads(c.prim[0], c.prim[1], quad_b, model_door, door);
        }
    }
}

void Init_Car_Doors(void)
{
    for (int i = 0; i < num_cars; i++) {
        door_state[i][0] = 0;
        door_state[i][1] = 0;
    }
    for (int i = 0; i < num_cars; i++)
        init_model_doors(&car_objects[i][0], list_70694, list_706a0, no_faces, no_faces, &car_paint[i].door[0]);
    for (int i = 0; i < num_cars; i++)
        init_model_doors(&car_objects[i][1], no_faces, no_faces, list_7073c, list_70740, &car_paint[i].door[0]);
}

/* Model 0: the body CLUTs of the player's own model (object 0); 1: those of a car's near model
 * (object 1); 2: the extra CLUTs of a car's distant model (object 0). Car 18 keeps its model's colours. */
void Apply_Car_Part_Cluts(int car, int paint, int model)
{
    if (car == NO_PAINT_CAR)
        return;
    const PartClut *tris, *quads;
    const Object *obj;
    if (model == 0) {
        tris = parts_70770;
        quads = parts_707c8;
        obj = &car_objects[car][0];
    } else if (model == 1) {
        tris = parts_70828;
        quads = parts_70870;
        obj = &car_objects[car][1];
    } else {
        static const PartClut none[] = {{-1, NULL}}; /* 0x800708F0 */
        tris = none;
        quads = parts_708f8;
        obj = &car_objects[car][0];
    }
    for (RunCursor c = first_run(obj); c.run->record_size != 0; next_run(&c)) {
        int kind = c.run->kind & 0xfd;
        if (kind == LIT_PICT3)
            Set_Tri_Cluts(c.prim[0], c.prim[1], tris, paint);
        else if (kind == LIT_PICT4)
            Set_Quad_Cluts_GT4(c.prim[0], c.prim[1], quads, paint);
    }
}

/* A loose bonnet or boot takes the paint of the car it came off. */
static void set_part_clut(const Object *obj, int tri_kind, const int8_t *tris, const int8_t *quads, uint16_t clut)
{
    for (RunCursor c = first_run(obj); c.run->record_size != 0; next_run(&c)) {
        int kind = c.run->kind & 0xfd;
        for (int b = 0; b < 2; b++) {
            if (kind == tri_kind && tris != NULL) {
                for (const int8_t *f = tris; *f != -1; f++)
                    ((POLY_FT3 *)c.prim[b])[*f].clut = clut;
            } else if (kind == 0xd) {
                for (const int8_t *f = quads; *f != -1; f++)
                    ((POLY_FT4 *)c.prim[b])[*f].clut = clut;
            }
        }
    }
}

void Change_Bonnet_Clut(int paint, int bonnet)
{
    set_part_clut(&bonnet_objects[bonnet], 9, list_7098c, list_70984, car_paint[paint].clut[0]);
}

void Change_Boot_Clut(int paint, int boot)
{
    set_part_clut(&boot_objects[boot], -1, NULL, list_709c0, car_paint[paint].clut[0]);
}

/* Lights up (or restores) one part of the near model for the pit camera: the faces' colour. */
void Highlight_Area(int car, int area, uint8_t level)
{
    static const int8_t *const tris[10] = {
        list_7068c, list_70694, list_70674, no_faces, list_70640,
        list_70668, list_706a0, list_70680, no_faces, no_faces,
    };
    static const int8_t *const quads[10] = {
        list_70704, no_faces, list_706fc, list_706dc, list_706d8,
        list_706f8, no_faces, list_70700, list_706e4, list_706e8,
    };
    const Mesh *mesh = car_objects[car][0].mesh;
    const FaceRun *run = (const FaceRun *)mesh_section(mesh, mesh->faces);
    while (run->record_size != 0) {
        uint8_t *rec = (uint8_t *)(run + 1);
        int kind = run->kind & 0xfd;
        const int8_t *list = kind == LIT_PICT3 ? tris[area] : kind == LIT_PICT4 ? quads[area] : no_faces;
        for (; *list != -1; list++) {
            uint8_t *rgbc = rec + *list * run->record_size + 4;
            rgbc[0] = level;
            rgbc[1] = level;
            rgbc[2] = level;
        }
        run = (const FaceRun *)(rec + run->count * run->record_size);
    }
}

/* Swaps one door's texture to another damage stage of the paint. */
void Set_Car_Door_Texture(int car, int paint, int model, int door, int state)
{
    int old = door_state[car][door];
    door_state[car][door] = (uint8_t)state;
    const Sprite *from = &car_paint[paint].door[old];
    const Sprite *to = &car_paint[paint].door[state];
    const int8_t *tris = no_faces, *quads = no_faces;
    const Object *obj;
    if (model == 0) {
        tris = door == 0 ? list_70694 : list_706a0;
        obj = &car_objects[car][0];
    } else {
        quads = door == 0 ? list_7073c : list_70740;
        obj = &car_objects[car][1];
    }
    for (RunCursor c = first_run(obj); c.run->record_size != 0; next_run(&c)) {
        int kind = c.run->kind & 0xfd;
        if (kind == LIT_PICT3)
            Retexture_Tris(c.prim[0], c.prim[1], tris, from, to);
        else if (kind == LIT_PICT4)
            Retexture_Quads(c.prim[0], c.prim[1], quads, from, to);
    }
}

/* A body region's damage stages and its faces in one model. */
typedef struct {
    const PartTexture *stages;
    const int8_t *tris, *quads;
} DentRegion;

static void sprite_at(Sprite *s, const PartTexture *t)
{
    s->tpage = t->tpage;
    s->u = t->u;
    s->v = t->v;
}

/* A region of a car's model shows damage stage `to` instead of `from`. Regions 2 and 6 are the doors;
 * at stage 3 the bonnet and boot are gone and their faces show the stage's colours as well. */
static void texture_dent(const Object *obj, const DentRegion *regions, int car, int model, int region, int from,
                         int to)
{
    if (region == 2 || region == 6) {
        Set_Car_Door_Texture(car, car, model, region == 2, to);
        return;
    }
    const DentRegion *r = &regions[region];
    Sprite old_stage, new_stage;
    sprite_at(&old_stage, &r->stages[from]);
    sprite_at(&new_stage, &r->stages[to]);
    int recolour = to == 3 && (region == 8 || region == 10);
    uint16_t clut = region == 8 ? part_textures.bonnet[3].clut : part_textures.boot[3].clut;
    for (RunCursor c = first_run(obj); c.run->record_size != 0; next_run(&c)) {
        int kind = c.run->kind & 0xfd;
        if (kind == LIT_PICT3) {
            if (recolour) {
                for (const int8_t *f = r->tris; *f != -1; f++) {
                    ((POLY_GT3 *)c.prim[0])[*f].clut = clut;
                    ((POLY_GT3 *)c.prim[1])[*f].clut = clut;
                }
            }
            Retexture_Tris(c.prim[0], c.prim[1], r->tris, &old_stage, &new_stage);
        } else if (kind == LIT_PICT4) {
            if (recolour) {
                for (const int8_t *f = r->quads; *f != -1; f++) {
                    ((POLY_GT4 *)c.prim[0])[*f].clut = clut;
                    ((POLY_GT4 *)c.prim[1])[*f].clut = clut;
                }
            }
            Retexture_Quads(c.prim[0], c.prim[1], r->quads, &old_stage, &new_stage);
        }
    }
}

void TextureDentHiCar(int car, int region, int from, int to)
{
    const PartTextures *t = &part_textures;
    const DentRegion regions[11] = {
        {t->front, no_faces, list_706dc},
        {t->front_window, list_70668, list_706f8},
        {NULL, NULL, NULL},
        {t->back_window, list_70680, list_70700},
        {t->bumper, no_faces, list_706e4},
        {t->back_window, list_7068c, list_70704},
        {NULL, NULL, NULL},
        {t->front_window, list_70674, list_706fc},
        {t->bonnet, list_70640, list_706d8},
        {t->roof, list_7064c, no_faces},
        {t->boot, no_faces, list_706e8},
    };
    texture_dent(&car_objects[car][0], regions, car, 0, region, from, to);
}

void TextureDentMidCar(int car, int region, int from, int to)
{
    const PartTextures *t = &part_textures;
    const DentRegion regions[11] = {
        {t->front, no_faces, list_70720},
        {t->front_window, list_706c8, list_70730},
        {NULL, NULL, NULL},
        {t->back_window, list_706d4, list_70734},
        {t->bumper, no_faces, list_7071c},
        {t->back_window, list_706d0, list_70738},
        {NULL, NULL, NULL},
        {t->front_window, list_706c0, no_faces},
        {t->bonnet, list_706ac, list_7070c},
        {t->roof, no_faces, list_70708},
        {t->boot, no_faces, list_70710},
    };
    texture_dent(&car_objects[car][1], regions, car, 1, region, from, to);
}
