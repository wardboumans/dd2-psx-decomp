/* Replays: a script of pad events recorded during the race and played back afterwards. */
#ifndef GAME_REPLAY_H
#define GAME_REPLAY_H

#include "system/controller.h"

#include <stdint.h>

#define REPLAY_SCRIPT_WORDS 0xe00

/* The script: one word per event and the frames it lasts. Digital pads (PAD_DIGITAL) record the steer and
 * throttle bits in the top 5 bits and the count in the low 11; analog pads the 6-bit steering and two
 * throttle bits in the low byte and the count in the high byte. */
extern int32_t replay_pad_type;                      /* 0x80079738: the pad type the events are recorded for */
extern int32_t replay_record_pad_type;               /* 0x80079740: the pad type of the last recorded event */
extern int32_t replay_action_repeat;                 /* 0x8007973C: frames the current event still lasts */
extern uint16_t *replay_script_ptr;                  /* 0x80079B00 */
extern uint16_t *const replay_script;                /* 0x8009581C, REPLAY_SCRIPT_WORDS long */
extern uint16_t *replay_script_end;                  /* 0x80079C30 */
extern int32_t replay_ended;                         /* 0x80079BF4 */
extern int32_t replay_started;                       /* 0x80079A88: the first event has been recorded */
extern int32_t replay_invalid;                       /* 0x80079BF8 */
extern int32_t replay_level;                         /* 0x80079AE4 */

void Init_Replay_Script(void);                       /* 0x80045DA8 */
void Record_Event(uint16_t event, const PadState *pad); /* 0x80045E18 */
void Control_Car_Replay(int car, const PadState *pad); /* 0x80045928 */
void Terminate_Replay(const PadState *pad);          /* 0x80045F74 */
void Terminate_Replay_Bodge(const PadState *pad);    /* 0x80045FC8 */

void UndentCar(int car, int section);         /* 0x80044EB8 */
void SetHighLight(int section, int flashing);  /* 0x80045500 */
void Drive_Car_Into_Pit(int car);              /* 0x8004571C */

#endif
