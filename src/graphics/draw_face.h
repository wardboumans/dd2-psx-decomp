#ifndef GRAPHICS_DRAW_FACE_H
#define GRAPHICS_DRAW_FACE_H

#include "psx/libgpu.h"
#include "psx/libgte.h"

#include <stddef.h>
#include <stdint.h>

/* A mesh's face stream is a list of runs: a FaceRun header, then `count` records of that kind,
 * until a header whose `record_size` is 0. Each kind has a handler in the three face tables; a handler
 * walks `count` records from gpoly and packets from gprim (in port sizes). */
typedef struct {
    int16_t count;
    uint8_t kind;
    uint8_t record_size; /* bytes per record; 0 ends the list */
} FaceRun;

#define FACE_KINDS 44

/* Face records, as stored in the mesh (sizes per kind are fixed). rgbc is r, g, b, GPU code;
 * `normal` is the face normal of the lit flat and textured kinds; v/n index gvertices/gnormals. */
typedef struct {
    uint16_t f00;
    int16_t normal;
    uint8_t rgbc[4];
    int16_t v[4];
} FaceFlat; /* F3, F4 */

typedef struct {
    uint16_t f00;
    int16_t normal;
    uint8_t rgbc[4];
    int16_t texture;
    uint16_t clut;
    int16_t v[4];
} FaceText; /* FT3, FT4, sprites */

typedef struct {
    uint32_t f00;
    uint8_t rgbc[3][4];
    int16_t v[4];
} FaceGour3;

typedef struct {
    uint32_t f00;
    uint8_t rgbc[4];
    int16_t v[3];
    int16_t n[3];
} FaceGour3Lit;

typedef struct {
    uint32_t f00;
    uint8_t rgbc[4][4];
    int16_t v[4];
} FaceGour4;

typedef struct {
    uint32_t f00;
    uint8_t rgbc[4];
    int16_t v[4];
    int16_t n[4];
} FaceGour4Lit;

typedef struct {
    uint32_t f00;
    uint8_t rgbc[3][4];
    int16_t texture;
    uint16_t clut;
    int16_t v[4];
} FacePict3; /* GT3 */

typedef struct {
    uint32_t f00;
    uint8_t rgbc[4];
    int16_t texture;
    uint16_t clut;
    int16_t v[3];
    int16_t n[3];
} FacePict3Lit;

typedef struct {
    uint32_t f00;
    uint8_t rgbc[4][4];
    int16_t texture;
    uint16_t clut;
    int16_t v[4];
} FacePict4; /* GT4 */

typedef struct {
    uint32_t f00;
    uint8_t rgbc[4];
    int16_t texture;
    uint16_t clut;
    int16_t v[4];
    int16_t n[4];
} FacePict4Lit;

_Static_assert(sizeof(FaceFlat) == 0x10, "F3/F4 record");
_Static_assert(sizeof(FaceText) == 0x14, "FT3/FT4 record");
_Static_assert(sizeof(FaceGour3) == 0x18, "G3 record");
_Static_assert(sizeof(FaceGour3Lit) == 0x14, "G3 lit record");
_Static_assert(sizeof(FaceGour4) == 0x1c, "G4 record");
_Static_assert(sizeof(FaceGour4Lit) == 0x18, "G4 lit record");
_Static_assert(sizeof(FacePict3) == 0x1c, "GT3 record");
_Static_assert(sizeof(FacePict3Lit) == 0x18, "GT3 lit record");
_Static_assert(sizeof(FacePict4) == 0x20, "GT4 record");
_Static_assert(sizeof(FacePict4Lit) == 0x1c, "GT4 lit record");

/* A texture definition of the level (12 bytes): page and the UVs of up to four corners. */
typedef struct {
    uint16_t tpage;
    uint16_t f02;
    uint8_t uv[4][2];
} TextureDef;

_Static_assert(sizeof(TextureDef) == 12, "TextureDef");

typedef void (*FaceHandler)(int count);

extern const FaceHandler face_table_subdiv[FACE_KINDS]; /* 0x80072044, Draw_Subdiv_Object */
extern const FaceHandler face_table_object[FACE_KINDS]; /* 0x800720F4, Draw_Object_Level */
extern const FaceHandler face_table_build[FACE_KINDS];  /* 0x800721A4, Build_Object_Packets */

extern const uint8_t *gpoly;          /* 0x80079A58, cursor over face records */
extern uint8_t *gprim;                /* 0x80079A6C, cursor over this frame's packets */
extern uint8_t *gprim_other;          /* 0x80079A70, the other frame's, while building */
extern const TextureDef *gtexture;    /* 0x80079BDC */
extern const SVECTOR *gvertices;      /* 0x80079B2C */
extern const SVECTOR *gnormals;       /* 0x80079BA8 */
extern OT_TAG *got;                   /* 0x80079B78, the object's OT base */
extern uint32_t clip_flag_mask;       /* 0x80079950 */

/* Set by Set_World_View from the camera angles: */
extern MATRIX sprite_matrix;          /* 0x800A1CD8, the roll only: model sprites face the camera */
extern MATRIX tilt_matrix;            /* 0x800A6760, roll and pitch */

/* Face drawers (one per face kind, see the tables); each takes the run's record count. */
void draw_face_3pt_flat(int count);
void draw_face_3pt_flat_lit(int count);
void draw_face_3pt_flat_dpq(int count);
void draw_face_3pt_flat_dpq_lit(int count);
void draw_face_4pt_flat(int count);
void draw_face_4pt_flat_lit(int count);
void draw_face_4pt_flat_dpq(int count);
void draw_face_4pt_flat_dpq_lit(int count);
void draw_face_3pt_text(int count);
void draw_face_3pt_text_lit(int count);
void draw_face_3pt_text_dpq(int count);
void draw_face_3pt_text_dpq_lit(int count);
void draw_face_3pt_text_squash(int count);
void draw_face_3pt_text_dpq_squash(int count);
void draw_face_4pt_text(int count);
void draw_face_4pt_text_lit(int count);
void draw_face_4pt_text_dpq(int count);
void draw_face_4pt_text_dpq_lit(int count);
void draw_face_4pt_text_squash(int count);
void draw_face_4pt_text_dpq_squash(int count);
void draw_face_3pt_gour(int count);
void draw_face_3pt_gour_lit(int count);
void draw_face_3pt_gour_dpq(int count);
void draw_face_3pt_gour_dpq_lit(int count);
void draw_face_4pt_gour(int count);
void draw_face_4pt_gour_lit(int count);
void draw_face_4pt_gour_dpq(int count);
void draw_face_4pt_gour_dpq_lit(int count);
void draw_face_3pt_pict(int count);
void draw_face_3pt_pict_lit(int count);
void draw_face_3pt_pict_dpq(int count);
void draw_face_3pt_pict_dpq_lit(int count);
void draw_face_4pt_pict(int count);
void draw_face_4pt_pict_lit(int count);
void draw_face_4pt_pict_dpq(int count);
void draw_face_4pt_pict_dpq_lit(int count);
void draw_face_sprite(int count);
void draw_face_sprite_dpq(int count);
void draw_face_centred_tilt_sprite(int count);
void draw_face_centred_tilt_sprite_dpq(int count);
void draw_face_tilt_sprite(int count);
void draw_face_tilt_sprite_dpq(int count);

/* Bytes of one packet of a face kind in the primitive buffers (the original's table at 0x80070620
 * holds the PSX sizes). */
size_t face_packet_size(int kind);

void convert_poly_f3(int count);      /* 0x80052CC8 */
void convert_poly_f4(int count);      /* 0x800533BC */
void convert_poly_ft3(int count);     /* 0x80053C00 */
void convert_poly_ft4(int count);     /* 0x80055140 */
void convert_poly_g3(int count);      /* 0x80056C54 */
void convert_poly_g3_lit(int count);  /* 0x80056CF0 */
void convert_poly_g4(int count);      /* 0x800574AC */
void convert_poly_g4_lit(int count);  /* 0x80057554 */
void convert_poly_gt3(int count);     /* 0x80057EE0 */
void convert_poly_gt3_lit(int count); /* 0x80058004 */
void convert_poly_gt4(int count);     /* 0x80058864 */
void convert_poly_gt4_lit(int count); /* 0x8005899C */
void convert_poly_sprite(int count);  /* 0x800593D4 */

#endif
