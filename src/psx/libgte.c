#include "psx/libgte.h"

#include <string.h>

extern const int16_t rsin_table[0x401];

#define MATRIX_STACK_DEPTH 20

static MATRIX matrix_stack[MATRIX_STACK_DEPTH];
static int matrix_sp;

/* Psy-Q's defaults: OTZ = average Z / 4, projection distance 1000. */
void InitGeom(void)
{
    memset(&gte, 0, sizeof(gte));
    gte.zsf3 = 0x155;
    gte.zsf4 = 0x100;
    gte.h = 1000;
    gte.dqa = -0x1062;
    gte.dqb = 0x1400000;
    matrix_sp = 0;
}

/* Floor of the square root. */
int32_t SquareRoot0(int32_t a)
{
    if (a <= 0)
        return 0;
    uint32_t x = (uint32_t)a, r = 0, bit = 1u << 30;
    while (bit > x)
        bit >>= 2;
    while (bit) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)r;
}

/* Angles are 4096 to a turn; the table holds the first quarter. */
static int sin_quadrants(int a)
{
    if (a <= 0x800)
        return a <= 0x400 ? rsin_table[a] : rsin_table[0x800 - a];
    return a <= 0xc00 ? -rsin_table[a - 0x800] : -rsin_table[0x1000 - a];
}

int rsin(int a)
{
    return a < 0 ? -sin_quadrants(-a & 0xfff) : sin_quadrants(a & 0xfff);
}

int rcos(int a)
{
    if (a < 0)
        a = -a;
    a &= 0xfff;
    if (a <= 0x800)
        return a <= 0x400 ? rsin_table[0x400 - a] : -rsin_table[a - 0x400];
    return a <= 0xc00 ? -rsin_table[0xc00 - a] : rsin_table[a - 0xc00];
}

void SetGeomOffset(int32_t ofx, int32_t ofy)
{
    gte.ofx = ofx << 16;
    gte.ofy = ofy << 16;
}

void SetGeomScreen(int32_t h)
{
    gte.h = (uint16_t)h;
}

/* Colours are given 0-255 and kept as the GTE's 12-bit fraction registers. */
void SetBackColor(int32_t rbk, int32_t gbk, int32_t bbk)
{
    gte.bk[0] = rbk << 4;
    gte.bk[1] = gbk << 4;
    gte.bk[2] = bbk << 4;
}

void SetFarColor(int32_t rfc, int32_t gfc, int32_t bfc)
{
    gte.fc[0] = rfc << 4;
    gte.fc[1] = gfc << 4;
    gte.fc[2] = bfc << 4;
}

/* Fog from distance a (none) to b (full), for projection distance h; ignored when b - a < 100. */
void SetFogNearFar(int32_t a, int32_t b, int32_t h)
{
    if (b - a < 100)
        return;
    int64_t dqa = (int64_t)(-a) * b / (b - a) * 256 / h;
    if (dqa < -32768)
        dqa = -32768;
    if (dqa > 32767)
        dqa = 32767;
    gte.dqa = (int16_t)dqa;
    gte.dqb = (int32_t)(((int64_t)b << 12) / (b - a) << 12);
}

void SetRotMatrix(const MATRIX *m)
{
    memcpy(gte.rt, m->m, sizeof(gte.rt));
}

void SetTransMatrix(const MATRIX *m)
{
    memcpy(gte.tr, m->t, sizeof(gte.tr));
}

void SetLightMatrix(const MATRIX *m)
{
    memcpy(gte.llm, m->m, sizeof(gte.llm));
}

void SetColorMatrix(const MATRIX *m)
{
    memcpy(gte.lcm, m->m, sizeof(gte.lcm));
}

/* Saves and restores the GTE's rotation and translation. */
void PushMatrix(void)
{
    if (matrix_sp == MATRIX_STACK_DEPTH)
        return;
    MATRIX *m = &matrix_stack[matrix_sp++];
    memcpy(m->m, gte.rt, sizeof(gte.rt));
    memcpy(m->t, gte.tr, sizeof(gte.tr));
}

void PopMatrix(void)
{
    if (matrix_sp == 0)
        return;
    MATRIX *m = &matrix_stack[--matrix_sp];
    memcpy(gte.rt, m->m, sizeof(gte.rt));
    memcpy(gte.tr, m->t, sizeof(gte.tr));
}

static int16_t fix12(int64_t v)
{
    v >>= 12;
    return (int16_t)(v < -0x8000 ? -0x8000 : v > 0x7fff ? 0x7fff : v);
}

/* The rotations multiply from the left: m = R * m. */
MATRIX *RotMatrixX(int32_t r, MATRIX *m)
{
    int64_t s = rsin(r), c = rcos(r);
    for (int j = 0; j < 3; j++) {
        int64_t a = m->m[1][j], b = m->m[2][j];
        m->m[1][j] = fix12(c * a - s * b);
        m->m[2][j] = fix12(s * a + c * b);
    }
    return m;
}

MATRIX *RotMatrixY(int32_t r, MATRIX *m)
{
    int64_t s = rsin(r), c = rcos(r);
    for (int j = 0; j < 3; j++) {
        int64_t a = m->m[0][j], b = m->m[2][j];
        m->m[0][j] = fix12(c * a + s * b);
        m->m[2][j] = fix12(-s * a + c * b);
    }
    return m;
}

MATRIX *RotMatrixZ(int32_t r, MATRIX *m)
{
    int64_t s = rsin(r), c = rcos(r);
    for (int j = 0; j < 3; j++) {
        int64_t a = m->m[0][j], b = m->m[1][j];
        m->m[0][j] = fix12(c * a - s * b);
        m->m[1][j] = fix12(s * a + c * b);
    }
    return m;
}

/* Psy-Q computes the products on the GTE with m0 loaded as its rotation matrix, and leaves it there;
 * game code relies on that (Set_Object_View without a rotation, the wheels of Draw_Car). */
static void load_rotation(const MATRIX *m)
{
    memcpy(gte.rt, m->m, sizeof(gte.rt));
}

MATRIX *MulMatrix0(const MATRIX *m0, const MATRIX *m1, MATRIX *m2)
{
    load_rotation(m0);
    int16_t r[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i][j] = fix12((int64_t)m0->m[i][0] * m1->m[0][j] + (int64_t)m0->m[i][1] * m1->m[1][j] +
                            (int64_t)m0->m[i][2] * m1->m[2][j]);
    memcpy(m2->m, r, sizeof(r));
    return m2;
}

MATRIX *MulMatrix(MATRIX *m0, const MATRIX *m1)
{
    return MulMatrix0(m0, m1, m0);
}

MATRIX *MulMatrix2(const MATRIX *m0, MATRIX *m1)
{
    return MulMatrix0(m0, m1, m1);
}

VECTOR *ApplyMatrixLV(const MATRIX *m, const VECTOR *v0, VECTOR *v1)
{
    load_rotation(m);
    int32_t r[3];
    for (int i = 0; i < 3; i++)
        r[i] = (int32_t)(((int64_t)m->m[i][0] * v0->vx + (int64_t)m->m[i][1] * v0->vy + (int64_t)m->m[i][2] * v0->vz) >> 12);
    v1->vx = r[0];
    v1->vy = r[1];
    v1->vz = r[2];
    return v1;
}

void OuterProduct12(const VECTOR *v0, const VECTOR *v1, VECTOR *v2)
{
    int32_t x = (int32_t)(((int64_t)v0->vy * v1->vz - (int64_t)v0->vz * v1->vy) >> 12);
    int32_t y = (int32_t)(((int64_t)v0->vz * v1->vx - (int64_t)v0->vx * v1->vz) >> 12);
    int32_t z = (int32_t)(((int64_t)v0->vx * v1->vy - (int64_t)v0->vy * v1->vx) >> 12);
    v2->vx = x;
    v2->vy = y;
    v2->vz = z;
}

/* Scales v0 to length 4096; returns the squared length. */
int32_t VectorNormal(const VECTOR *v0, VECTOR *v1)
{
    int64_t sq = (int64_t)v0->vx * v0->vx + (int64_t)v0->vy * v0->vy + (int64_t)v0->vz * v0->vz;
    int64_t len = 1;
    while (len * len < sq)
        len <<= 1;
    for (int64_t step = len >> 1; step > 0; step >>= 1)
        if ((len - step) * (len - step) >= sq)
            len -= step;
    if (len == 0)
        len = 1;
    v1->vx = (int32_t)((int64_t)v0->vx * 4096 / len);
    v1->vy = (int32_t)((int64_t)v0->vy * 4096 / len);
    v1->vz = (int32_t)((int64_t)v0->vz * 4096 / len);
    return (int32_t)(sq > 0x7fffffff ? 0x7fffffff : sq);
}

void RotTrans(const SVECTOR *v0, VECTOR *v1, int32_t *flag)
{
    gte_ldv0(v0);
    gte_op_mvmva(1, 0, 0, 0, 0);
    gte_stlvnl(v1);
    *flag = (int32_t)gte.flag;
}

/* Projects v0; returns OTZ (SZ / 4) like the library. */
int32_t RotTransPers(const SVECTOR *v0, int32_t *sxy, int32_t *p, int32_t *flag)
{
    gte_ldv0(v0);
    gte_rtps();
    gte_stsxy(sxy);
    *p = gte.ir[0];
    *flag = (int32_t)gte.flag;
    return gte.sz[3] >> 2;
}

/* Vertex morphing: otp[i] += dfp[i] * p / 4096. */
void gteMIMefunc(SVECTOR *otp, const SVECTOR *dfp, int32_t n, int32_t p)
{
    for (int32_t i = 0; i < n; i++) {
        otp[i].vx = (int16_t)(otp[i].vx + ((dfp[i].vx * p) >> 12));
        otp[i].vy = (int16_t)(otp[i].vy + ((dfp[i].vy * p) >> 12));
        otp[i].vz = (int16_t)(otp[i].vz + ((dfp[i].vz * p) >> 12));
    }
}
