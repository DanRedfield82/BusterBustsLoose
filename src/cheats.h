#pragma once
#include "bbl.h"

enum {
  CHEAT_INF_HEALTH = 0,
  CHEAT_INF_LIVES,
  CHEAT_INF_DASH,
  CHEAT_INVINCIBLE,
  CHEAT_MOON_JUMP,
  CHEAT_99_LIVES,
  CHEAT_5_HEARTS,
  CHEAT_STAR_BONUS,
  CHEAT_STAGE_SELECT,
  CHEAT_PASSWORDS,
  CHEAT_COUNT
};

typedef struct CheatInfo {
  const char* name;
  const char* help;
} CheatInfo;

extern const CheatInfo cheatInfo[CHEAT_COUNT];

void cheats_init(Bbl* g);
void cheats_set(Bbl* g, int id, int value);
int cheats_get(Bbl* g, int id);
// reapply all ROM patches (after a reset or state load nothing is needed, but
// this keeps the ROM image in sync with the enabled set)
void cheats_apply(Bbl* g);
void cheats_preFrame(Bbl* g);
void cheats_postFrame(Bbl* g);
