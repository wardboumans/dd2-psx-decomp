#include "psx/libc.h"

#include <stdint.h>

/* The game never calls srand, so the sequence starts from the seed the BIOS sets at boot.
 * Unverified: psx-spx does not give it; check against a real BIOS. */
static uint32_t rand_next = 0x24040001;

/* The BIOS generator (A(2Fh) rand, A(30h) srand) that Psy-Q's rand calls: the game's random
 * choices (the demo track, AI) follow from it. */
int psx_rand(void)
{
    rand_next = rand_next * 1103515245u + 12345u;
    return (int)((rand_next >> 16) & RAND_MAX_PSX);
}

void psx_srand(unsigned int seed)
{
    rand_next = seed;
}
