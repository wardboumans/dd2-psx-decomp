/* Checks of the GTE emulator and libgte against PSX rules. */
#include "psx/libgte.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

static const MATRIX identity = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};

static void test_trig(void)
{
    CHECK(rsin(0) == 0 && rsin(1024) == 4096 && rsin(2048) == 0 && rsin(3072) == -4096, "rsin quadrants");
    CHECK(rcos(0) == 4096 && rcos(1024) == 0 && rcos(2048) == -4096 && rcos(-1024) == 0, "rcos quadrants");
    CHECK(rsin(-512) == -rsin(512) && rsin(4096 + 300) == rsin(300), "rsin symmetry and wrap");
}

static void test_projection(void)
{
    InitGeom();
    SetGeomOffset(160, 120);
    SetGeomScreen(1000);
    SetRotMatrix(&identity);
    SetTransMatrix(&identity);
    SVECTOR v = {100, 50, 1000, 0};
    int32_t sxy, p, flag;
    int32_t otz = RotTransPers(&v, &sxy, &p, &flag);
    CHECK((int16_t)sxy == 260 && (sxy >> 16) == 170, "projected to (%d, %d), want (260, 170)", (int16_t)sxy, sxy >> 16);
    CHECK(gte.sz[3] == 1000 && otz == 250, "sz %d otz %d", gte.sz[3], otz);
    CHECK((flag & GTE_FLAG_ERROR) == 0, "flag %08x", (unsigned)flag);

    /* Closer than h/2: the divide saturates and flags it, which the game culls on. */
    SVECTOR near_v = {0, 0, 400, 0};
    RotTransPers(&near_v, &sxy, &p, &flag);
    CHECK(flag & GTE_FLAG_DIVIDE, "near vertex did not flag a divide overflow");
}

static void test_nclip_avsz(void)
{
    InitGeom();
    SetGeomOffset(0, 0);
    SetGeomScreen(1000);
    SetRotMatrix(&identity);
    SetTransMatrix(&identity);
    SVECTOR a = {0, 0, 1000, 0}, b = {100, 0, 2000, 0}, c = {0, 100, 3000, 0};
    gte_ldv3(&a, &b, &c);
    gte_rtpt();
    gte_nclip();
    int32_t opz;
    gte_stopz(&opz);
    gte_ldv3(&a, &c, &b);
    gte_rtpt();
    gte_nclip();
    int32_t opz_reversed;
    gte_stopz(&opz_reversed);
    CHECK(opz != 0 && opz == -opz_reversed, "nclip %d vs reversed %d", opz, opz_reversed);
    gte_avsz3();
    int32_t otz;
    gte_stotz(&otz);
    /* ZSF3 = 0x155 approximates 1/12 and the GTE truncates: not exactly 6000 / 12 = 500 */
    CHECK(otz == (0x155 * 6000) >> 12, "avsz3 otz %d, want %d", otz, (0x155 * 6000) >> 12);
}

static void test_matrices(void)
{
    MATRIX m = identity;
    RotMatrixZ(1024, &m); /* a quarter turn */
    CHECK(m.m[0][0] == 0 && m.m[0][1] == -4096 && m.m[1][0] == 4096 && m.m[1][1] == 0, "RotMatrixZ quarter turn");
    MATRIX r;
    MulMatrix0(&m, &m, &r);
    CHECK(r.m[0][0] == -4096 && r.m[1][1] == -4096, "two quarter turns make a half turn");
    VECTOR x = {4096, 0, 0, 0}, y = {0, 4096, 0, 0}, z;
    OuterProduct12(&x, &y, &z);
    CHECK(z.vx == 0 && z.vy == 0 && z.vz == 4096, "x cross y = z");
    CHECK(SquareRoot0(1000000) == 1000 && SquareRoot0(999999) == 999, "SquareRoot0");
}

int main(void)
{
    test_trig();
    test_projection();
    test_nclip_avsz();
    test_matrices();
    printf(failures ? "%d failure(s)\n" : "all GTE checks passed\n", failures);
    return failures ? 1 : 0;
}
