#include "psx/libetc.h"
#include "psx/libgpu.h"
#include "psx/libpad.h"
#include "psx/libspu.h"
#include "platform/platform.h"

#include <stdint.h>
#include <stdlib.h>

/* NTSC vertical blank: 60000/1001 Hz = 59.94 Hz, so one vblank is 50050000/3 ns.
 * 263 lines per field give the horizontal count VSync(0) and VSync(1) return. */
#define HSYNCS_PER_VBLANK 263

static uint64_t clock_start;
static int64_t vblanks_serviced; /* vblanks whose callback has run and frame was shown */
static int64_t last_vsync;       /* vblank count at the previous VSync wait */
static int64_t last_polled = -1; /* result of the previous VSync(-1) */
static int same_polls;           /* polls since it last changed */
#define SPIN_POLLS 64
static void (*vsync_callback)(void);
static int64_t timer_vblank = -1;
static void (*timer_func)(void);

static int64_t vblank_now(void)
{
    if (clock_start == 0)
        clock_start = platform_ticks_ns();
    return (int64_t)((platform_ticks_ns() - clock_start) * 3 / 50050000);
}

static uint64_t vblank_time_ns(int64_t vblank)
{
    return clock_start + (uint64_t)vblank * 50050000 / 3;
}

/* Everything the hardware does on its own at each vblank: the VSync callback and
 * scanning out the display area. */
void etc_service(void)
{
    int64_t now = vblank_now();
    if (vblanks_serviced < now) {
        while (vblanks_serviced < now) {
            vblanks_serviced++;
            pad_service();
            spu_service();
            if (vsync_callback)
                vsync_callback();
        }
        gpu_present();
        if (timer_func && vblanks_serviced >= timer_vblank) {
            void (*func)(void) = timer_func;
            timer_func = NULL;
            func();
        }
    }
    if (platform_pump_events())
        exit(0);
}

static void wait_for_vblank(int64_t target)
{
    while (vblank_now() < target) {
        platform_sleep_until_ns(vblank_time_ns(target));
        etc_service();
    }
    etc_service();
}

void etc_idle(void)
{
    wait_for_vblank(vblank_now() + 1);
}

int VSync(int mode)
{
    if (mode < 0) {
        /* Games poll VSync(-1) in busy loops; after many polls within one vblank, sleep to the next
         * instead of spinning. A single read after such a loop (Draw_All notes the vblank it ended
         * on) must not wait, or every frame would take a vblank more than the game asks for. */
        int64_t now = vblank_now();
        if (now == last_polled && ++same_polls >= SPIN_POLLS) {
            wait_for_vblank(now + 1);
            now = vblank_now();
        } else {
            etc_service();
        }
        if (now != last_polled)
            same_polls = 0;
        last_polled = now;
        return (int)now;
    }
    int64_t now = vblank_now();
    if (mode == 1)
        return (int)((now - last_vsync) * HSYNCS_PER_VBLANK);

    int64_t target = mode == 0 ? now + 1 : last_vsync + mode;
    if (target < now)
        target = now;
    wait_for_vblank(target);
    int elapsed = (int)((target - last_vsync) * HSYNCS_PER_VBLANK);
    last_vsync = target;
    return elapsed;
}

int VSyncCallback(void (*func)(void))
{
    vblanks_serviced = vblank_now();
    vsync_callback = func;
    return 0;
}

int ResetCallback(void)
{
    vsync_callback = NULL;
    return 0;
}

int StopCallback(void)
{
    vsync_callback = NULL;
    return 0;
}

/* Only NTSC is emulated: the vblank clock above is fixed at 59.94 Hz. */
long SetVideoMode(long mode)
{
    (void)mode;
    return MODE_NTSC;
}

void etc_after_vblanks(int vblanks, void (*func)(void))
{
    timer_vblank = vblank_now() + vblanks;
    timer_func = func;
}
