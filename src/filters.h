#pragma once
#include <stdbool.h>
#include "ppu.h"

enum {
  COLOUR_ORIGINAL = 0, COLOUR_VIVID, COLOUR_EXTRA_VIVID, COLOUR_NATURAL,
  COLOUR_WARM, COLOUR_COOL, COLOUR_SOFT, COLOUR_COUNT
};

extern const char* colourModeNames[COLOUR_COUNT];

// builds the shared colour table; returns false if the settings are neutral
bool filters_build(int mode, int brightness, int contrast, int saturation, int gamma);
// builds the table and attaches it to a PPU (or detaches it when neutral)
void filters_apply(Ppu* ppu, int mode, int brightness, int contrast, int saturation, int gamma);
