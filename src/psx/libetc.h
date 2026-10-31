/* libetc replacement: VSync and its callback on an emulated NTSC clock. */
#ifndef PSX_LIBETC_H
#define PSX_LIBETC_H

#define MODE_NTSC 0
#define MODE_PAL 1

int VSync(int mode);
int VSyncCallback(void (*func)(void));
int ResetCallback(void);
int StopCallback(void);
long SetVideoMode(long mode);

/* Port-only: run vblank callbacks that are due, show the display area and handle window
 * events. Long busy-waits in game code (CD, DMA) must call this so time keeps passing. */
void etc_service(void);

/* Port-only: sleep until the next vblank and service it; for polling loops that wait on the hardware. */
void etc_idle(void);

/* Port-only: call `func` from the vblank service once `vblanks` vblanks have passed (testing). */
void etc_after_vblanks(int vblanks, void (*func)(void));

#endif
