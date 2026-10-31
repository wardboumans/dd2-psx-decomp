/* libapi replacement: BIOS services the game uses (critical sections, heap, events, root counters, the
 * memory card's file system). */
#ifndef PSX_LIBAPI_H
#define PSX_LIBAPI_H

#include "psx/types.h"

/* Psy-Q names that collide with system libraries (kernel32 and the C library here) link under a psx_
 * prefix; game code keeps calling them by their Psy-Q name. */
#define EnterCriticalSection psx_EnterCriticalSection
#define ExitCriticalSection psx_ExitCriticalSection
#define open psx_open
#define read psx_read
#define write psx_write
#define close psx_close
#define delete psx_delete

int EnterCriticalSection(void);
void ExitCriticalSection(void);

/* The port allocates with the C library; the PSX heap area is never handed to malloc. */
void InitHeap(u_long *head, u_long size);

/* Events: opened for a class (desc) and kind (spec); DeliverEvent marks the open ones that match, and
 * TestEvent reports and clears that once. */
long OpenEvent(u_long desc, long spec, long mode, long (*func)(void));
long EnableEvent(long event);
long TestEvent(long event);
void DeliverEvent(u_long desc, long spec);

#define EvSpIOE 0x0004
#define EvSpERROR 0x8000
#define EvSpTIMOUT 0x0100
#define EvSpNEW 0x2000
#define SwCARD 0xf4000001
#define HwCARD 0xf0000011

long SetRCnt(u_long spec, u_short target, long mode);

/* The stack stays where the host put it; the game only moves it to the scratchpad for speed. */
long SetSp(long sp);

/* The BIOS file system on the memory cards ("bu00:" and "bu10:"), see psx/libcard.c. */
#define FREAD 0x0001
#define FWRITE 0x0002
#define FCREAT 0x0200

struct DIRENTRY {
    char name[20];
    long attr;
    long size;              /* bytes */
    struct DIRENTRY *next;
    long head;              /* first block */
    char system[4];
};

int open(const char *name, int flags);   /* FCREAT | blocks << 16 to create */
int read(int fd, void *buf, int n);      /* n a multiple of 128 */
int write(int fd, const void *buf, int n);
int close(int fd);
int delete(const char *name);
int format(const char *device);
struct DIRENTRY *firstfile(const char *pattern, struct DIRENTRY *dir);
struct DIRENTRY *nextfile(struct DIRENTRY *dir);

#endif
