#include "psx/libapi.h"

/* There are no interrupts to mask: callbacks run from VSync/etc_service on the game's own thread. */
int EnterCriticalSection(void)
{
    return 1;
}

void ExitCriticalSection(void)
{
}

void InitHeap(u_long *head, u_long size)
{
    (void)head;
    (void)size;
}

/* Events are only delivered by the memory card emulation (psx/libcard.c); callbacks and interrupt modes
 * are not needed by the game. */
#define MAX_EVENTS 32
#define EVENT_BASE 0xf1000000

static struct {
    u_long desc;
    long spec;
    int used, enabled, ready;
} events[MAX_EVENTS];

long OpenEvent(u_long desc, long spec, long mode, long (*func)(void))
{
    (void)mode;
    (void)func;
    /* PORT: the game opens the memory card's events again after each movie; the BIOS would keep
     * the old ones too until its table is full. The same class and kind gets the same event here. */
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (events[i].used && events[i].desc == desc && events[i].spec == spec) {
            events[i].enabled = 0;
            events[i].ready = 0;
            return EVENT_BASE + i;
        }
    }
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (!events[i].used) {
            events[i].desc = desc;
            events[i].spec = spec;
            events[i].used = 1;
            events[i].enabled = 0;
            events[i].ready = 0;
            return EVENT_BASE + i;
        }
    }
    return -1;
}

static int event_index(long event)
{
    long i = event - EVENT_BASE;
    return i >= 0 && i < MAX_EVENTS && events[i].used ? (int)i : -1;
}

long EnableEvent(long event)
{
    int i = event_index(event);
    if (i < 0)
        return 0;
    events[i].enabled = 1;
    return 1;
}

long TestEvent(long event)
{
    int i = event_index(event);
    if (i < 0 || !events[i].ready)
        return 0;
    events[i].ready = 0;
    return 1;
}

void DeliverEvent(u_long desc, long spec)
{
    for (int i = 0; i < MAX_EVENTS; i++)
        if (events[i].used && events[i].enabled && events[i].desc == desc && events[i].spec == spec)
            events[i].ready = 1;
}

/* Root counters only drive Profile_Init's timing, which the port does not use. */
long SetRCnt(u_long spec, u_short target, long mode)
{
    (void)spec;
    (void)target;
    (void)mode;
    return 1;
}

long SetSp(long sp)
{
    (void)sp;
    return 0;
}
