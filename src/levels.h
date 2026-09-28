#pragma once
#include <stdint.h>

enum {
  LEVEL_AREA = 0,    // a playable area (stage $68, area $94)
  LEVEL_EXTRA,       // ending / staff roll (special area numbers 6 and 7)
  LEVEL_BONUS,       // bonus game from the hidden bonus menu; `area` = menu entry ($0442)
};

typedef struct LevelArea {
  uint8_t kind;      // LEVEL_*
  uint8_t stage;     // value for $68
  uint8_t area;      // value for $94 (or the bonus menu entry)
  const char* name;
  int8_t music;      // music table entry the area should play (-1 = the game handles it)
} LevelArea;

typedef struct LevelStage {
  const char* name;
  int firstArea;
  int areaCount;
} LevelStage;

extern const LevelStage levelStages[];
extern const int levelStageCount;
extern const LevelArea levelAreas[];
extern const int levelAreaCount;
