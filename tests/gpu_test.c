/* Checks of the software GPU against PSX rules. Usage: gpu_test [out.bmp]
 * Exits non-zero on the first failure; the optional BMP shows the test area for a look. */
#include "psx/libgpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
            failures++;                                     \
        }                                                   \
    } while (0)

static u_short px(int x, int y)
{
    return gpu_vram()[y * VRAM_WIDTH + x];
}

static u_short rgb(int r, int g, int b)
{
    return (u_short)(r | g << 5 | b << 10);
}

static int count_pixels(int x0, int y0, int w, int h, u_short value)
{
    int n = 0;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            n += px(x, y) == value;
    return n;
}

static void set_env(int x, int y, int w, int h, int ofs_x, int ofs_y)
{
    DRAWENV env;
    SetDefDrawEnv(&env, x, y, w, h);
    env.ofs[0] = (short)ofs_x;
    env.ofs[1] = (short)ofs_y;
    env.dtd = 0;
    PutDrawEnv(&env);
}

static void clear(void)
{
    RECT all = {0, 0, VRAM_WIDTH, VRAM_HEIGHT};
    ClearImage(&all, 0, 0, 0);
}

static void test_fill_rule(void)
{
    clear();
    set_env(0, 0, 320, 240, 0, 0);
    POLY_F3 tri;
    setPolyF3(&tri);
    setRGB0(&tri, 255, 255, 255);
    setXY3(&tri, 0, 0, 10, 0, 0, 10);
    DrawPrim(&tri);
    /* x + y < 10: the hypotenuse is a right/bottom edge and stays empty */
    CHECK(count_pixels(0, 0, 16, 16, rgb(31, 31, 31)) == 55, "triangle covers %d pixels, want 55",
          count_pixels(0, 0, 16, 16, rgb(31, 31, 31)));

    clear();
    POLY_F4 quad;
    setPolyF4(&quad);
    setRGB0(&quad, 255, 0, 0);
    setXYWH(&quad, 20, 20, 8, 8);
    DrawPrim(&quad);
    CHECK(count_pixels(16, 16, 16, 16, rgb(31, 0, 0)) == 64, "8x8 quad covers %d pixels, want 64",
          count_pixels(16, 16, 16, 16, rgb(31, 0, 0)));
}

/* A 16x16 4-bit texture at VRAM (640, 0) and a 16-colour CLUT at (0, 480). */
static void load_texture(void)
{
    u_short clut[16];
    for (int i = 0; i < 16; i++)
        clut[i] = i == 0 ? 0 : rgb(i * 2, 31 - i * 2, 16);
    RECT cr = {0, 480, 16, 1};
    LoadImage(&cr, (u_long *)clut);
    u_short tex[16][4];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 4; x++) {
            int idx[4];
            for (int k = 0; k < 4; k++)
                idx[k] = ((x * 4 + k) + y) & 15;
            tex[y][x] = (u_short)(idx[0] | idx[1] << 4 | idx[2] << 8 | idx[3] << 12);
        }
    RECT tr = {640, 0, 4, 16};
    LoadImage(&tr, (u_long *)tex);
}

static void test_textures(void)
{
    clear();
    load_texture();
    set_env(0, 0, 320, 240, 0, 0);
    u_short clut = GetClut(0, 480);
    u_short tpage = GetTPage(0, 0, 640, 0);

    DR_TPAGE mode;
    SetDrawTPage(&mode, 0, 0, tpage);
    DrawPrim(&mode);
    SPRT sp;
    setSprt(&sp);
    setShadeTex(&sp, 1);
    setXY0(&sp, 100, 100);
    setUV0(&sp, 0, 0);
    setWH(&sp, 16, 16);
    sp.clut = clut;
    DrawPrim(&sp);
    int bad = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int idx = (x + y) & 15;
            u_short want = idx == 0 ? 0 : rgb(idx * 2, 31 - idx * 2, 16);
            bad += px(100 + x, 100 + y) != want;
        }
    CHECK(bad == 0, "raw 4-bit sprite has %d wrong pixels", bad);

    /* Modulation by 128 is identity; the texture-mapped quad must match the sprite. */
    POLY_FT4 ft;
    setPolyFT4(&ft);
    setRGB0(&ft, 128, 128, 128);
    setXYWH(&ft, 140, 100, 16, 16);
    setUVWH(&ft, 0, 0, 16, 16);
    ft.clut = clut;
    ft.tpage = tpage;
    DrawPrim(&ft);
    bad = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            bad += px(140 + x, 100 + y) != px(100 + x, 100 + y);
    CHECK(bad == 0, "modulated quad differs from the raw sprite in %d pixels", bad);
}

static void test_semi_and_lines(void)
{
    clear();
    set_env(0, 0, 320, 240, 0, 0);
    TILE base;
    setTile(&base);
    setRGB0(&base, 80, 80, 80);
    setXY0(&base, 10, 10);
    setWH(&base, 8, 8);
    DrawPrim(&base);
    DR_TPAGE add;
    SetDrawTPage(&add, 0, 0, GetTPage(0, 1, 0, 0)); /* mode 1: background + foreground */
    DrawPrim(&add);
    TILE over = base;
    setSemiTrans(&over, 1);
    setRGB0(&over, 40, 0, 0);
    DrawPrim(&over);
    CHECK(px(12, 12) == rgb(10 + 5, 10, 10), "additive blend gives %04x, want %04x", px(12, 12), rgb(15, 10, 10));

    LINE_G2 ln;
    setLineG2(&ln);
    setRGB0(&ln, 255, 0, 0);
    setRGB1(&ln, 0, 0, 255);
    setXY2(&ln, 50, 50, 70, 50);
    DrawPrim(&ln);
    CHECK(px(50, 50) == rgb(31, 0, 0) && px(70, 50) == rgb(0, 0, 31), "line ends %04x %04x", px(50, 50), px(70, 50));
}

static void test_offset_and_clip(void)
{
    clear();
    set_env(0, 256, 320, 240, 0, 256); /* the second display buffer */
    TILE t;
    setTile(&t);
    setRGB0(&t, 0, 255, 0);
    setXY0(&t, -4, 10);
    setWH(&t, 8, 8);
    DrawPrim(&t);
    CHECK(count_pixels(0, 256 + 10, 8, 8, rgb(0, 31, 0)) == 32, "clipped tile covers %d pixels, want 32",
          count_pixels(0, 256 + 10, 8, 8, rgb(0, 31, 0)));
    CHECK(px(0, 10) == 0, "offset ignored: the tile landed in the first buffer");
}

int main(int argc, char *argv[])
{
    ResetGraph(0);
    test_fill_rule();
    test_offset_and_clip();
    test_semi_and_lines();
    test_textures();
    if (argc > 1) {
        DISPENV disp;
        PutDispEnv(SetDefDispEnv(&disp, 0, 0, 320, 240));
        SetDispMask(1);
        gpu_save_display(argv[1]);
    }
    printf(failures ? "%d failure(s)\n" : "all GPU checks passed\n", failures);
    return failures ? 1 : 0;
}
