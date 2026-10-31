# Controls

The port emulates one PSX pad on port 1. The keyboard and the first SDL gamepad found both drive it, and
pressing a button on either counts. Nothing is read in test runs (`--screenshot`, `--vram`, `--wav`); use
`--keys` to script presses instead. Source: `src/platform/platform_sdl.c` (key and gamepad tables) and
`src/psx/libpad.c` (NeGcon emulation).

## Physical button → PSX button

| PSX button | Keyboard | Gamepad (SDL position) |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| Cross ✕ | `X` or `Space` | South (Xbox A) |
| Circle ○ | `C` | East (Xbox B) |
| Square □ | `Z` | West (Xbox X) |
| Triangle △ | `S` | North (Xbox Y) |
| L1 | `Q` | Left shoulder |
| R1 | `E` | Right shoulder |
| L2 | `1` | Left trigger (past half way) |
| R2 | `3` | Right trigger (past half way) |
| Start | `Enter` | Start / Menu |
| Select | `Backspace` | Back / View |
| L3 / R3 | (none) | Stick clicks (the game doesn't use them) |

By default the pad is a **digital** pad, so the analog sticks do nothing and you steer with the D-pad.
Start with `--negcon` to steer with the left stick (see below).

## In the race (digital pad)

The game's **Controls** menu offers four layouts. Type 1 is the default. The table below is
`pad_layouts` in `src/game/frontend_init.c`.

| Action | Type 1 (default) | Type 2 | Type 3 | Type 4 |
|---|---|---|---|---|
| Accelerate | ✕ | □ | ○ | △ |
| Brake | □ | ✕ | △ | ○ |
| Steer | D-pad ← → | D-pad ← → | D-pad ← → | D-pad ← → |
| Fast steer (hold with ← →) | L1 or R1 | L1 or R1 | L2 or R2 | L2 or R2 |
| View toggle (bumper ↔ chase) | L2 or R2 | L2 or R2 | L1 or R1 | L1 or R1 |

So with Type 1 on a keyboard: `X`/`Space` accelerates, `Z` brakes, the arrows steer, `Q`/`E` give fast
steer and `1`/`3` switch the camera. Brake wins when both pedals are held.

These work the same in every layout:

| Action | Button |
|---|---|
| Pause menu | Start |
| Abandon the race | Hold Start + Select for about 2 seconds |
| Pause menu: move / choose / volume | D-pad ↑ ↓, ✕, D-pad ← → |
| Pit repairs: choose a part of the car / repair it / leave | D-pad ← →, ✕, hold □ |
| Replay: slow motion / exit | Hold ✕, △ |
| Attract-mode demo: exit | Any button |

On the PSX, unplugging the pad or changing its type also opens the pause menu. In the port the keyboard
always counts as a pad, so unplugging a gamepad does nothing.

## Front end and menus

| Action | Button |
|---|---|
| Move | D-pad |
| Select / confirm | ✕ (Start also works on some screens) |
| Back | △ |
| Change a menu option's value (back / forward) | □ / ○ |

## NeGcon mode (`--negcon`)

With `--negcon` the port reports a NeGcon (twist pad) instead of a digital pad:

| NeGcon control | Gamepad | Keyboard |
|---|---|---|
| Twist (steering) | Left stick X | (none) |
| I (analog) | Right trigger | `X` / `Space` (fully pressed) |
| II (analog) | Left trigger | `Z` (fully pressed) |
| L (analog) | Left shoulder | `Q` (fully pressed) |
| A, B, R, Start, D-pad | ○, △, R1, Start and D-pad as mapped above | as mapped above |

The game counts I, II and L as pressed once they go past half way. In the Type 1 NeGcon layout, I
accelerates, II brakes, and L or R toggles the view. The steering dead zone and full-lock range come from
the NeGcon setup screen (the defaults are dead zone 5 and range 70, out of 128 per side).
