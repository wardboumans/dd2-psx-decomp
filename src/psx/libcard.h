/* libcard replacement: memory cards emulated on card image files (psx/libcard.c). */
#ifndef PSX_LIBCARD_H
#define PSX_LIBCARD_H

void InitCARD(long pad_enable);
long StartCARD(void);
long StopCARD(void);
void _bu_init(void);
long _card_auto(long val);
long _card_info(long chan);  /* delivers SwCARD: IOE present, TIMOUT none */
long _card_load(long chan);  /* delivers SwCARD: IOE formatted, NEW not */
long _card_clear(long chan); /* delivers HwCARD IOE */

/* PORT: the image files for the two slots instead of the user's (NULL: slot 1 a card in memory only, slot 2
 * empty). For tests; call before the first card access. */
void card_set_image_paths(const char *slot1, const char *slot2);

#endif
