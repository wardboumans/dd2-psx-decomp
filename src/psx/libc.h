/* The parts of Psy-Q's libc whose results the game depends on. Names that clash with the host's
 * C library get a psx_ prefix through a macro, so game code keeps calling rand(). */
#ifndef PSX_LIBC_H
#define PSX_LIBC_H

#include <stdlib.h> /* first, so the host's rand() is declared before the macro renames it */

#define rand psx_rand
#define srand psx_srand

#define RAND_MAX_PSX 0x7fff

int psx_rand(void);
void psx_srand(unsigned int seed);

#endif
