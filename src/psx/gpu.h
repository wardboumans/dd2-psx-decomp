/* The software GPU behind libgpu: executes GP0 command words into VRAM. */
#ifndef PSX_GPU_H
#define PSX_GPU_H

#include "psx/types.h"

/* Run `count` GP0 command words (one primitive packet). */
void gpu_gp0(const u_int *words, int count);

void gpu_reset(void);

#endif
