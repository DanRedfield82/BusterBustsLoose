#pragma once
#include <stdbool.h>
#include "bbl.h"
#include "cheats.h"

enum { DISPLAY_WINDOWED = 0, DISPLAY_BORDERLESS, DISPLAY_FULLSCREEN, DISPLAY_COUNT };
enum { WS_OFF = 0, WS_16_10, WS_16_9, WS_21_9, WS_AUTO, WS_COUNT };
enum { ASPECT_CRT = 0, ASPECT_SQUARE, ASPECT_STRETCH, ASPECT_COUNT };
enum { SCALE_SHARP = 0, SCALE_SMOOTH, SCALE_SHARP_BILINEAR, SCALE_INTEGER, SCALE_COUNT };

// Extra actions that can be bound besides the SNES buttons
enum {
  ACT_MENU = 0, ACT_FAST_FORWARD, ACT_QUICK_SAVE, ACT_QUICK_LOAD, ACT_SCREENSHOT,
  ACT_FULLSCREEN, ACT_WIDESCREEN, ACT_COUNT
};

#define PAD_UNBOUND -1
// pad bindings: 0..SDL_CONTROLLER_BUTTON_MAX-1 are buttons, PAD_AXIS_BASE + n are
// analog trigger axes (n = 0 left trigger, 1 right trigger)
#define PAD_AXIS_BASE 100

typedef struct Settings {
  // video
  int displayMode;
  int windowScale;
  int monitor;        // display index
  int widescreen;
  int lastWidescreen; // mode F10 turns back on
  int hudReanchor;
  int aspect;
  int scaling;
  int scanlines;      // 0..3
  int crtMask;        // aperture grille overlay
  int upscaler;       // UPSCALE_*
  int vsync;
  int spriteLimit;    // 0 original, 1 unlimited
  int showFps;
  // colour
  int colourMode;
  int brightness;     // percent
  int contrast;       // percent
  int saturation;     // percent
  int gamma;          // percent (100 = neutral)
  // audio
  int volume;         // 0..100
  int monoAudio;
  int muteWhenPaused;
  // gameplay
  int cheats[CHEAT_COUNT];
  int pauseOnFocusLoss;
  // input
  int keyBind[BTN_COUNT][2];      // two SDL scancodes per SNES button
  int keyAction[ACT_COUNT];
  int padBind[BTN_COUNT];         // SDL_GameControllerButton / PAD_AXIS_BASE+n
  int padAction[ACT_COUNT];
  int analogStick;                // left stick drives the d-pad
  int selectOpensMenu;            // SNES Select opens the menu instead of reaching the game
  int fastForward;                // the fast forward button works
  // misc
  char romPath[512];
  int saveSlot;
  int skipIntro;        // level select skips the stage intro
  int levelDifficulty;  // $6C: 0 children, 1 normal, 2 challenge
} Settings;

extern Settings cfg;

void config_defaults(Settings* s);
void config_defaultKeys(Settings* s);
void config_defaultPad(Settings* s);
bool config_load(const char* path);
void config_validate(void);
bool config_save(const char* path);
