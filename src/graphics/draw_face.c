#include "graphics/draw_face.h"

#include "graphics/object.h"

#include <string.h>

const uint8_t *gpoly;
uint8_t *gprim;
uint8_t *gprim_other;
const TextureDef *gtexture;
const SVECTOR *gvertices;
const SVECTOR *gnormals;
OT_TAG *got;
uint32_t clip_flag_mask = GTE_CLIP_FLAGS;

/* ---- packet builders: run once per object, filling both frames' packets ---- */

/* The handlers below fill the packet at gprim, copy it to gprim_other and step both. */
#define NEXT_PACKET(type)                                       \
    do {                                                        \
        *(type *)gprim_other = *(type *)gprim;                  \
        gprim += sizeof(type);                                  \
        gprim_other += sizeof(type);                            \
    } while (0)

/* The record's colour word carries the GPU code (and with it the semi-transparency bit). */
#define SET_RGBC0(p, c) ((p)->r0 = (c)[0], (p)->g0 = (c)[1], (p)->b0 = (c)[2], (p)->code = (c)[3])

void convert_poly_f3(int count)
{
    for (int i = 0; i < count; i++) {
        const FaceFlat *f = (const FaceFlat *)gpoly;
        POLY_F3 *p = (POLY_F3 *)gprim;
        setPolyF3(p);
        SET_RGBC0(p, f->rgbc);
        NEXT_PACKET(POLY_F3);
        gpoly += sizeof(FaceFlat);
    }
}

void convert_poly_f4(int count)
{
    for (int i = 0; i < count; i++) {
        const FaceFlat *f = (const FaceFlat *)gpoly;
        POLY_F4 *p = (POLY_F4 *)gprim;
        setPolyF4(p);
        SET_RGBC0(p, f->rgbc);
        NEXT_PACKET(POLY_F4);
        gpoly += sizeof(FaceFlat);
    }
}

void convert_poly_ft3(int count)
{
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        const TextureDef *t = &gtexture[f->texture];
        POLY_FT3 *p = (POLY_FT3 *)gprim;
        setPolyFT3(p);
        SET_RGBC0(p, f->rgbc);
        p->tpage = t->tpage;
        p->clut = f->clut;
        setUV3(p, t->uv[0][0], t->uv[0][1], t->uv[1][0], t->uv[1][1], t->uv[2][0], t->uv[2][1]);
        gpoly += sizeof(FaceText);
        NEXT_PACKET(POLY_FT3);
    }
}

static void build_ft4(void)
{
    const FaceText *f = (const FaceText *)gpoly;
    const TextureDef *t = &gtexture[f->texture];
    POLY_FT4 *p = (POLY_FT4 *)gprim;
    setPolyFT4(p);
    SET_RGBC0(p, f->rgbc);
    p->tpage = t->tpage;
    p->clut = f->clut;
    setUV4(p, t->uv[0][0], t->uv[0][1], t->uv[1][0], t->uv[1][1], t->uv[2][0], t->uv[2][1], t->uv[3][0], t->uv[3][1]);
    NEXT_PACKET(POLY_FT4);
    gpoly += sizeof(FaceText);
}

void convert_poly_ft4(int count)
{
    for (int i = 0; i < count; i++)
        build_ft4();
}

/* Model sprites are textured quads too; their corners are set when drawn. */
void convert_poly_sprite(int count)
{
    for (int i = 0; i < count; i++)
        build_ft4();
}

#define SET_RGB(p, n, c) ((p)->r##n = (c)[0], (p)->g##n = (c)[1], (p)->b##n = (c)[2])

void convert_poly_g3(int count)
{
    for (int i = 0; i < count; i++) {
        const FaceGour3 *f = (const FaceGour3 *)gpoly;
        POLY_G3 *p = (POLY_G3 *)gprim;
        setPolyG3(p);
        SET_RGBC0(p, f->rgbc[0]);
        SET_RGB(p, 1, f->rgbc[1]);
        p->pad1 = f->rgbc[1][3];
        SET_RGB(p, 2, f->rgbc[2]);
        p->pad2 = f->rgbc[2][3];
        NEXT_PACKET(POLY_G3);
        gpoly += sizeof(FaceGour3);
    }
}

/* Lit faces get their colours from the light calculation each frame. */
void convert_poly_g3_lit(int count)
{
    for (int i = 0; i < count; i++) {
        setPolyG3((POLY_G3 *)gprim);
        setPolyG3((POLY_G3 *)gprim_other);
        gprim += sizeof(POLY_G3);
        gprim_other += sizeof(POLY_G3);
        gpoly += sizeof(FaceGour3Lit);
    }
}

void convert_poly_g4(int count)
{
    for (int i = 0; i < count; i++) {
        const FaceGour4 *f = (const FaceGour4 *)gpoly;
        POLY_G4 *p = (POLY_G4 *)gprim;
        setPolyG4(p);
        SET_RGBC0(p, f->rgbc[0]);
        SET_RGB(p, 1, f->rgbc[1]);
        p->pad1 = f->rgbc[1][3];
        SET_RGB(p, 2, f->rgbc[2]);
        p->pad2 = f->rgbc[2][3];
        SET_RGB(p, 3, f->rgbc[3]);
        p->pad3 = f->rgbc[3][3];
        NEXT_PACKET(POLY_G4);
        gpoly += sizeof(FaceGour4);
    }
}

void convert_poly_g4_lit(int count)
{
    for (int i = 0; i < count; i++) {
        setPolyG4((POLY_G4 *)gprim);
        setPolyG4((POLY_G4 *)gprim_other);
        gprim += sizeof(POLY_G4);
        gprim_other += sizeof(POLY_G4);
        gpoly += sizeof(FaceGour4Lit);
    }
}

static void set_gt3_texture(POLY_GT3 *p, int16_t texture, uint16_t clut)
{
    const TextureDef *t = &gtexture[texture];
    p->tpage = t->tpage;
    p->clut = clut;
    setUV3(p, t->uv[0][0], t->uv[0][1], t->uv[1][0], t->uv[1][1], t->uv[2][0], t->uv[2][1]);
}

void convert_poly_gt3(int count)
{
    for (int i = 0; i < count; i++) {
        const FacePict3 *f = (const FacePict3 *)gpoly;
        POLY_GT3 *p = (POLY_GT3 *)gprim;
        setPolyGT3(p);
        SET_RGBC0(p, f->rgbc[0]);
        SET_RGB(p, 1, f->rgbc[1]);
        p->p1 = f->rgbc[1][3];
        SET_RGB(p, 2, f->rgbc[2]);
        p->p2 = f->rgbc[2][3];
        set_gt3_texture(p, f->texture, f->clut);
        NEXT_PACKET(POLY_GT3);
        gpoly += sizeof(FacePict3);
    }
}

void convert_poly_gt3_lit(int count)
{
    for (int i = 0; i < count; i++) {
        const FacePict3Lit *f = (const FacePict3Lit *)gpoly;
        POLY_GT3 *p = (POLY_GT3 *)gprim;
        setPolyGT3(p);
        set_gt3_texture(p, f->texture, f->clut);
        NEXT_PACKET(POLY_GT3);
        gpoly += sizeof(FacePict3Lit);
    }
}

static void set_gt4_texture(POLY_GT4 *p, int16_t texture, uint16_t clut)
{
    const TextureDef *t = &gtexture[texture];
    p->tpage = t->tpage;
    p->clut = clut;
    setUV4(p, t->uv[0][0], t->uv[0][1], t->uv[1][0], t->uv[1][1], t->uv[2][0], t->uv[2][1], t->uv[3][0], t->uv[3][1]);
}

void convert_poly_gt4(int count)
{
    for (int i = 0; i < count; i++) {
        const FacePict4 *f = (const FacePict4 *)gpoly;
        POLY_GT4 *p = (POLY_GT4 *)gprim;
        setPolyGT4(p);
        SET_RGBC0(p, f->rgbc[0]);
        SET_RGB(p, 1, f->rgbc[1]);
        p->p1 = f->rgbc[1][3];
        SET_RGB(p, 2, f->rgbc[2]);
        p->p2 = f->rgbc[2][3];
        SET_RGB(p, 3, f->rgbc[3]);
        p->p3 = f->rgbc[3][3];
        set_gt4_texture(p, f->texture, f->clut);
        NEXT_PACKET(POLY_GT4);
        gpoly += sizeof(FacePict4);
    }
}

void convert_poly_gt4_lit(int count)
{
    for (int i = 0; i < count; i++) {
        const FacePict4Lit *f = (const FacePict4Lit *)gpoly;
        POLY_GT4 *p = (POLY_GT4 *)gprim;
        setPolyGT4(p);
        set_gt4_texture(p, f->texture, f->clut);
        NEXT_PACKET(POLY_GT4);
        gpoly += sizeof(FacePict4Lit);
    }
}

/* ---- face drawing: run every frame, transforming vertices and linking visible packets ---- */

/* RTPT on three vertices and NCLIP; true when no clip flag is set. Leaves the SXY FIFO and
 * MAC0 (opz) for the caller. */
static int load_tri(const int16_t *v)
{
    gte_ldv3(&gvertices[v[0]], &gvertices[v[1]], &gvertices[v[2]]);
    gte_rtpt();
    int32_t flag;
    gte_stflg(&flag);
    gte_nclip();
    return (clip_flag_mask & (uint32_t)flag) == 0;
}

/* load_tri and facing the camera (opz <= 0). */
static int front_tri(const int16_t *v)
{
    if (!load_tri(v))
        return 0;
    int32_t opz;
    gte_stopz(&opz);
    return opz < 1;
}

/* RTPS on the fourth vertex and AVSZ4; true when no clip flag is set. */
static int fourth_vertex(int16_t v)
{
    gte_ldv0(&gvertices[v]);
    gte_rtps();
    int32_t flag;
    gte_stflg(&flag);
    gte_avsz4();
    return (clip_flag_mask & (uint32_t)flag) == 0;
}

static void link_prim(void *p)
{
    int32_t otz;
    gte_stotz(&otz);
    addPrim(&got[otz], p);
}

static void load_rgbc(const uint8_t c[4])
{
    CVECTOR v = {c[0], c[1], c[2], c[3]};
    gte_ldrgb(&v);
}

/* One colour lit by a normal (NCCS, or NCDS with depth cue), written over the packet's rgbc. */
static void lit_colour(const uint8_t c[4], int16_t normal, void *dst, int depth_cue)
{
    load_rgbc(c);
    gte_ldv0(&gnormals[normal]);
    if (depth_cue)
        gte_ncds();
    else
        gte_nccs();
    gte_strgb((CVECTOR *)dst);
}

static void dpq_colour(const uint8_t c[4], void *dst)
{
    load_rgbc(c);
    gte_dpcs();
    gte_strgb((CVECTOR *)dst);
}

/* Three vertex colours lit by their normals (NCCT/NCDT) from the loaded RGBC. */
static void lit_colours3(const int16_t n[3], void *a, void *b, void *c, int depth_cue)
{
    gte_ldv3(&gnormals[n[0]], &gnormals[n[1]], &gnormals[n[2]]);
    if (depth_cue)
        gte_ncdt();
    else
        gte_ncct();
    gte_strgb3((CVECTOR *)a, (CVECTOR *)b, (CVECTOR *)c);
}

enum { PLAIN, LIT, DPQ, DPQ_LIT };

/* Flat triangles: F3 records, POLY_F3 packets. */
static void flat_tri(int count, int mode)
{
    for (int i = 0; i < count; i++) {
        const FaceFlat *f = (const FaceFlat *)gpoly;
        POLY_F3 *p = (POLY_F3 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz;
            gte_stopz(&opz);
            gte_avsz3();
            if (opz < 1) {
                gte_stsxy3_f3(p);
                link_prim(p);
                if (mode == LIT || mode == DPQ_LIT)
                    lit_colour(f->rgbc, f->normal, &p->r0, mode == DPQ_LIT);
                else if (mode == DPQ)
                    dpq_colour(f->rgbc, &p->r0);
            }
        }
        gpoly += sizeof(FaceFlat);
        gprim += sizeof(POLY_F3);
    }
}

void draw_face_3pt_flat(int count) { flat_tri(count, PLAIN); }            /* 0x80052D44 */
void draw_face_3pt_flat_lit(int count) { flat_tri(count, LIT); }          /* 0x80052EB4 */
void draw_face_3pt_flat_dpq(int count) { flat_tri(count, DPQ); }          /* 0x80053070 */
void draw_face_3pt_flat_dpq_lit(int count) { flat_tri(count, DPQ_LIT); }  /* 0x80053200 */

/* Flat quads: F4 records, POLY_F4 packets; only the first triangle is tested for facing. */
static void flat_quad(int count, int mode)
{
    for (int i = 0; i < count; i++) {
        const FaceFlat *f = (const FaceFlat *)gpoly;
        POLY_F4 *p = (POLY_F4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_f3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
                if (mode == LIT || mode == DPQ_LIT)
                    lit_colour(f->rgbc, f->normal, &p->r0, mode == DPQ_LIT);
                else if (mode == DPQ)
                    dpq_colour(f->rgbc, &p->r0);
            }
        }
        gpoly += sizeof(FaceFlat);
        gprim += sizeof(POLY_F4);
    }
}

void draw_face_4pt_flat(int count) { flat_quad(count, PLAIN); }           /* 0x80053438 */
void draw_face_4pt_flat_lit(int count) { flat_quad(count, LIT); }         /* 0x800535FC */
void draw_face_4pt_flat_dpq(int count) { flat_quad(count, DPQ); }         /* 0x8005380C */
void draw_face_4pt_flat_dpq_lit(int count) { flat_quad(count, DPQ_LIT); } /* 0x800539F0 */

/* Textured triangles: FT3 records, POLY_FT3 packets. */
static void text_tri(int count, int mode)
{
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        POLY_FT3 *p = (POLY_FT3 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz;
            gte_stopz(&opz);
            gte_avsz3();
            if (opz < 1) {
                gte_stsxy3_ft3(p);
                link_prim(p);
                if (mode == LIT || mode == DPQ_LIT)
                    lit_colour(f->rgbc, f->normal, &p->r0, mode == DPQ_LIT);
                else if (mode == DPQ)
                    dpq_colour(f->rgbc, &p->r0);
            }
        }
        gpoly += sizeof(FaceText);
        gprim += sizeof(POLY_FT3);
    }
}

void draw_face_3pt_text(int count) { text_tri(count, PLAIN); }            /* 0x80053D04 */
void draw_face_3pt_text_lit(int count) { text_tri(count, LIT); }          /* 0x80054548 */
void draw_face_3pt_text_dpq(int count) { text_tri(count, DPQ); }          /* 0x80054704 */
void draw_face_3pt_text_dpq_lit(int count) { text_tri(count, DPQ_LIT); }  /* 0x80054F84 */

/* Textured quads, unlit: drawn when either half faces the camera (the second NCLIP runs on
 * vertices 1-3 after RTPS, so its sign is reversed). */
static void text_quad_both(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        POLY_FT4 *p = (POLY_FT4 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz, opz2, flag;
            gte_stopz(&opz);
            gte_stsxy3_ft3(p);
            gte_ldv0(&gvertices[f->v[3]]);
            gte_rtps();
            gte_stflg(&flag);
            gte_nclip();
            if ((clip_flag_mask & (uint32_t)flag) == 0) {
                gte_stopz(&opz2);
                gte_avsz4();
                if (opz < 1 || opz2 >= 0) {
                    gte_stsxy_xy3(p);
                    link_prim(p);
                    if (depth_cue)
                        dpq_colour(f->rgbc, &p->r0);
                }
            }
        }
        gpoly += sizeof(FaceText);
        gprim += sizeof(POLY_FT4);
    }
}

/* Textured quads, lit: only the first triangle is tested for facing. */
static void text_quad_lit(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        POLY_FT4 *p = (POLY_FT4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_ft3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
                lit_colour(f->rgbc, f->normal, &p->r0, depth_cue);
            }
        }
        gpoly += sizeof(FaceText);
        gprim += sizeof(POLY_FT4);
    }
}

void draw_face_4pt_text(int count) { text_quad_both(count, 0); }          /* 0x80055258 */
void draw_face_4pt_text_lit(int count) { text_quad_lit(count, 0); }       /* 0x80055D28 */
void draw_face_4pt_text_dpq(int count) { text_quad_both(count, 1); }      /* 0x80055F38 */
void draw_face_4pt_text_dpq_lit(int count) { text_quad_lit(count, 1); }   /* 0x80056A44 */

/* Near-plane textured faces of subdivided objects ("squash"): no clip-flag test; a projected
 * corner beyond the box (-340..660, -130..370) is pulled onto its edge along the line to the
 * screen centre (160, 120), first in x, then in y. */
#define SQUASH_CENTRE_X 160
#define SQUASH_CENTRE_Y 120
#define SQUASH_MIN_X (-340)
#define SQUASH_MAX_X 660
#define SQUASH_MIN_Y (-130)
#define SQUASH_MAX_Y 370

static void squash_point(short *x, short *y)
{
    int sx = *x, sy = *y;
    if (sx >= SQUASH_MAX_X) {
        *y = (short)((short)((sy - SQUASH_CENTRE_Y) * (SQUASH_MAX_X - SQUASH_CENTRE_X) / (sx - SQUASH_CENTRE_X)) +
                     SQUASH_CENTRE_Y);
        *x = SQUASH_MAX_X;
    } else if (sx < SQUASH_MIN_X + 1) {
        *y = (short)(SQUASH_CENTRE_Y -
                     (short)((sy - SQUASH_CENTRE_Y) * (SQUASH_MAX_X - SQUASH_CENTRE_X) / (sx - SQUASH_CENTRE_X)));
        *x = SQUASH_MIN_X;
    }
    sx = *x;
    sy = *y;
    if (sy >= SQUASH_MAX_Y) {
        *x = (short)((short)((sx - SQUASH_CENTRE_X) * (SQUASH_MAX_Y - SQUASH_CENTRE_Y) / (sy - SQUASH_CENTRE_Y)) +
                     SQUASH_CENTRE_X);
        *y = SQUASH_MAX_Y;
    } else if (sy < SQUASH_MIN_Y + 1) {
        *x = (short)(SQUASH_CENTRE_X -
                     (short)((sx - SQUASH_CENTRE_X) * (SQUASH_MAX_Y - SQUASH_CENTRE_Y) / (sy - SQUASH_CENTRE_Y)));
        *y = SQUASH_MIN_Y;
    }
}

static void text_tri_squash(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        POLY_FT3 *p = (POLY_FT3 *)gprim;
        gte_ldv3(&gvertices[f->v[0]], &gvertices[f->v[1]], &gvertices[f->v[2]]);
        gte_rtpt();
        gte_nclip();
        int32_t opz, otz;
        gte_stopz(&opz);
        gte_avsz3();
        if (opz < 1) {
            short x[3] = {gte.sx[0], gte.sx[1], gte.sx[2]}, y[3] = {gte.sy[0], gte.sy[1], gte.sy[2]};
            gte_stotz(&otz);
            if (otz > 0) {
                for (int k = 0; k < 3; k++)
                    squash_point(&x[k], &y[k]);
                setXY3(p, x[0], y[0], x[1], y[1], x[2], y[2]);
                addPrim(&got[otz], p);
                if (depth_cue)
                    dpq_colour(f->rgbc, &p->r0);
            }
        }
        gpoly += sizeof(FaceText);
        gprim += sizeof(POLY_FT3);
    }
}

/* Quads are drawn when either half faces the camera, as in text_quad_both. */
static void text_quad_squash(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        POLY_FT4 *p = (POLY_FT4 *)gprim;
        gte_ldv3(&gvertices[f->v[0]], &gvertices[f->v[1]], &gvertices[f->v[2]]);
        gte_rtpt();
        gte_nclip();
        int32_t opz, opz2, otz;
        gte_stopz(&opz);
        short x[4] = {gte.sx[0], gte.sx[1], gte.sx[2], 0}, y[4] = {gte.sy[0], gte.sy[1], gte.sy[2], 0};
        gte_ldv0(&gvertices[f->v[3]]);
        gte_rtps();
        gte_nclip();
        gte_stopz(&opz2);
        gte_avsz4();
        if (opz < 1 || opz2 >= 0) {
            x[3] = gte.sx[2];
            y[3] = gte.sy[2];
            gte_stotz(&otz);
            if (otz > 0) {
                for (int k = 0; k < 4; k++)
                    squash_point(&x[k], &y[k]);
                setXY4(p, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
                addPrim(&got[otz], p);
                if (depth_cue)
                    dpq_colour(f->rgbc, &p->r0);
            }
        }
        gpoly += sizeof(FaceText);
        gprim += sizeof(POLY_FT4);
    }
}

void draw_face_3pt_text_squash(int count) { text_tri_squash(count, 0); }      /* 0x80053E74 */
void draw_face_3pt_text_dpq_squash(int count) { text_tri_squash(count, 1); }  /* 0x80054894 */
void draw_face_4pt_text_squash(int count) { text_quad_squash(count, 0); }     /* 0x80055440 */
void draw_face_4pt_text_dpq_squash(int count) { text_quad_squash(count, 1); } /* 0x80056140 */

/* Gouraud triangles. Plain and depth-cued use G3 records with three colours; the lit kinds use
 * G3-lit records: one colour and three normals. */
void draw_face_3pt_gour(int count) /* 0x80056D60 */
{
    for (int i = 0; i < count; i++) {
        const FaceGour3 *f = (const FaceGour3 *)gpoly;
        POLY_G3 *p = (POLY_G3 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz;
            gte_stopz(&opz);
            gte_avsz3();
            if (opz < 1) {
                gte_stsxy3_g3(p);
                link_prim(p);
            }
        }
        gpoly += sizeof(FaceGour3);
        gprim += sizeof(POLY_G3);
    }
}

void draw_face_3pt_gour_dpq(int count) /* 0x800570DC */
{
    for (int i = 0; i < count; i++) {
        const FaceGour3 *f = (const FaceGour3 *)gpoly;
        POLY_G3 *p = (POLY_G3 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz;
            gte_stopz(&opz);
            gte_avsz3();
            if (opz < 1) {
                gte_stsxy3_g3(p);
                link_prim(p);
                gte_ldrgb3((const CVECTOR *)f->rgbc[0], (const CVECTOR *)f->rgbc[1], (const CVECTOR *)f->rgbc[2]);
                gte_dpct();
                gte_strgb3((CVECTOR *)&p->r0, (CVECTOR *)&p->r1, (CVECTOR *)&p->r2);
            }
        }
        gpoly += sizeof(FaceGour3);
        gprim += sizeof(POLY_G3);
    }
}

static void gour_tri_lit(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FaceGour3Lit *f = (const FaceGour3Lit *)gpoly;
        POLY_G3 *p = (POLY_G3 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz;
            gte_stopz(&opz);
            gte_avsz3();
            if (opz < 1) {
                gte_stsxy3_g3(p);
                link_prim(p);
                load_rgbc(f->rgbc);
                lit_colours3(f->n, &p->r0, &p->r1, &p->r2, depth_cue);
            }
        }
        gpoly += sizeof(FaceGour3Lit);
        gprim += sizeof(POLY_G3);
    }
}

void draw_face_3pt_gour_lit(int count) { gour_tri_lit(count, 0); }        /* 0x80056ED0 */
void draw_face_3pt_gour_dpq_lit(int count) { gour_tri_lit(count, 1); }    /* 0x800572A0 */

/* Gouraud quads; only the first triangle is tested for facing. */
void draw_face_4pt_gour(int count) /* 0x800575C4 */
{
    for (int i = 0; i < count; i++) {
        const FaceGour4 *f = (const FaceGour4 *)gpoly;
        POLY_G4 *p = (POLY_G4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_g3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
            }
        }
        gpoly += sizeof(FaceGour4);
        gprim += sizeof(POLY_G4);
    }
}

void draw_face_4pt_gour_dpq(int count) /* 0x80057A1C */
{
    for (int i = 0; i < count; i++) {
        const FaceGour4 *f = (const FaceGour4 *)gpoly;
        POLY_G4 *p = (POLY_G4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_g3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
                gte_ldrgb3((const CVECTOR *)f->rgbc[0], (const CVECTOR *)f->rgbc[1], (const CVECTOR *)f->rgbc[2]);
                gte_dpct();
                gte_strgb3((CVECTOR *)&p->r0, (CVECTOR *)&p->r1, (CVECTOR *)&p->r2);
                dpq_colour(f->rgbc[3], &p->r3);
            }
        }
        gpoly += sizeof(FaceGour4);
        gprim += sizeof(POLY_G4);
    }
}

void draw_face_4pt_gour_lit(int count) /* 0x80057788 */
{
    for (int i = 0; i < count; i++) {
        const FaceGour4Lit *f = (const FaceGour4Lit *)gpoly;
        POLY_G4 *p = (POLY_G4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_g3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
                load_rgbc(f->rgbc);
                lit_colours3(f->n, &p->r0, &p->r1, &p->r2, 0);
                gte_ldv0(&gnormals[f->n[3]]);
                gte_nccs();
                gte_strgb((CVECTOR *)&p->r3);
            }
        }
        gpoly += sizeof(FaceGour4Lit);
        gprim += sizeof(POLY_G4);
    }
}

/* The original never loads RGBC here: the lighting uses whatever colour the GTE still holds. */
void draw_face_4pt_gour_dpq_lit(int count) /* 0x80057C58 */
{
    for (int i = 0; i < count; i++) {
        const FaceGour4Lit *f = (const FaceGour4Lit *)gpoly;
        POLY_G4 *p = (POLY_G4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_g3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                lit_colours3(f->n, &p->r0, &p->r1, &p->r2, 1);
                link_prim(p);
                gte_ldv0(&gnormals[f->n[3]]);
                gte_ncds();
                gte_strgb((CVECTOR *)&p->r3);
            }
        }
        gpoly += sizeof(FaceGour4Lit);
        gprim += sizeof(POLY_G4);
    }
}

/* Gouraud textured ("pict") triangles: GT3 records, or GT3-lit with normals. */
static void pict_tri(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FacePict3 *f = (const FacePict3 *)gpoly;
        POLY_GT3 *p = (POLY_GT3 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz;
            gte_stopz(&opz);
            gte_avsz3();
            if (opz < 1) {
                gte_stsxy3_gt3(p);
                link_prim(p);
                if (depth_cue) {
                    dpq_colour(f->rgbc[0], &p->r0);
                    dpq_colour(f->rgbc[1], &p->r1);
                    dpq_colour(f->rgbc[2], &p->r2);
                }
            }
        }
        gpoly += sizeof(FacePict3);
        gprim += sizeof(POLY_GT3);
    }
}

static void pict_tri_lit(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FacePict3Lit *f = (const FacePict3Lit *)gpoly;
        POLY_GT3 *p = (POLY_GT3 *)gprim;
        if (load_tri(f->v)) {
            int32_t opz;
            gte_stopz(&opz);
            gte_avsz3();
            if (opz < 1) {
                gte_stsxy3_gt3(p);
                link_prim(p);
                load_rgbc(f->rgbc);
                lit_colours3(f->n, &p->r0, &p->r1, &p->r2, depth_cue);
            }
        }
        gpoly += sizeof(FacePict3Lit);
        gprim += sizeof(POLY_GT3);
    }
}

void draw_face_3pt_pict(int count) { pict_tri(count, 0); }                /* 0x80058104 */
void draw_face_3pt_pict_lit(int count) { pict_tri_lit(count, 0); }        /* 0x80058274 */
void draw_face_3pt_pict_dpq(int count) { pict_tri(count, 1); }            /* 0x80058480 */
void draw_face_3pt_pict_dpq_lit(int count) { pict_tri_lit(count, 1); }    /* 0x80058658 */

/* Gouraud textured quads; only the first triangle is tested for facing. */
static void pict_quad(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FacePict4 *f = (const FacePict4 *)gpoly;
        POLY_GT4 *p = (POLY_GT4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_gt3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
                if (depth_cue) {
                    /* The original depth-cues the first colour for all four corners. */
                    dpq_colour(f->rgbc[0], &p->r0);
                    dpq_colour(f->rgbc[0], &p->r1);
                    dpq_colour(f->rgbc[0], &p->r2);
                    dpq_colour(f->rgbc[0], &p->r3);
                }
            }
        }
        gpoly += sizeof(FacePict4);
        gprim += sizeof(POLY_GT4);
    }
}

static void pict_quad_lit(int count, int depth_cue)
{
    for (int i = 0; i < count; i++) {
        const FacePict4Lit *f = (const FacePict4Lit *)gpoly;
        POLY_GT4 *p = (POLY_GT4 *)gprim;
        if (front_tri(f->v)) {
            gte_stsxy3_gt3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
                load_rgbc(f->rgbc);
                lit_colours3(f->n, &p->r0, &p->r1, &p->r2, depth_cue);
                gte_ldv0(&gnormals[f->n[3]]);
                if (depth_cue)
                    gte_ncds();
                else
                    gte_nccs();
                gte_strgb((CVECTOR *)&p->r3);
            }
        }
        gpoly += sizeof(FacePict4Lit);
        gprim += sizeof(POLY_GT4);
    }
}

void draw_face_4pt_pict(int count) { pict_quad(count, 0); }               /* 0x80058AA4 */
void draw_face_4pt_pict_lit(int count) { pict_quad_lit(count, 0); }       /* 0x80058C68 */
void draw_face_4pt_pict_dpq(int count) { pict_quad(count, 1); }           /* 0x80058EFC */
void draw_face_4pt_pict_dpq_lit(int count) { pict_quad_lit(count, 1); }   /* 0x80059140 */

/* ---- model sprites: textured quads that keep facing the camera ---- */

MATRIX sprite_matrix;
MATRIX tilt_matrix;

/* The original keeps 16 centres on the stack; the port has room for longer runs. */
#define MAX_SPRITE_RUN 256

/* Pass 1: each sprite's centre (average of its corners) goes through the current rotation
 * (or `centre_matrix`, the camera's) into view space. Pass 2: with `corner_matrix`, the corners are drawn
 * around each centre. The corner offsets are kept in one place, so every sprite of the run
 * takes the last sprite's shape, as in the original. */
static void sprite_run(int count, const MATRIX *centre_matrix, const MATRIX *corner_matrix, int depth_cue)
{
    static VECTOR centres[MAX_SPRITE_RUN];
    SVECTOR corner[4];
    if (count > MAX_SPRITE_RUN)
        count = MAX_SPRITE_RUN;
    if (centre_matrix)
        gte_SetRotMatrix(centre_matrix);
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)(gpoly + i * sizeof(FaceText));
        const SVECTOR *v[4] = {&gvertices[f->v[0]], &gvertices[f->v[1]], &gvertices[f->v[2]], &gvertices[f->v[3]]};
        int sx = v[0]->vx + v[1]->vx + v[2]->vx + v[3]->vx;
        int sy = v[0]->vy + v[1]->vy + v[2]->vy + v[3]->vy;
        int sz = v[0]->vz + v[1]->vz + v[2]->vz + v[3]->vz;
        SVECTOR centre = {(int16_t)(sx / 4), (int16_t)(sy / 4), (int16_t)(sz / 4), 0};
        for (int k = 0; k < 4; k++) {
            corner[k] = *v[k];
            corner[k].vx = (int16_t)(corner[k].vx - centre.vx);
            corner[k].vy = (int16_t)(corner[k].vy - centre.vy);
            corner[k].vz = (int16_t)(corner[k].vz - centre.vz);
        }
        gte_ldv0(&centre);
        gte_op_mvmva(1, 0, 0, 0, 0);
        gte_stlvnl(&centres[i]);
    }
    gte_SetRotMatrix(corner_matrix);
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        POLY_FT4 *p = (POLY_FT4 *)gprim;
        memcpy(gte.tr, &centres[i], sizeof(gte.tr));
        gte_ldv3(&corner[0], &corner[1], &corner[2]);
        gte_rtpt();
        int32_t flag;
        gte_stflg(&flag);
        if ((clip_flag_mask & (uint32_t)flag) == 0) {
            gte_stsxy3_ft3(p);
            gte_ldv0(&corner[3]);
            gte_rtps();
            gte_stflg(&flag);
            gte_avsz4();
            if ((clip_flag_mask & (uint32_t)flag) == 0) {
                gte_stsxy_xy3(p);
                link_prim(p);
                if (depth_cue)
                    dpq_colour(f->rgbc, &p->r0);
            }
        }
        gpoly += sizeof(FaceText);
        gprim += sizeof(POLY_FT4);
    }
}

void draw_face_sprite(int count) { sprite_run(count, NULL, &sprite_matrix, 0); }        /* 0x800594EC */
void draw_face_sprite_dpq(int count) { sprite_run(count, NULL, &sprite_matrix, 1); }    /* 0x800599AC */
void draw_face_centred_tilt_sprite(int count) /* 0x80059E94 */
{
    sprite_run(count, &world_matrix, &tilt_matrix, 0);
}
void draw_face_centred_tilt_sprite_dpq(int count) /* 0x8005A388 */
{
    sprite_run(count, &world_matrix, &tilt_matrix, 1);
}

/* Tilted sprites: the quad's own corners through the tilt rotation, without a facing test.
 * The rotation is left set for whatever the object draws next, as in the original. */
static void tilt_sprite(int count, int depth_cue)
{
    gte_SetRotMatrix(&tilt_matrix);
    for (int i = 0; i < count; i++) {
        const FaceText *f = (const FaceText *)gpoly;
        POLY_FT4 *p = (POLY_FT4 *)gprim;
        gte_ldv3(&gvertices[f->v[0]], &gvertices[f->v[1]], &gvertices[f->v[2]]);
        gte_rtpt();
        int32_t flag;
        gte_stflg(&flag);
        if ((clip_flag_mask & (uint32_t)flag) == 0) {
            gte_stsxy3_ft3(p);
            if (fourth_vertex(f->v[3])) {
                gte_stsxy_xy3(p);
                link_prim(p);
                if (depth_cue)
                    dpq_colour(f->rgbc, &p->r0);
            }
        }
        gpoly += sizeof(FaceText);
        gprim += sizeof(POLY_FT4);
    }
}

void draw_face_tilt_sprite(int count) { tilt_sprite(count, 0); }      /* 0x8005A8A4 */
void draw_face_tilt_sprite_dpq(int count) { tilt_sprite(count, 1); }  /* 0x8005AA88 */

/* 0x800720F4 */
const FaceHandler face_table_object[FACE_KINDS] = {
    draw_face_3pt_flat, draw_face_3pt_flat_lit, draw_face_3pt_flat_dpq, draw_face_3pt_flat_dpq_lit,
    draw_face_4pt_flat, draw_face_4pt_flat_lit, draw_face_4pt_flat_dpq, draw_face_4pt_flat_dpq_lit,
    draw_face_3pt_text, draw_face_3pt_text_lit, draw_face_3pt_text_dpq, draw_face_3pt_text_dpq_lit,
    draw_face_4pt_text, draw_face_4pt_text_lit, draw_face_4pt_text_dpq, draw_face_4pt_text_dpq_lit,
    draw_face_3pt_gour, draw_face_3pt_gour_lit, draw_face_3pt_gour_dpq, draw_face_3pt_gour_dpq_lit,
    draw_face_4pt_gour, draw_face_4pt_gour_lit, draw_face_4pt_gour_dpq, draw_face_4pt_gour_dpq_lit,
    draw_face_3pt_pict, draw_face_3pt_pict_lit, draw_face_3pt_pict_dpq, draw_face_3pt_pict_dpq_lit,
    draw_face_4pt_pict, draw_face_4pt_pict_lit, draw_face_4pt_pict_dpq, draw_face_4pt_pict_dpq_lit,
    draw_face_sprite, draw_face_sprite, draw_face_sprite_dpq, draw_face_sprite_dpq,
    draw_face_centred_tilt_sprite, draw_face_centred_tilt_sprite,
    draw_face_centred_tilt_sprite_dpq, draw_face_centred_tilt_sprite_dpq,
    draw_face_tilt_sprite, draw_face_tilt_sprite, draw_face_tilt_sprite_dpq, draw_face_tilt_sprite_dpq,
};

/* 0x80072044: as the object table, but unlit textured faces use the near-plane squash. */
const FaceHandler face_table_subdiv[FACE_KINDS] = {
    draw_face_3pt_flat, draw_face_3pt_flat_lit, draw_face_3pt_flat_dpq, draw_face_3pt_flat_dpq_lit,
    draw_face_4pt_flat, draw_face_4pt_flat_lit, draw_face_4pt_flat_dpq, draw_face_4pt_flat_dpq_lit,
    draw_face_3pt_text_squash, draw_face_3pt_text_lit, draw_face_3pt_text_dpq_squash, draw_face_3pt_text_dpq_lit,
    draw_face_4pt_text_squash, draw_face_4pt_text_lit, draw_face_4pt_text_dpq_squash, draw_face_4pt_text_dpq_lit,
    draw_face_3pt_gour, draw_face_3pt_gour_lit, draw_face_3pt_gour_dpq, draw_face_3pt_gour_dpq_lit,
    draw_face_4pt_gour, draw_face_4pt_gour_lit, draw_face_4pt_gour_dpq, draw_face_4pt_gour_dpq_lit,
    draw_face_3pt_pict, draw_face_3pt_pict_lit, draw_face_3pt_pict_dpq, draw_face_3pt_pict_dpq_lit,
    draw_face_4pt_pict, draw_face_4pt_pict_lit, draw_face_4pt_pict_dpq, draw_face_4pt_pict_dpq_lit,
    draw_face_sprite, draw_face_sprite, draw_face_sprite_dpq, draw_face_sprite_dpq,
    draw_face_centred_tilt_sprite, draw_face_centred_tilt_sprite,
    draw_face_centred_tilt_sprite_dpq, draw_face_centred_tilt_sprite_dpq,
    draw_face_tilt_sprite, draw_face_tilt_sprite, draw_face_tilt_sprite_dpq, draw_face_tilt_sprite_dpq,
};

/* 0x800721A4: kinds pair up as (plain, lit, depth-cued, depth-cued lit) per primitive type. */
const FaceHandler face_table_build[FACE_KINDS] = {
    convert_poly_f3, convert_poly_f3, convert_poly_f3, convert_poly_f3,
    convert_poly_f4, convert_poly_f4, convert_poly_f4, convert_poly_f4,
    convert_poly_ft3, convert_poly_ft3, convert_poly_ft3, convert_poly_ft3,
    convert_poly_ft4, convert_poly_ft4, convert_poly_ft4, convert_poly_ft4,
    convert_poly_g3, convert_poly_g3_lit, convert_poly_g3, convert_poly_g3_lit,
    convert_poly_g4, convert_poly_g4_lit, convert_poly_g4, convert_poly_g4_lit,
    convert_poly_gt3, convert_poly_gt3_lit, convert_poly_gt3, convert_poly_gt3_lit,
    convert_poly_gt4, convert_poly_gt4_lit, convert_poly_gt4, convert_poly_gt4_lit,
    convert_poly_sprite, convert_poly_sprite, convert_poly_sprite, convert_poly_sprite,
    convert_poly_sprite, convert_poly_sprite, convert_poly_sprite, convert_poly_sprite,
    convert_poly_sprite, convert_poly_sprite, convert_poly_sprite, convert_poly_sprite,
};

size_t face_packet_size(int kind)
{
    static const size_t sizes[8] = {
        sizeof(POLY_F3), sizeof(POLY_F4), sizeof(POLY_FT3), sizeof(POLY_FT4),
        sizeof(POLY_G3), sizeof(POLY_G4), sizeof(POLY_GT3), sizeof(POLY_GT4),
    };
    return kind < 32 ? sizes[kind >> 2] : sizeof(POLY_FT4);
}
