#pragma once
#include "bbl.h"
void hud_init(Bbl* g);
// call once per frame before running the frame
void hud_update(Bbl* g);
bool hud_inGameplay(Bbl* g);
