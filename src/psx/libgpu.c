#include "psx/libgpu.h"
#include "platform/platform.h"
#include "psx/gpu.h"

#include <string.h>

/* Pixels are PSX 15-bit: bits 0-4 red, 5-9 green, 10-14 blue, bit 15 mask/semi-transparency. */
static u_short vram[VRAM_HEIGHT][VRAM_WIDTH];
static DISPENV disp_env;
static DRAWENV draw_env;
static int display_enabled;

u_short *gpu_vram(void)
{
    return &vram[0][0];
}

int ResetGraph(int mode)
{
    if (mode == 0 || mode == 3) {
        memset(vram, 0, sizeof(vram));
        memset(&disp_env, 0, sizeof(disp_env));
        memset(&draw_env, 0, sizeof(draw_env));
        display_enabled = 0;
    }
    gpu_reset();
    return 0;
}

int SetGraphDebug(int level)
{
    (void)level;
    return 0;
}

/* 0: the retail GPU. The game has a different texture page layout for types 1 and 2. */
int GetGraphType(void)
{
    return 0;
}

void SetDispMask(int mask)
{
    display_enabled = mask != 0;
}

int DrawSync(int mode)
{
    /* Drawing is synchronous here, so nothing is ever pending. */
    (void)mode;
    return 0;
}

int ClearImage(RECT *rect, u_char r, u_char g, u_char b)
{
    u_short pixel = (u_short)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
    for (int y = 0; y < rect->h; y++) {
        u_short *row = vram[(rect->y + y) & (VRAM_HEIGHT - 1)];
        for (int x = 0; x < rect->w; x++)
            row[(rect->x + x) & (VRAM_WIDTH - 1)] = pixel;
    }
    return 0;
}

int LoadImage(RECT *rect, u_long *p)
{
    const u_short *src = (const u_short *)p;
    for (int y = 0; y < rect->h; y++) {
        u_short *row = vram[(rect->y + y) & (VRAM_HEIGHT - 1)];
        for (int x = 0; x < rect->w; x++)
            row[(rect->x + x) & (VRAM_WIDTH - 1)] = *src++;
    }
    return 0;
}

int StoreImage(RECT *rect, u_long *p)
{
    u_short *dst = (u_short *)p;
    for (int y = 0; y < rect->h; y++) {
        const u_short *row = vram[(rect->y + y) & (VRAM_HEIGHT - 1)];
        for (int x = 0; x < rect->w; x++)
            *dst++ = row[(rect->x + x) & (VRAM_WIDTH - 1)];
    }
    return 0;
}

int MoveImage(RECT *rect, int x, int y)
{
    /* Source and destination may overlap, so copy through a temporary row. */
    u_short line[VRAM_WIDTH];
    for (int row = 0; row < rect->h; row++) {
        const u_short *src = vram[(rect->y + row) & (VRAM_HEIGHT - 1)];
        u_short *dst = vram[(y + row) & (VRAM_HEIGHT - 1)];
        for (int col = 0; col < rect->w; col++)
            line[col] = src[(rect->x + col) & (VRAM_WIDTH - 1)];
        for (int col = 0; col < rect->w; col++)
            dst[(x + col) & (VRAM_WIDTH - 1)] = line[col];
    }
    return 0;
}

OT_TAG *ClearOTagR(OT_TAG *ot, int n)
{
    for (int i = 0; i < n; i++) {
        ot[i].next = i > 0 ? &ot[i - 1] : NULL;
        ot[i].len = 0;
    }
    return ot;
}

/* A packet's GPU command words follow its tag. */
static void run_packet(const OT_TAG *p)
{
    gpu_gp0((const u_int *)((const u_char *)p + sizeof(OT_TAG)), (int)p->len);
}

void DrawOTag(OT_TAG *p)
{
    for (; p; p = p->next)
        if (p->len)
            run_packet(p);
}

void DrawPrim(void *p)
{
    run_packet((const OT_TAG *)p);
}

u_short GetTPage(int tp, int abr, int x, int y)
{
    return getTPage(tp, abr, x, y);
}

u_short GetClut(int x, int y)
{
    return getClut(x, y);
}

static u_int texture_window(const RECT *tw)
{
    if (!tw || tw->w == 0 || tw->h == 0)
        return 0xe2000000;
    u_int mask_x = (u_int)((-tw->w) & 0xff) >> 3, mask_y = (u_int)((-tw->h) & 0xff) >> 3;
    u_int ofs_x = (u_int)(tw->x & 0xff) >> 3, ofs_y = (u_int)(tw->y & 0xff) >> 3;
    return 0xe2000000 | mask_x | mask_y << 5 | ofs_x << 10 | ofs_y << 15;
}

static u_int draw_mode(int dfe, int dtd, int tpage)
{
    return 0xe1000000 | (dtd ? 0x200u : 0) | (dfe ? 0x400u : 0) | ((u_int)tpage & 0x9ff);
}

void SetDrawMode(DR_MODE *p, int dfe, int dtd, int tpage, RECT *tw)
{
    setlen(p, 2);
    p->code[0] = draw_mode(dfe, dtd, tpage);
    p->code[1] = texture_window(tw);
}

void SetDrawTPage(DR_TPAGE *p, int dfe, int dtd, int tpage)
{
    setlen(p, 1);
    p->code[0] = draw_mode(dfe, dtd, tpage);
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h)
{
    memset(env, 0, sizeof(*env));
    env->disp.x = (short)x;
    env->disp.y = (short)y;
    env->disp.w = (short)w;
    env->disp.h = (short)h;
    return env;
}

DISPENV *PutDispEnv(DISPENV *env)
{
    disp_env = *env;
    return env;
}

DISPENV *GetDispEnv(DISPENV *env)
{
    *env = disp_env;
    return env;
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h)
{
    memset(env, 0, sizeof(*env));
    env->clip.x = (short)x;
    env->clip.y = (short)y;
    env->clip.w = (short)w;
    env->clip.h = (short)h;
    env->ofs[0] = (short)x;
    env->ofs[1] = (short)y;
    env->dtd = 1;
    env->dfe = 1;
    return env;
}

/* Sends the same commands as the real library: area, offset, mode, window, then the optional clear. */
DRAWENV *PutDrawEnv(DRAWENV *env)
{
    draw_env = *env;
    const RECT *c = &env->clip;
    u_int words[5] = {
        0xe3000000 | ((u_int)c->x & 0x3ff) | ((u_int)c->y & 0x1ff) << 10,
        0xe4000000 | ((u_int)(c->x + c->w - 1) & 0x3ff) | ((u_int)(c->y + c->h - 1) & 0x1ff) << 10,
        0xe5000000 | ((u_int)env->ofs[0] & 0x7ff) | ((u_int)env->ofs[1] & 0x7ff) << 11,
        draw_mode(env->dfe, env->dtd, env->tpage),
        texture_window(&env->tw),
    };
    gpu_gp0(words, 5);
    if (env->isbg)
        ClearImage(&env->clip, env->r0, env->g0, env->b0);
    return env;
}

DRAWENV *GetDrawEnv(DRAWENV *env)
{
    *env = draw_env;
    return env;
}

/* The display area as a VRAM rectangle (w in pixels; 24-bit pixels take 1.5 VRAM words); false when
 * nothing is shown. The TV always shows at least 240 lines, whatever the area's height. The real
 * GPU's usual modes never wrap around VRAM, so the area is clamped. */
static int display_area(int *x, int *y, int *w, int *h)
{
    const RECT *d = &disp_env.disp;
    if (!display_enabled || d->w <= 0 || d->h <= 0)
        return 0;
    int words = disp_env.isrgb24 ? d->w * 3 / 2 : d->w;
    int lines = d->h < 240 ? 240 : d->h;
    *x = d->x & (VRAM_WIDTH - 1);
    *y = d->y & (VRAM_HEIGHT - 1);
    if (*x + words > VRAM_WIDTH)
        words = VRAM_WIDTH - *x;
    *w = disp_env.isrgb24 ? words * 2 / 3 : words;
    *h = *y + lines > VRAM_HEIGHT ? VRAM_HEIGHT - *y : lines;
    return 1;
}

void gpu_present(void)
{
    int x, y, w, h;
    if (!display_area(&x, &y, &w, &h))
        platform_present(NULL, 0, 0, 0);
    else if (disp_env.isrgb24)
        platform_present_rgb24((const uint8_t *)&vram[y][x], w, h, VRAM_WIDTH * 2);
    else
        platform_present(&vram[y][x], w, h, VRAM_WIDTH);
}

int gpu_save_display(const char *path)
{
    int x, y, w, h;
    if (!display_area(&x, &y, &w, &h))
        return 0;
    if (disp_env.isrgb24)
        return platform_save_bmp_rgb24(path, (const uint8_t *)&vram[y][x], w, h, VRAM_WIDTH * 2);
    return platform_save_bmp(path, &vram[y][x], w, h, VRAM_WIDTH);
}

int gpu_save_vram(const char *path)
{
    return platform_save_bmp(path, &vram[0][0], VRAM_WIDTH, VRAM_HEIGHT, VRAM_WIDTH);
}
