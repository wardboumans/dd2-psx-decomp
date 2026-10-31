/* The intro and outro movies. On the PSX each is its own executable (NTSCINTR.EXE, NTSCXTRO.EXE,
 * PALINTRO.EXE, PALXTRO.EXE: Sony's streaming movie sample) that the game loads and runs; here they are
 * ordinary code, run by Play_Intro and Play_Xtro. */
#ifndef GAME_MOVIE_H
#define GAME_MOVIE_H

enum { MOVIE_INTRO, MOVIE_OUTRO };

/* What the executable's main does (0x80101A2C in each). */
void Movie_Main(int movie, int pal);

#endif
