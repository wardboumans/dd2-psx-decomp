/* Track triggers and the effects of cars on the ground. */
#ifndef GAME_TRIGGERS_H
#define GAME_TRIGGERS_H

void Strip_Trigger_Handler(void);        /* 0x8004820C */
void Sparking(int ground_mode, int car); /* 0x800482E0 */

#endif
