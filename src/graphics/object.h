#ifndef GRAPHICS_OBJECT_H
#define GRAPHICS_OBJECT_H

#include "psx/libgpu.h"
#include "psx/libgte.h"

#include <stdint.h>

/* Primitive buffer sizes requested in PSX bytes are doubled: port packets are larger. */
#define PRIM_SIZE_SCALE 2

/* One half of the double buffer. The original is 0x74 bytes (with a 4-byte OT pointer). */
typedef struct {
    DRAWENV draw;
    DISPENV disp;
    OT_TAG *ot;
} DrawBuffer;

/* A display layout: two buffers at (draw_x, draw_y) and (disp_x, disp_y) that swap roles. */
typedef struct {
    short draw_x, draw_y;
    short disp_x, disp_y;
    short w, h;
    short screen_x, screen_y;
} VideoMode;

extern VideoMode video_modes[2]; /* 0x800717A0: PAL, NTSC */
/* The screen the menus and overlays are laid out on (NTSC shows 240 of its lines). */
#define SCREEN_W 320
#define SCREEN_H 256

extern DrawBuffer db[2];        /* 0x8008931C */
extern DrawBuffer *cdb;         /* 0x80079B60, the buffer being built */
extern int32_t buffer_num;      /* 0x80079924 */
extern int32_t otsize;          /* 0x80079AC8 */
extern int32_t pal_mode;        /* 0x80079920 */
extern int32_t frame_count;     /* 0x80079928 */
extern int32_t screen_width;    /* 0x8007992C */
extern int32_t screen_height;   /* 0x80079930 */
extern int32_t frames_per_sec;  /* 0x80079934 */
extern int32_t draw_height;     /* 0x80079938 */
extern int32_t last_vsync;      /* 0x8007993C */

/* A mesh as stored in the level data. The section fields are offsets from the mesh; the
 * original turns them into pointers on first use (setting `relocated`), the port keeps them
 * as offsets and resolves them with mesh_section(). */
typedef struct {
    uint16_t radius;      /* +0x00, bounding radius for culling */
    uint8_t unused_02[2]; /* +0x02: 0 in the level files */
    uint8_t flags;        /* +0x04, bit 0: lit; bit 7: placed at its exact position (Setup_Object_Block) */
    uint8_t subdivide;    /* +0x05, 0: never; 2: also the finer subdivision when inside the radius */
    uint16_t subdivide_z; /* +0x06, depth below which the subdivided mesh is drawn */
    uint16_t ot_bias;     /* +0x08, OT slot offset of the whole object */
    uint16_t vertex_count; /* +0x0A */
    uint16_t normal_count; /* +0x0C */
    uint16_t face_count;  /* +0x0E: tri_count + quad_count */
    uint16_t tri_count;   /* +0x10: three-point faces */
    uint16_t quad_count;  /* +0x12: four-point faces */
    uint16_t tri_count_copy, quad_count_copy; /* +0x14: 0 in the files; Subdivide_Object copies the counts here, nothing reads them */
    uint16_t prim_size;   /* +0x18, packet bytes in PSX sizes */
    uint16_t face_words;  /* +0x1A: prim_size + 4 per run; with run_count, sizes the subdivided face list */
    uint16_t run_count;   /* +0x1C: FaceRun headers in the face list, the end marker included */
    int16_t relocated;    /* +0x1E */
    uint32_t vertices;    /* +0x20 */
    uint32_t normals;     /* +0x24 */
    uint32_t faces;       /* +0x28 */
} Mesh;

_Static_assert(sizeof(Mesh) == 0x2c, "Mesh header must match the level data");

/* Offsets are signed: Draw_Car points a shared mesh at a car's own vertex copy outside it. */
static inline uint8_t *mesh_section(const Mesh *mesh, uint32_t offset)
{
    return (uint8_t *)mesh + (int32_t)offset;
}

static inline uint32_t mesh_offset_of(const Mesh *mesh, const void *p)
{
    return (uint32_t)(int32_t)((const uint8_t *)p - (const uint8_t *)mesh);
}

/* An object instance: a mesh at up to two detail levels with their packets (0x1C bytes in the
 * original). Level 2 is the subdivided mesh made by Subdivide_Object. */
typedef struct {
    const void *textures;  /* +0x00, the level's TextureDefs */
    Mesh *mesh;            /* +0x04 */
    int32_t prim_offset;   /* +0x08 */
    int32_t f0c;
    Mesh *mesh2;           /* +0x10 */
    int32_t prim_offset2;  /* +0x14 */
    uint8_t level1_timer;  /* +0x18: frames until the packets are freed (Draw_Scene_Object) */
    uint8_t level2_timer;  /* +0x19: frames until the subdivided mesh is freed */
    uint8_t level;         /* +0x1A: 0 no packets yet, else the level drawn */
    uint8_t f1b;
} Object;

extern const MATRIX *light_matrix; /* 0x80079B54 */
extern MATRIX world_matrix;        /* 0x8007175C, the camera: rotation, and its position in t */
extern MATRIX object_matrix;       /* 0x800892F0, world_matrix times the object's placement */

void Set_World_Position(const int32_t pos[3]);                   /* 0x8004F37C */
void Set_World_Matrix(const int16_t m[3][3]);                    /* 0x8004F3B4 */
void Set_World_View(const int16_t angles[3]);                    /* 0x8004F404 */
void Set_Object_View(const MATRIX *rotation, const int32_t pos[3]); /* 0x8004F5EC */
void Calc_Object_MatrixYXZ(MATRIX *m, const int16_t angles[3]);  /* 0x8004F85C */
void Calc_Object_MatrixYZX(MATRIX *m, const int16_t angles[3]);  /* 0x8004F8D8 */
void Calc_Object_Angles(const MATRIX *m, int16_t angles[3]);    /* 0x8004F954 */
void Point_Camera(const int32_t target[3], int roll);            /* 0x8004FE24 */
void Calc_Direction_Matrix(MATRIX *m, const int32_t dir[3], int roll); /* 0x8004FAF8 */

void Create_Object(Object *obj, Mesh *mesh);  /* 0x8005AD78 */
int32_t Build_Object_Packets(Object *obj);    /* 0x8005AC90 */
void Set_Object(Object *obj, Mesh *mesh);     /* 0x8005AEC0 */
void Remove_Object(Object *obj, int level);   /* 0x8005AF24 */
void Draw_Subdiv_Object(Object *obj);         /* 0x8005AFB4 */
void Draw_Object_Level(Object *obj, int level); /* 0x8005B0A0 */
void Update_Object(Object *obj);              /* 0x8005B1A0 */

/* Primitive buffers: one per display buffer, same layout, so an offset from Alloc_Prim_Buffer
 * addresses a primitive in either. Buffer 0 also holds the allocator's free list. */
extern uint8_t *prim_buffer[2];     /* 0x80079B90 */
extern int32_t prim_free_blocks;    /* 0x80079B74, in 8-byte blocks */

extern int32_t screen_centre_x, screen_centre_y; /* 0x800798DC */
extern int32_t projection_distance;              /* 0x800798D8 */
extern int32_t zclip_near, zclip_far;            /* 0x800798D0 */
extern const void *gtexture_def;                 /* 0x80079B48, texture definitions of the level */
extern int16_t view_norm_x[3], view_norm_y[3];   /* 0x80079A9C, 0x80079AB8: frustum side normals */

void Setup_View_Norm(const void *textures);           /* 0x8004F0C0 */
void set_screen_centre(int32_t x, int32_t y);         /* 0x8004F278 */
void set_projection_distance(int32_t d);              /* 0x8004F2B4 */
void Set_Zclip(int32_t near_z, int32_t far_z);        /* 0x8004F2E0 */
void Set_Ambient_Light(const uint8_t *rgb);           /* 0x8004F568 */
void Set_Depth_Cue(int32_t near_z, int32_t far_z, const uint8_t *rgb); /* 0x8004F598 */
void Init_Primitive_Buffer(int32_t size);             /* 0x8005B2F8 */
int32_t Alloc_Prim_Buffer(int32_t size);              /* 0x8005B3F0 */
void Free_Prim_Buffer(int32_t offset);                /* 0x8005B4A4 */

void Set_Glyph_Quad_Attrs(POLY_FT4 *quad, int tpage, int clut, const uint8_t *rgb, int trans); /* 0x80050A34 */
void Draw_Font_Poly(POLY_FT4 *quad, int ot_slot); /* 0x80050B44 */
/* An LZSS decompression that can run in pieces: each call writes up to `chunk` bytes. The
 * stream starts with its unpacked size; `state` is 0 before the start, -1 after the end. */
typedef struct {
    const uint8_t *src;
    uint8_t *dst;
    int32_t remaining;
    uint16_t chunk;
    int16_t state;
} DecompressState;

void Decompress(DecompressState *d); /* 0x80050BA0 */
void Allocate_OT(int slots);    /* 0x80050CB4 */
void Set_Draw_Mode(int mode);   /* 0x80050D10 */
void Swap_Draw_Buffers(void);   /* 0x80050E78 */
void Swap_Buffers(int vsyncs);  /* 0x80050ED0 */
void Draw_All(int vsyncs);      /* 0x80050F70 */
void Draw_Tile(TILE *tile, int ot_slot, int x, int y, int w, int h, const uint8_t *rgb); /* 0x8005102C */

#endif
