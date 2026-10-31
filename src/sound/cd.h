#ifndef SOUND_CD_H
#define SOUND_CD_H

#include "psx/libcd.h"

#include <stdint.h>

#define CD_TOC_ENTRIES 100

extern CdlLOC cd_toc[CD_TOC_ENTRIES]; /* 0x8007C728: [0] disc end, [1..] track starts */
extern int32_t cd_track_count;        /* 0x800799E0 */

void CD_Open(void);                  /* 0x8004E344 */
void Set_CD_Volume(short volume);    /* 0x8004E364 */
void Set_CD_Track(int track, int loop); /* 0x8004E3A4 */
void CD_Stop(void);                  /* 0x8004E424 */
void CD_Pause(void);                 /* 0x8004E454 */
void CD_Restart(void);               /* 0x8004E484 */
void Read_CD_Toc(void);              /* 0x8004E4BC */
void CD_Set_Play_Range(const CdlLOC *start, const CdlLOC *end); /* 0x8004E520 */
void Start_CD_Audio(void);           /* 0x8004E5A0 */
void Check_For_CD_Loop(u_char status, u_char *result); /* 0x8004E5E0 */

#endif
