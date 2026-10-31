#include "system/mpe_heap.h"

#include <stdlib.h>

static uint8_t heap_area[HEAP_SIZE];
uint8_t *heap_base = heap_area;

void *MPE_malloc(size_t size)
{
    return malloc(size);
}

void MPE_free(void *p)
{
    free(p);
}

void *MPE_realloc(void *p, size_t size)
{
    return realloc(p, size);
}
