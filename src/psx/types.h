/* Psy-Q basic types.
 *
 * On the PSX `long` is 32 bits. Psy-Q uses u_long both for 32-bit data and for
 * addresses, so here it is a fixed 32-bit type; code that stores pointers in it
 * must be rewritten to use real pointers. */
#ifndef PSX_TYPES_H
#define PSX_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t u_char;
typedef uint16_t u_short;
typedef uint32_t u_int;
typedef uint32_t u_long;

#endif
