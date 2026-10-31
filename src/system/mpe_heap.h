#ifndef SYSTEM_MPE_HEAP_H
#define SYSTEM_MPE_HEAP_H

#include <stddef.h>
#include <stdint.h>

/* The PSX heap area (0x800AC000, 0xE0000 bytes). The game also uses its start as a scratch
 * load buffer before and between heap use; the port allocates with the C library instead. */
#define HEAP_SIZE 0xE0000
extern uint8_t *heap_base; /* 0x8007969C */

void *MPE_malloc(size_t size);              /* 0x80052C68 */
void MPE_free(void *p);                     /* 0x80052C88 */
void *MPE_realloc(void *p, size_t size);    /* 0x80052CA8 */

#endif
