/* libpress replacement: the MDEC movie decoder. DecDCTvlc turns a frame's bitstream (.STR versions 1
 * and 2) into MDEC run-level codes; DecDCTin/DecDCTout decode those into 16x16 macroblocks of 15-bit
 * or 24-bit pixels. The decoding is synchronous: DecDCTout has finished (and called the callback)
 * when it returns. */
#ifndef PSX_LIBPRESS_H
#define PSX_LIBPRESS_H

#include "psx/types.h"

void DecDCTReset(int mode);
int DecDCTvlc(u_long *bs, u_long *buf);
void DecDCTin(u_long *buf, int mode);   /* mode bit 0: 24-bit output; bit 1: 15-bit with bit 15 set */
void DecDCTout(u_long *buf, int size);  /* size in 32-bit words */
int DecDCToutCallback(void (*func)(void));

#endif
