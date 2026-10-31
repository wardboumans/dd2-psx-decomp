/* libpad replacement (old-style InitPAD API): port 1 is a digital pad driven by the platform input. */
#ifndef PSX_LIBPAD_H
#define PSX_LIBPAD_H

#include "psx/types.h"

#include <stdbool.h>

void InitPAD(u_char *buf1, long len1, u_char *buf2, long len2);
void StartPAD(void);
void StopPAD(void);
void ChangeClearPAD(long mode);

/* Port-only: port 1 answers as a NeGcon (analog twist and I, II, L) instead of a digital pad. */
void pad_set_negcon(bool on);

/* Port-only: what the BIOS does each vblank, fill the receive buffers. */
void pad_service(void);

/* Port-only, for testing: hold `buttons` (PSX pad bits) for a few vblanks from vblank `at`
 * (counted from StartPAD). */
void pad_script(int at, int hold, unsigned buttons); /* hold 0: a short press */

#endif
