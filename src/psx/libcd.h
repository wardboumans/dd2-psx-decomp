/* libcd replacement: serves the disc from the assets folder made by tools/make_assets.py. */
#ifndef PSX_LIBCD_H
#define PSX_LIBCD_H

#include "psx/types.h"

#include <stdbool.h>

#define CdlNop 0x01
#define CdlSetloc 0x02
#define CdlPlay 0x03
#define CdlReadN 0x06
#define CdlStop 0x08
#define CdlPause 0x09
#define CdlMute 0x0b
#define CdlDemute 0x0c
#define CdlSetmode 0x0e
#define CdlSeekL 0x15
#define CdlSeekP 0x16
#define CdlReadS 0x1b

/* Interrupt kinds passed to the ready callback. */
#define CdlDataReady 0x01

/* CdlSetmode bits. */
#define CdlModeDA 0x01    /* play CD-DA */
#define CdlModeRept 0x04  /* report the play position */
#define CdlModeSF 0x08    /* only play the XA channel set with CdlSetfilter */
#define CdlModeRT 0x40    /* play XA audio sectors as they are read */
#define CdlModeSpeed 0x80 /* double speed */

/* Absolute disc position, BCD, including the 2-second lead-in (sector 0 = 00:02:00). */
typedef struct {
    u_char minute;
    u_char second;
    u_char sector;
    u_char track;
} CdlLOC;

typedef struct {
    CdlLOC pos;
    u_long size;
    char name[16];
} CdlFILE;

typedef struct {
    u_char val0, val1, val2, val3;
} CdlATV;

typedef void (*CdlCB)(u_char status, u_char *result);

int CdInit(void);
CdlFILE *CdSearchFile(CdlFILE *fp, char *name);
int CdControl(u_char com, u_char *param, u_char *result);
int CdControlB(u_char com, u_char *param, u_char *result);
int CdControlF(u_char com, u_char *param);
int CdRead(int sectors, u_long *buf, int mode);
int CdReadSync(int mode, u_char *result);
int CdGetToc(CdlLOC *loc);
int CdMix(CdlATV *vol);
CdlCB CdReadyCallback(CdlCB func);
int CdPosToInt(CdlLOC *p);
CdlLOC *CdIntToPos(int i, CdlLOC *p);

/* Streaming (.STR movies): CdRead2 reads on from the current position at the drive's pace; the video
 * sectors are gathered into whole frames in the ring and XA audio sectors are played (CdlModeRT). */
#define CdlModeStream 0x100 /* CdRead2: stream into the ring */

void StSetRing(u_long *ring_addr, u_long ring_size); /* size in sectors; the port keeps its own ring */
void StSetStream(u_long mode, u_long start_frame, u_long end_frame, void (*func1)(void), void (*func2)(void));
u_long StGetNext(u_long **addr, u_long **header); /* 0 and a frame, or 1 when none is complete */
u_long StFreeRing(u_long *base);
void StClearRing(void);
void StUnSetRing(void);
int CdRead2(long mode);

/* Port-only: use the assets folder as the disc; false (with a message) when it isn't there. */
bool cd_open_assets(const char *dir);

/* Port-only: the drive's CD-DA output, 44.1 kHz stereo, as the SPU takes it in: `frames` frames
 * of the music being played (silence when nothing plays). Sends the play position reports. */
void cd_audio_read(short *stereo, int frames);

#endif
