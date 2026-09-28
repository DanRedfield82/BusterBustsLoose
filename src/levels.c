// Stage/area list for the level select.
//
// Areas use the hidden debug stage select ($68 = stage 0-5, $94 = area, Start).
// Area 6 of stage 0 is the ending and area 7 the staff roll. Holding Select as
// the stage is confirmed opens the bonus section instead; on stage 0 that is a
// hidden menu of the bonus games (cursor $0442: 0 = roulette wheel, 1-5 games).
#include "levels.h"

const LevelArea levelAreas[] = {
  // Stage 1
  { LEVEL_AREA, 0, 0, "Hallway",            -1 },
  { LEVEL_AREA, 0, 1, "Corridor",            3 },
  { LEVEL_AREA, 0, 2, "Library",             3 },
  { LEVEL_AREA, 0, 3, "Classroom",           3 },
  { LEVEL_AREA, 0, 4, "Cafeteria (Boss)",   -1 },
  // Stage 2
  { LEVEL_AREA, 1, 0, "Frontier Town",      -1 },
  { LEVEL_AREA, 1, 1, "Saloon",              4 },
  { LEVEL_AREA, 1, 2, "Train Roof",         -1 },
  { LEVEL_AREA, 1, 3, "Flat Cars",           9 },
  { LEVEL_AREA, 1, 4, "Locomotive (Boss)",  -1 },
  // Stage 3
  { LEVEL_AREA, 2, 0, "Mansion Gate",       -1 },
  { LEVEL_AREA, 2, 1, "Bell Tower",          5 },
  { LEVEL_AREA, 2, 2, "Candle Hall",         5 },
  { LEVEL_AREA, 2, 3, "Laboratory (Boss)",  -1 },
  // Stage 4
  { LEVEL_AREA, 3, 0, "Football Game",      -1 },
  // Stage 5
  { LEVEL_AREA, 4, 0, "Balloon Field",      -1 },
  { LEVEL_AREA, 4, 1, "Biplane Chase",      11 },
  { LEVEL_AREA, 4, 2, "Hot-Air Balloons",   11 },
  { LEVEL_AREA, 4, 3, "Airship",            11 },
  { LEVEL_AREA, 4, 4, "Pinball (Boss)",     -1 },
  { LEVEL_AREA, 4, 5, "Pinball 2",           6 },
  { LEVEL_AREA, 4, 6, "Cloud Temple",       11 },
  // Stage 6
  { LEVEL_AREA, 5, 0, "Space Station",      -1 },
  { LEVEL_AREA, 5, 1, "Asteroid Belt",      10 },
  { LEVEL_AREA, 5, 2, "Reactor",            10 },
  { LEVEL_AREA, 5, 3, "Hard Course",        10 },
  { LEVEL_AREA, 5, 4, "Robot Factory",      10 },
  { LEVEL_AREA, 5, 5, "Final Battle",       -1 },
  // bonus section played between stages
  { LEVEL_BONUS, 0, 0, "Game Time Roulette", -1 },
  { LEVEL_BONUS, 0, 1, "Squash",             -1 },
  { LEVEL_BONUS, 0, 2, "Junction",           -1 },
  { LEVEL_BONUS, 0, 3, "Maze",               -1 },
  { LEVEL_BONUS, 0, 4, "Bingo",              -1 },
  { LEVEL_BONUS, 0, 5, "Seesaw",             -1 },
  // extras
  { LEVEL_EXTRA, 0, 6, "Ending",            -1 },
  { LEVEL_EXTRA, 0, 7, "Staff Roll",        -1 },
};
const int levelAreaCount = sizeof(levelAreas) / sizeof(levelAreas[0]);

const LevelStage levelStages[] = {
  { "Stage 1: Acme Looniversity", 0, 5 },
  { "Stage 2: Wild West",          5, 5 },
  { "Stage 3: Scary Mansion",     10, 4 },
  { "Stage 4: Football",          14, 1 },
  { "Stage 5: Sky High",          15, 7 },
  { "Stage 6: Star Warners",      22, 6 },
  { "Bonus Games",                28, 6 },
  { "Extras",                     34, 2 },
};
const int levelStageCount = sizeof(levelStages) / sizeof(levelStages[0]);
