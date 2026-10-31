# PSX module map

Where the code of each PSX address range lives: each row is a run of consecutive functions of one
source file (exe `.text`, then the overlays). Source comments give the address of every function.

## SCUS_943.50 (main executable)

| Range | File | Functions |
|---|---|---|
| `800116ec-80011e67` | `src/game/play_game.c` | 2 |
| `80011e68-80014f67` | `src/game/debris.c` | 12 |
| `80014f68-80015bab` | `src/game/denting.c` | 2 |
| `80016074-8001980f` | `src/game/track.c` | 9 |
| `80019810-8001aacf` | `src/game/camera.c` | 7 |
| `8001aad0-8001d513` | `src/game/car_draw.c` | 9 |
| `8001d514-8001db8f` | `src/game/car_init.c` | 3 |
| `8001db90-8001dfe3` | `src/game/anim.c` | 2 |
| `8001e220-8001f94b` | `src/game/other_objects.c` | 2 |
| `8001f94c-80020523` | `src/game/flag.c` | 3 |
| `80020524-80020c6b` | `src/game/lens_flare.c` | 2 |
| `80020c6c-80024c5f` | `src/game/overlays.c` | 4 |
| `80024c60-8002674f` | `src/game/scene.c` | 12 |
| `80026750-80029a73` | `src/game/ai.c` | 12 |
| `80029a74-8002bd6b` | `src/game/collision.c` | 5 |
| `8002bd6c-8002ce8f` | `src/game/wheels.c` | 3 |
| `8002ce90-8002e84f` | `src/game/particles.c` | 8 |
| `8002e850-80033b33` | `src/game/car_collision.c` | 8 |
| `80033b34-8003721b` | `src/game/car_cluts.c` | 13 |
| `8003721c-80042667` | `src/game/car_motion.c` | 40 |
| `80042668-8004327b` | `src/game/main_init.c` | 10 |
| `8004327c-80044eb7` | `src/game/pause.c` | 3 |
| `80044eb8-80045fff` | `src/game/replay.c` | 8 |
| `80046000-8004806b` | `src/game/sound_fx.c` | 8 |
| `8004820c-80048a8f` | `src/game/triggers.c` | 2 |
| `80048a90-8004a987` | `src/game/frontend_init.c` | 22 |
| `8004a988-8004b80f` | `src/game/league.c` | 18 |
| `8004b810-8004c1cf` | `src/game/stats.c` | 9 |
| `8004c1d0-8004e343` | `src/graphics/font.c` | 17 |
| `8004e344-8004e697` | `src/sound/cd.c` | 10 |
| `8004e698-8004f0bf` | `src/sound/sound.c` | 16 |
| `8004f0c0-800502c3` | `src/graphics/object.c` | 15 |
| `800502c4-80050a33` | `src/system/controller.c` | 4 |
| `80050a34-800510c7` | `src/graphics/object.c` | 9 |
| `800511c0-8005245f` | `src/system/file.c` | 10 |
| `80052884-80052c67` | `src/system/maths.c` | 2 |
| `80052c68-80052cc7` | `src/system/mpe_heap.c` | 3 |
| `80052cc8-8005ac8f` | `src/graphics/draw_face.c` | 55 |
| `8005ac90-8005b5c7` | `src/graphics/object.c` | 10 |
| `8005b5c8-8005bb3f` | `src/graphics/sprite.c` | 4 |
| `8005bb40-8005d25f` | `src/system/card.c` | 25 |
| `8005d260-8005d36f` | `src/system/debug.c` | 2 |
| `8005d370-8005e5eb` | `src/graphics/sprite.c` | 1 |

## FRONTEND.BIN (overlay at 0x801C4000)

| Range | File | Functions |
|---|---|---|
| `801c5870-801c726f` | `src/game/stats.c` | 14 |
| `801c72d0-801c7333` | `src/game/frontend.c` | 2 |
| `801c7334-801c828f` | `src/game/stats.c` | 8 |
| `801c8290-801d3ff3` | `src/game/frontend.c` | 119 |

## GENERAL.BIN (overlay at 0x801E0000)

| Range | File | Functions |
|---|---|---|
| `801e09d0-801e8273` | `src/game/loadsave.c` | 27 |
| `801e8274-801ea9cf` | `src/game/stats.c` | 21 |
| `801ea9d0-801ec53b` | `src/game/frontend.c` | 17 |

## RESULTS.BIN (overlay at 0x801C4000, Ghidra block ov1)

| Range | File | Functions |
|---|---|---|
| `801c4934-801c97df` | `src/game/frontend.c` | 43 |
