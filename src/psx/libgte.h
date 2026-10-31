/* libgte replacement: the Psy-Q geometry library on top of the GTE emulator (psx/gte.c).
 * Psy-Q's `long` is 32 bits; it is int32_t here so layouts and pointers match on every system. */
#ifndef PSX_LIBGTE_H
#define PSX_LIBGTE_H

#include "psx/gte.h"
#include "psx/libgpu.h"

#include <stdint.h>

#define ONE 4096 /* 1.0 in the GTE's 4.12 fixed point */

typedef struct {
    int16_t m[3][3];
    int32_t t[3];
} MATRIX;

typedef struct {
    int32_t vx, vy, vz, pad;
} VECTOR;

typedef struct {
    int16_t vx, vy, vz, pad;
} SVECTOR;

typedef struct {
    uint8_t r, g, b, cd;
} CVECTOR;

_Static_assert(sizeof(MATRIX) == 32, "MATRIX has the Psy-Q layout");

void InitGeom(void);
int32_t SquareRoot0(int32_t a);
int rsin(int a);
int rcos(int a);

void SetGeomOffset(int32_t ofx, int32_t ofy);
void SetGeomScreen(int32_t h);
void SetBackColor(int32_t rbk, int32_t gbk, int32_t bbk);
void SetFarColor(int32_t rfc, int32_t gfc, int32_t bfc);
void SetFogNearFar(int32_t a, int32_t b, int32_t h);

void SetRotMatrix(const MATRIX *m);
void SetTransMatrix(const MATRIX *m);
void SetLightMatrix(const MATRIX *m);
void SetColorMatrix(const MATRIX *m);
void PushMatrix(void);
void PopMatrix(void);

MATRIX *RotMatrixX(int32_t r, MATRIX *m);
MATRIX *RotMatrixY(int32_t r, MATRIX *m);
MATRIX *RotMatrixZ(int32_t r, MATRIX *m);
MATRIX *MulMatrix0(const MATRIX *m0, const MATRIX *m1, MATRIX *m2); /* m2 = m0 * m1 */
MATRIX *MulMatrix(MATRIX *m0, const MATRIX *m1);                    /* m0 = m0 * m1 */
MATRIX *MulMatrix2(const MATRIX *m0, MATRIX *m1);                   /* m1 = m0 * m1 */
VECTOR *ApplyMatrixLV(const MATRIX *m, const VECTOR *v0, VECTOR *v1);
void OuterProduct12(const VECTOR *v0, const VECTOR *v1, VECTOR *v2);
int32_t VectorNormal(const VECTOR *v0, VECTOR *v1);

void RotTrans(const SVECTOR *v0, VECTOR *v1, int32_t *flag);
int32_t RotTransPers(const SVECTOR *v0, int32_t *sxy, int32_t *p, int32_t *flag);
void gteMIMefunc(SVECTOR *otp, const SVECTOR *dfp, int32_t n, int32_t p);

/* Register macros (Psy-Q inline_c.h). Instructions run with sf = 1; lighting ones with lm = 1. */
static inline void gte_ldv0(const SVECTOR *v) { gte.vx[0] = v->vx; gte.vy[0] = v->vy; gte.vz[0] = v->vz; }
static inline void gte_ldv1(const SVECTOR *v) { gte.vx[1] = v->vx; gte.vy[1] = v->vy; gte.vz[1] = v->vz; }
static inline void gte_ldv2(const SVECTOR *v) { gte.vx[2] = v->vx; gte.vy[2] = v->vy; gte.vz[2] = v->vz; }
static inline void gte_ldv3(const SVECTOR *a, const SVECTOR *b, const SVECTOR *c) { gte_ldv0(a); gte_ldv1(b); gte_ldv2(c); }
static inline void gte_ldrgb(const CVECTOR *c) { gte.rgbc[0] = c->r; gte.rgbc[1] = c->g; gte.rgbc[2] = c->b; gte.rgbc[3] = c->cd; }
static inline void gte_SetRotMatrix(const MATRIX *m) { SetRotMatrix(m); }
static inline void gte_SetTransMatrix(const MATRIX *m) { SetTransMatrix(m); }
static inline void gte_ldtr(int32_t x, int32_t y, int32_t z) { gte.tr[0] = x; gte.tr[1] = y; gte.tr[2] = z; }

static inline void gte_rtps(void) { gte_op_rtps(1); }
static inline void gte_rtpt(void) { gte_op_rtpt(1); }
static inline void gte_rt(void) { gte_op_mvmva(1, 0, 0, 0, 0); } /* V0 rotated and translated, no projection */
static inline void gte_rtv0(void) { gte_op_mvmva(1, 0, 0, 0, 3); } /* V0 rotated only */
static inline void gte_nclip(void) { gte_op_nclip(); }
static inline void gte_avsz3(void) { gte_op_avsz3(); }
static inline void gte_avsz4(void) { gte_op_avsz4(); }
static inline void gte_ncs(void) { gte_op_ncs(1); }
static inline void gte_nccs(void) { gte_op_nccs(1); }
static inline void gte_ncct(void) { gte_op_ncct(1); }
static inline void gte_ncds(void) { gte_op_ncds(1); }
static inline void gte_ncdt(void) { gte_op_ncdt(1); }
static inline void gte_dpcs(void) { gte_op_dpcs(1); }
static inline void gte_dpct(void) { gte_op_dpct(1); }

/* Colour FIFO loads and stores. */
static inline void gte_ldrgb3(const CVECTOR *a, const CVECTOR *b, const CVECTOR *c)
{
    const CVECTOR *src[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        gte.rgb_fifo[i][0] = src[i]->r;
        gte.rgb_fifo[i][1] = src[i]->g;
        gte.rgb_fifo[i][2] = src[i]->b;
        gte.rgb_fifo[i][3] = src[i]->cd;
    }
}

static inline void gte_strgb3(CVECTOR *a, CVECTOR *b, CVECTOR *c)
{
    CVECTOR *dst[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        dst[i]->r = gte.rgb_fifo[i][0];
        dst[i]->g = gte.rgb_fifo[i][1];
        dst[i]->b = gte.rgb_fifo[i][2];
        dst[i]->cd = gte.rgb_fifo[i][3];
    }
}

static inline int32_t gte_sxy(int i) { return (int32_t)((uint32_t)(uint16_t)gte.sx[i] | (uint32_t)gte.sy[i] << 16); }
static inline void gte_stsxy(int32_t *p) { *p = gte_sxy(2); }
static inline void gte_stsxy3(int32_t *a, int32_t *b, int32_t *c) { *a = gte_sxy(0); *b = gte_sxy(1); *c = gte_sxy(2); }
static inline void gte_stsz(int32_t *p) { *p = gte.sz[3]; }
static inline void gte_stotz(int32_t *p) { *p = gte.otz; }
static inline void gte_stopz(int32_t *p) { *p = gte.mac[0]; }
static inline void gte_stflg(int32_t *p) { *p = (int32_t)gte.flag; }
static inline void gte_stlvnl(VECTOR *v) { v->vx = gte.mac[1]; v->vy = gte.mac[2]; v->vz = gte.mac[3]; }
static inline void gte_stlvl(VECTOR *v) { v->vx = gte.ir[1]; v->vy = gte.ir[2]; v->vz = gte.ir[3]; }
static inline void gte_strgb(CVECTOR *c) { c->r = gte.rgb_fifo[2][0]; c->g = gte.rgb_fifo[2][1]; c->b = gte.rgb_fifo[2][2]; c->cd = gte.rgb_fifo[2][3]; }

/* The screen coordinates of the last RTPT into a primitive's three vertices. */
#define gte_stsxy3_xy(p)                                                                   \
    ((p)->x0 = gte.sx[0], (p)->y0 = gte.sy[0], (p)->x1 = gte.sx[1], (p)->y1 = gte.sy[1], \
     (p)->x2 = gte.sx[2], (p)->y2 = gte.sy[2])
#define gte_stsxy3_f3(p) gte_stsxy3_xy(p)
#define gte_stsxy3_ft3(p) gte_stsxy3_xy(p)
#define gte_stsxy3_g3(p) gte_stsxy3_xy(p)
#define gte_stsxy3_gt3(p) gte_stsxy3_xy(p)
/* The screen coordinates of the last RTPS into a quad's fourth vertex. */
#define gte_stsxy_xy3(p) ((p)->x3 = gte.sx[2], (p)->y3 = gte.sy[2])

#endif
