#include "sound/cd.h"

#include "sound/sound.h"

/* 0x800798CC: CD audio left to left and right to right at full volume */
static const CdlATV cd_mix = {255, 0, 255, 0};

CdlLOC cd_toc[CD_TOC_ENTRIES];
int32_t cd_track_count;

static int32_t cd_loop;       /* 0x800798C0 */
static CdlLOC cd_report_pos;  /* 0x800799D4 */
static CdlLOC cd_play_start;  /* 0x800799D8 */
static CdlLOC cd_play_end;    /* 0x800799DC */

void CD_Open(void)
{
    CdInit();
}

void Set_CD_Volume(short volume)
{
    spu_common.cd.volume.left = (short)(volume << 3);
    spu_common.mask = SPU_COMMON_CDVOLL | SPU_COMMON_CDVOLR;
    spu_common.cd.volume.right = spu_common.cd.volume.left;
    SpuSetCommonAttr(&spu_common);
}

/* Plays disc track track + 1 (cd_toc[1] is the data track) to the start of the next one, or to
 * the end of the disc for the last; with `loop` set it starts again at the end. */
void Set_CD_Track(int track, int loop)
{
    const CdlLOC *end;
    cd_loop = loop != 0;
    if (track + 1 < cd_track_count) {
        if (track < 1)
            return;
        end = &cd_toc[track + 2];
    } else {
        end = &cd_toc[0];
    }
    CD_Set_Play_Range(&cd_toc[track + 1], end);
}

void CD_Stop(void)
{
    CdControl(CdlStop, NULL, NULL);
    CdReadyCallback(NULL);
}

void CD_Pause(void)
{
    CdControl(CdlPause, NULL, NULL);
    CdReadyCallback(NULL);
}

void CD_Restart(void)
{
    CdReadyCallback(Check_For_CD_Loop);
    CdControl(CdlPlay, NULL, NULL);
}

void Read_CD_Toc(void)
{
    CdlATV mix = cd_mix;
    cd_track_count = CdGetToc(cd_toc);
    if (cd_track_count < 1)
        cd_track_count = 0;
    CdMix(&mix);
}

/* Seeks to the start of the range with the sound muted; Start_CD_Audio plays it. */
void CD_Set_Play_Range(const CdlLOC *start, const CdlLOC *end)
{
    u_char mode = CdlModeDA | CdlModeRept;
    CdIntToPos(CdPosToInt((CdlLOC *)start), &cd_play_start);
    CdIntToPos(CdPosToInt((CdlLOC *)end), &cd_play_end);
    CdControlF(CdlSetmode, &mode);
    CdControlF(CdlMute, NULL);
    CdControlF(CdlSeekP, (u_char *)&cd_play_start);
}

void Start_CD_Audio(void)
{
    CdReadyCallback(Check_For_CD_Loop);
    CdControlF(CdlPlay, NULL);
    CdControlF(CdlDemute, NULL);
}

/* The play position reports (to the second) end the range: start again, or stop listening. */
void Check_For_CD_Loop(u_char status, u_char *result)
{
    if (status != CdlDataReady)
        return;
    cd_report_pos.track = result[1];
    cd_report_pos.minute = result[3];
    cd_report_pos.second = result[4];
    cd_report_pos.sector = 0;
    if (CdPosToInt(&cd_report_pos) >= CdPosToInt(&cd_play_end)) {
        if (!cd_loop) {
            CdControl(CdlPause, NULL, NULL);
            CdReadyCallback(NULL);
        } else {
            CdControl(CdlPlay, (u_char *)&cd_play_start, NULL);
        }
    }
}
