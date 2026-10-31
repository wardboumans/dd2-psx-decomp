#include "system/debug.h"

#include "psx/libapi.h"

#define RCntCNT1 0xf2000001
#define RCntCNT2 0xf2000002
#define EvSpINT 0x0002
#define EvMdNOINTR 0x2000
#define RCntMdINTR 0x1000
#define RCntMdNOINTR 0x2000

/* Root counters 1 (hblank) and 2 (system clock / 8) for timing measurements. */
void Profile_Init(void)
{
    EnterCriticalSection();
    long event = OpenEvent(RCntCNT1, EvSpINT, EvMdNOINTR, NULL);
    EnableEvent(event);
    SetRCnt(RCntCNT1, 0xffff, RCntMdINTR);
    SetRCnt(RCntCNT2, 0xffff, RCntMdNOINTR);
    ExitCriticalSection();
}

void Debug_Stub(void)
{
}
