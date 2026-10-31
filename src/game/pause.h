/* The pause menu, and the pit lane's state. */
#ifndef GAME_PAUSE_H
#define GAME_PAUSE_H

#define PAUSE_END_RACE 3 /* Pause_Mode: retire or quit chosen */

void Initialise_Pause_Mode(void); /* 0x8004327C */
int Pause_Mode(void);             /* 0x80043718 */
void Init_Pit_State(void);        /* 0x80044E90 */

#endif
