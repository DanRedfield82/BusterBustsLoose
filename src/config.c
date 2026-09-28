#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "config.h"
#include "upscale.h"
#include "filters.h"

Settings cfg;

void config_defaultKeys(Settings* s) {
  static const int keys[BTN_COUNT][2] = {
    [BTN_B]      = { SDL_SCANCODE_Z, SDL_SCANCODE_SPACE },
    [BTN_Y]      = { SDL_SCANCODE_A, SDL_SCANCODE_LSHIFT },
    [BTN_SELECT] = { SDL_SCANCODE_RSHIFT, SDL_SCANCODE_BACKSPACE },
    [BTN_START]  = { SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER },
    [BTN_UP]     = { SDL_SCANCODE_UP, SDL_SCANCODE_I },
    [BTN_DOWN]   = { SDL_SCANCODE_DOWN, SDL_SCANCODE_K },
    [BTN_LEFT]   = { SDL_SCANCODE_LEFT, SDL_SCANCODE_J },
    [BTN_RIGHT]  = { SDL_SCANCODE_RIGHT, SDL_SCANCODE_L },
    [BTN_A]      = { SDL_SCANCODE_X, 0 },
    [BTN_X]      = { SDL_SCANCODE_S, 0 },
    [BTN_L]      = { SDL_SCANCODE_Q, 0 },
    [BTN_R]      = { SDL_SCANCODE_W, 0 },
  };
  memcpy(s->keyBind, keys, sizeof(keys));
  s->keyAction[ACT_MENU] = SDL_SCANCODE_ESCAPE;
  s->keyAction[ACT_FAST_FORWARD] = SDL_SCANCODE_TAB;
  s->keyAction[ACT_QUICK_SAVE] = SDL_SCANCODE_F5;
  s->keyAction[ACT_QUICK_LOAD] = SDL_SCANCODE_F9;
  s->keyAction[ACT_SCREENSHOT] = SDL_SCANCODE_F12;
  s->keyAction[ACT_FULLSCREEN] = SDL_SCANCODE_F11;
  s->keyAction[ACT_WIDESCREEN] = SDL_SCANCODE_F10;
}

void config_defaultPad(Settings* s) {
  // SNES layout by position: SNES B is the bottom face button, A the right one
  s->padBind[BTN_B] = SDL_CONTROLLER_BUTTON_A;
  s->padBind[BTN_A] = SDL_CONTROLLER_BUTTON_B;
  s->padBind[BTN_Y] = SDL_CONTROLLER_BUTTON_X;
  s->padBind[BTN_X] = SDL_CONTROLLER_BUTTON_Y;
  s->padBind[BTN_L] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  s->padBind[BTN_R] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  s->padBind[BTN_SELECT] = SDL_CONTROLLER_BUTTON_BACK;
  s->padBind[BTN_START] = SDL_CONTROLLER_BUTTON_START;
  s->padBind[BTN_UP] = SDL_CONTROLLER_BUTTON_DPAD_UP;
  s->padBind[BTN_DOWN] = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  s->padBind[BTN_LEFT] = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  s->padBind[BTN_RIGHT] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  for(int i = 0; i < ACT_COUNT; i++) s->padAction[i] = PAD_UNBOUND;
  s->padAction[ACT_MENU] = SDL_CONTROLLER_BUTTON_GUIDE;
  s->padAction[ACT_FAST_FORWARD] = PAD_AXIS_BASE + 1; // right trigger
  s->padAction[ACT_QUICK_SAVE] = PAD_UNBOUND;
  s->padAction[ACT_QUICK_LOAD] = PAD_UNBOUND;
  s->analogStick = 1;
  s->selectOpensMenu = 1;
}

void config_defaults(Settings* s) {
  memset(s, 0, sizeof(*s));
  s->displayMode = DISPLAY_WINDOWED;
  s->windowScale = 3;
  s->widescreen = WS_16_9;
  s->lastWidescreen = WS_16_9;
  s->hudReanchor = 1;
  s->aspect = ASPECT_CRT;
  s->scaling = SCALE_SHARP;
  s->scanlines = 0;
  s->crtMask = 0;
  s->upscaler = 0;
  s->vsync = 1;
  s->spriteLimit = 1;
  s->showFps = 0;
  s->colourMode = 0;
  s->brightness = 100;
  s->contrast = 100;
  s->saturation = 100;
  s->gamma = 100;
  s->volume = 80;
  s->monoAudio = 0;
  s->muteWhenPaused = 1;
  s->pauseOnFocusLoss = 1;
  s->fastForward = 1;
  s->saveSlot = 1;
  s->levelDifficulty = 1;
  s->skipIntro = 1;
  config_defaultKeys(s);
  config_defaultPad(s);
}

typedef struct IniInt {
  const char* key;
  int* ptr;
} IniInt;

static int buildTable(IniInt* t) {
  int n = 0;
#define I(k, p) t[n].key = k; t[n].ptr = &(p); n++;
  I("display_mode", cfg.displayMode);
  I("window_scale", cfg.windowScale);
  I("monitor", cfg.monitor);
  I("widescreen", cfg.widescreen);
  I("last_widescreen", cfg.lastWidescreen);
  I("hud_reanchor", cfg.hudReanchor);
  I("aspect", cfg.aspect);
  I("scaling", cfg.scaling);
  I("scanlines", cfg.scanlines);
  I("crt_mask", cfg.crtMask);
  I("upscaler", cfg.upscaler);
  I("vsync", cfg.vsync);
  I("unlimited_sprites", cfg.spriteLimit);
  I("show_fps", cfg.showFps);
  I("colour_mode", cfg.colourMode);
  I("brightness", cfg.brightness);
  I("contrast", cfg.contrast);
  I("saturation", cfg.saturation);
  I("gamma", cfg.gamma);
  I("volume", cfg.volume);
  I("mono_audio", cfg.monoAudio);
  I("mute_when_paused", cfg.muteWhenPaused);
  I("pause_on_focus_loss", cfg.pauseOnFocusLoss);
  I("analog_stick", cfg.analogStick);
  I("select_opens_menu", cfg.selectOpensMenu);
  I("fast_forward", cfg.fastForward);
  I("save_slot", cfg.saveSlot);
  I("level_difficulty", cfg.levelDifficulty);
  I("skip_intro", cfg.skipIntro);
  static char names[CHEAT_COUNT + BTN_COUNT * 3 + ACT_COUNT * 2][32];
  int ni = 0;
  for(int i = 0; i < CHEAT_COUNT; i++) {
    snprintf(names[ni], 32, "cheat_%d", i);
    I(names[ni], cfg.cheats[i]); ni++;
  }
  for(int i = 0; i < BTN_COUNT; i++) {
    snprintf(names[ni], 32, "key_%d_a", i); I(names[ni], cfg.keyBind[i][0]); ni++;
    snprintf(names[ni], 32, "key_%d_b", i); I(names[ni], cfg.keyBind[i][1]); ni++;
    snprintf(names[ni], 32, "pad_%d", i); I(names[ni], cfg.padBind[i]); ni++;
  }
  for(int i = 0; i < ACT_COUNT; i++) {
    snprintf(names[ni], 32, "key_action_%d", i); I(names[ni], cfg.keyAction[i]); ni++;
    snprintf(names[ni], 32, "pad_action_%d", i); I(names[ni], cfg.padAction[i]); ni++;
  }
#undef I
  return n;
}

bool config_load(const char* path) {
  config_defaults(&cfg);
  FILE* f = fopen(path, "r");
  if(!f) return false;
  static IniInt table[256];
  int n = buildTable(table);
  char line[1024];
  while(fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    char* eq = strchr(line, '=');
    if(!eq || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
    *eq = 0;
    const char* key = line;
    const char* val = eq + 1;
    if(!strcmp(key, "rom_path")) {
      snprintf(cfg.romPath, sizeof(cfg.romPath), "%s", val);
      continue;
    }
    for(int i = 0; i < n; i++) {
      if(!strcmp(table[i].key, key)) { *table[i].ptr = atoi(val); break; }
    }
  }
  fclose(f);
  config_validate();
  return true;
}

static void clampInt(int* v, int lo, int hi, int def) {
  if(*v < lo || *v > hi) *v = def;
}

static bool validPad(int b) {
  return b == PAD_UNBOUND || (b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX) || b == PAD_AXIS_BASE || b == PAD_AXIS_BASE + 1;
}

// out-of-range values (hand-edited ini) fall back to the defaults
void config_validate(void) {
  Settings d;
  config_defaults(&d);
  clampInt(&cfg.displayMode, 0, DISPLAY_COUNT - 1, d.displayMode);
  clampInt(&cfg.windowScale, 1, 8, d.windowScale);
  clampInt(&cfg.monitor, 0, 15, 0);
  clampInt(&cfg.widescreen, 0, WS_COUNT - 1, d.widescreen);
  clampInt(&cfg.lastWidescreen, WS_16_10, WS_COUNT - 1, d.widescreen);
  clampInt(&cfg.hudReanchor, 0, 1, d.hudReanchor);
  clampInt(&cfg.aspect, 0, ASPECT_COUNT - 1, d.aspect);
  clampInt(&cfg.scaling, 0, SCALE_COUNT - 1, d.scaling);
  clampInt(&cfg.scanlines, 0, 3, d.scanlines);
  clampInt(&cfg.crtMask, 0, 1, d.crtMask);
  clampInt(&cfg.upscaler, 0, UPSCALE_COUNT - 1, d.upscaler);
  clampInt(&cfg.vsync, 0, 1, d.vsync);
  clampInt(&cfg.spriteLimit, 0, 1, d.spriteLimit);
  clampInt(&cfg.showFps, 0, 1, d.showFps);
  clampInt(&cfg.colourMode, 0, COLOUR_COUNT - 1, d.colourMode);
  clampInt(&cfg.brightness, 50, 150, d.brightness);
  clampInt(&cfg.contrast, 50, 150, d.contrast);
  clampInt(&cfg.saturation, 0, 200, d.saturation);
  clampInt(&cfg.gamma, 50, 150, d.gamma);
  clampInt(&cfg.volume, 0, 100, d.volume);
  clampInt(&cfg.monoAudio, 0, 1, d.monoAudio);
  clampInt(&cfg.muteWhenPaused, 0, 1, d.muteWhenPaused);
  clampInt(&cfg.pauseOnFocusLoss, 0, 1, d.pauseOnFocusLoss);
  clampInt(&cfg.analogStick, 0, 1, d.analogStick);
  clampInt(&cfg.selectOpensMenu, 0, 1, d.selectOpensMenu);
  clampInt(&cfg.fastForward, 0, 1, d.fastForward);
  clampInt(&cfg.saveSlot, 1, 9, d.saveSlot);
  clampInt(&cfg.levelDifficulty, 0, 2, d.levelDifficulty);
  clampInt(&cfg.skipIntro, 0, 1, d.skipIntro);
  for(int i = 0; i < CHEAT_COUNT; i++) clampInt(&cfg.cheats[i], 0, 1, 0);
  for(int i = 0; i < BTN_COUNT; i++) {
    for(int j = 0; j < 2; j++) clampInt(&cfg.keyBind[i][j], 0, SDL_NUM_SCANCODES - 1, d.keyBind[i][j]);
    if(!validPad(cfg.padBind[i])) cfg.padBind[i] = d.padBind[i];
  }
  for(int i = 0; i < ACT_COUNT; i++) {
    clampInt(&cfg.keyAction[i], 0, SDL_NUM_SCANCODES - 1, d.keyAction[i]);
    if(!validPad(cfg.padAction[i])) cfg.padAction[i] = d.padAction[i];
  }
}

bool config_save(const char* path) {
  FILE* f = fopen(path, "w");
  if(!f) return false;
  static IniInt table[256];
  int n = buildTable(table);
  fprintf(f, "# Buster Busts Loose! PC settings. Delete this file to restore defaults.\n");
  fprintf(f, "rom_path=%s\n", cfg.romPath);
  for(int i = 0; i < n; i++) fprintf(f, "%s=%d\n", table[i].key, *table[i].ptr);
  fclose(f);
  return true;
}
