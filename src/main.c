// Buster Busts Loose! PC port - frontend
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <psapi.h>
#include <io.h>
#include <direct.h>
#endif
#include "stb_image_write.h"
#include "app.h"
#include "bbl.h"
#include "config.h"
#include "finput.h"
#include "audio.h"
#include "menu.h"
#include "ui.h"
#include "cheats.h"
#include "filters.h"
#include "upscale.h"
#include "hud.h"
#include "bgtask.h"
#include "music.h"
#include "levels.h"
#include "touch.h"
#ifdef __ANDROID__
#include <unistd.h>
#include <jni.h>
#endif

#define APP_TITLE "Buster Busts Loose!"

// automated test script state (see --script)
static FILE* scriptFile;
static int scriptWait;
static uint16_t scriptHeld;
static int scriptHoldFrames;
static bool scriptWaitReady;
static int scriptTicks;
static uint32_t scriptSleepUntil;
static char scriptWaitFile[512];
// script "rec <path>": each loop runs one game frame and writes the window's
// picture to <path>.vid (per frame: uint16 width, height, then RGB rows) and
// the sound to <path>.pcm (48 kHz stereo s16, frame-locked) (trailer footage)
static FILE* recVidF;
static FILE* recPcmF;
static double recFracF;

static int recSamples(void) {
  double want = AUDIO_RATE / SNES_FPS + recFracF;
  int n = (int)want;
  recFracF = want - n;
  return n;
}

static struct {
  SDL_Window* win;
  SDL_Renderer* ren;
  SDL_Texture* gameTex;
  int gameTexW, gameTexH;
  SDL_ScaleMode gameTexMode;
  SDL_Texture* scanTex;
  int scanTexH, scanTexLevel;
  SDL_Texture* maskTex;
  int maskTexW;
  Bbl* game;
  bool running;
  char baseDir[1024];
  char dataDir[1024];
  char cfgPath[1100];
  char romPath[1024];
  uint32_t* upBuf;
  size_t upCap;
  char toast[200];
  uint32_t toastUntil;
  bool focusLost;
  // level select sequencing
  int lsPhase;
  int lsFrames;
  int lsArea;
  // timing
  uint64_t lastCounter;
  double frameAcc;
  double fps, loopsPerSec;
  int fpsFrames, fpsLoops;
  uint32_t fpsTicks;
  int16_t audioBuf[4096 * 2];
} app;

// ------------------------------------------------------------------ helpers

Bbl* app_game(void) { return app.game; }
int app_windowWidth(void) { int w, h; SDL_GetRendererOutputSize(app.ren, &w, &h); return w; }
int app_windowHeight(void) { int w, h; SDL_GetRendererOutputSize(app.ren, &w, &h); return h; }

void app_toast(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(app.toast, sizeof(app.toast), fmt, ap);
  va_end(ap);
  app.toastUntil = SDL_GetTicks() + 2500;
}

static bool dirWritable(const char* dir) {
  char path[1200];
  snprintf(path, sizeof(path), "%s.write_test", dir);
  FILE* f = fopen(path, "wb");
  if(!f) return false;
  fclose(f);
  remove(path);
  return true;
}

static void makeDir(const char* path) {
#ifdef _WIN32
  _mkdir(path);
#else
  mkdir(path, 0755);
#endif
}

void app_saveConfig(void) {
  config_save(app.cfgPath);
}

static double parFor(int aspect) {
  return aspect == ASPECT_CRT ? 8.0 / 7.0 : 1.0;
}

static int computeExtraCols(void) {
  double target;
  switch(cfg.widescreen) {
    case WS_16_10: target = 16.0 / 10.0; break;
    case WS_16_9: target = 16.0 / 9.0; break;
    case WS_21_9: target = 21.0 / 9.0; break;
    case WS_AUTO: {
      int w = 16, h = 9;
      if(app.ren) SDL_GetRendererOutputSize(app.ren, &w, &h);
      target = h > 0 ? (double)w / h : 16.0 / 9.0;
      break;
    }
    default: return 0;
  }
  double par = cfg.aspect == ASPECT_STRETCH ? 1.0 : parFor(cfg.aspect);
  double width = 224.0 * target / par;
  int extra = (int)((width - 256.0) / 2.0 + 0.5);
  if(extra < 0) extra = 0;
  // the 512 px tilemap ring holds the view plus one streamed column each side:
  // 256 + 2 * 116 + 16 = 504
  if(extra > 116) extra = 116;
  return extra;
}

// the picture size in display pixels (before window scaling)
static void pictureSize(int* w, int* h) {
  int cols = 256 + 2 * computeExtraCols();
  double par = cfg.aspect == ASPECT_STRETCH ? parFor(ASPECT_CRT) : parFor(cfg.aspect);
  *w = (int)(cols * par + 0.5);
  *h = 224;
}

static int targetDisplay(void) {
  int n = SDL_GetNumVideoDisplays();
  return (cfg.monitor >= 0 && cfg.monitor < n) ? cfg.monitor : 0;
}

static void resizeWindowToScale(void) {
#ifdef __ANDROID__
  return;   // (the window is the whole screen)
#endif
  if(cfg.displayMode != DISPLAY_WINDOWED) return;
  int pw, ph;
  pictureSize(&pw, &ph);
  SDL_Rect usable;
  int disp = targetDisplay();
  int scale = cfg.windowScale;
  if(SDL_GetDisplayUsableBounds(disp, &usable) == 0) {
    while(scale > 1 && (pw * scale > usable.w - 16 || ph * scale > usable.h - 40)) scale--;
  }
  // round the width up so integer scaling fits the exact multiple
  int cols = 256 + 2 * computeExtraCols();
  double par = cfg.aspect == ASPECT_STRETCH ? parFor(ASPECT_CRT) : parFor(cfg.aspect);
  SDL_SetWindowSize(app.win, (int)ceil(cols * par * scale), ph * scale);
  SDL_SetWindowPosition(app.win, SDL_WINDOWPOS_CENTERED_DISPLAY(disp), SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
}

void app_applyVideo(void) {
#ifdef __ANDROID__
  // (always the whole screen: only V-Sync applies)
  SDL_RenderSetVSync(app.ren, cfg.vsync ? 1 : 0);
  return;
#endif
  int disp = targetDisplay();
  // leave fullscreen first so the window can move to the chosen monitor
  if(SDL_GetWindowFlags(app.win) & SDL_WINDOW_FULLSCREEN) SDL_SetWindowFullscreen(app.win, 0);
  if(cfg.displayMode == DISPLAY_WINDOWED) {
    resizeWindowToScale();
  } else {
    SDL_Rect b;
    if(SDL_GetDisplayBounds(disp, &b) == 0) SDL_SetWindowPosition(app.win, b.x + b.w / 4, b.y + b.h / 4);
    if(cfg.displayMode == DISPLAY_FULLSCREEN) {
      SDL_DisplayMode dm;
      if(SDL_GetDesktopDisplayMode(disp, &dm) == 0) SDL_SetWindowDisplayMode(app.win, &dm);
      SDL_SetWindowFullscreen(app.win, SDL_WINDOW_FULLSCREEN);
    } else {
      SDL_SetWindowFullscreen(app.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
  }
  SDL_RenderSetVSync(app.ren, cfg.vsync ? 1 : 0);
  SDL_ShowCursor(cfg.displayMode == DISPLAY_WINDOWED ? SDL_ENABLE : SDL_DISABLE);
}

int app_displayCount(void) { return SDL_GetNumVideoDisplays(); }

void app_displayName(int i, char* out, int size) {
  const char* n = SDL_GetDisplayName(i);
  SDL_DisplayMode dm;
  if(SDL_GetDesktopDisplayMode(i, &dm) == 0) snprintf(out, size, "%d: %s (%dx%d)", i + 1, n ? n : "Display", dm.w, dm.h);
  else snprintf(out, size, "%d: %s", i + 1, n ? n : "Display");
}

void app_applyGameSettings(void) {
  if(!app.game) return;
  int oldExtra = app.game->extraCols;
  if(cfg.widescreen != WS_OFF) cfg.lastWidescreen = cfg.widescreen;
  bbl_setWidescreen(app.game, computeExtraCols());
  app.game->hudReanchor = cfg.hudReanchor != 0;
  app.game->noSpriteLimit = cfg.spriteLimit != 0;
  filters_apply(app.game->snes->ppu, cfg.colourMode, cfg.brightness, cfg.contrast, cfg.saturation, cfg.gamma);
  for(int i = 0; i < CHEAT_COUNT; i++) cheats_set(app.game, i, cfg.cheats[i]);
  if(oldExtra != app.game->extraCols && cfg.widescreen != WS_AUTO) resizeWindowToScale();
}

void app_quit(void) { app.running = false; }
void app_closeMenu(void) { menu_close(); }

// ------------------------------------------------------------------ states

static void statePath(int slot, char* out, int size) {
  snprintf(out, size, "%ssaves%cslot%d.state", app.dataDir, '/', slot);
}

static bool levelSelectHidden(void);

bool app_saveState(int slot) {
  // (the level select pokes difficulty and area state while it works)
  if(levelSelectHidden()) { app_toast("Wait for the level to load"); return false; }
  char path[1200];
  statePath(slot, path, sizeof(path));
  uint8_t* data;
  int size = bbl_saveState(app.game, &data);
  bool ok = bbl_writeFile(path, data, size);
  free(data);
  app_toast(ok ? "Saved to slot %d" : "Could not save slot %d", slot);
  return ok;
}

bool app_loadState(int slot) {
  char path[1200];
  statePath(slot, path, sizeof(path));
  uint8_t* data;
  int size;
  if(levelSelectHidden()) { app_toast("Wait for the level to load"); return false; }
  if(!bbl_readFile(path, &data, &size)) { app_toast("Slot %d is empty", slot); return false; }
  bool ok = bbl_loadState(app.game, data, size);
  free(data);
  if(ok) {
    app.lsPhase = 0;
    audio_clear();
  }
  app_toast(ok ? "Loaded slot %d" : "Slot %d is from another version", slot);
  return ok;
}

bool app_stateExists(int slot, char* info, int infoSize) {
  char path[1200];
  statePath(slot, path, sizeof(path));
  struct stat st;
  if(stat(path, &st) != 0) return false;
  struct tm* t = localtime(&st.st_mtime);
  if(t) strftime(info, infoSize, "Saved %Y-%m-%d %H:%M", t);
  else snprintf(info, infoSize, "Saved");
  return true;
}

void app_resetGame(void) {
  bbl_reset(app.game, true);
  app.lsPhase = 0;
  audio_clear();
  app_toast("Game reset");
}

// ------------------------------------------------------------------ level select

bool app_levelSelectReady(void) { return bg_ready(); }
bool app_levelSelectFailed(void) { return bg_failed(); }

enum {
  LS_IDLE = 0,
  LS_CONFIRM,      // hold Start (+Select for the bonus menu) on the stage select
  LS_INTRO,        // tap through the stage intro (hidden)
  LS_BONUS_WAIT,   // wait for the hidden bonus menu (hidden)
  LS_BONUS_PICK,   // choose the bonus game (hidden)
  LS_WAIT_PLAY,    // intro shown: wait for gameplay
  LS_ADVANCE,      // complete areas until the chosen one (hidden)
};

// true while the level select is working behind the loading screen
static bool levelSelectHidden(void) {
  return app.lsPhase == LS_CONFIRM || app.lsPhase == LS_INTRO ||
         app.lsPhase == LS_BONUS_WAIT || app.lsPhase == LS_BONUS_PICK || app.lsPhase == LS_ADVANCE;
}

void app_startLevel(int entry) {
  if(entry < 0 || entry >= levelAreaCount) return;
  int size;
  const uint8_t* st = bg_state(&size);
  if(!st) return;
  if(!bbl_loadState(app.game, st, size)) { app_toast("Level select failed"); return; }
  const LevelArea* la = &levelAreas[entry];
  bbl_poke16(app.game, 0x7E006C, cfg.levelDifficulty);
  bbl_poke16(app.game, 0x7E0068, la->stage);
  // (the base state is past the new game setup the cheat patches)
  if(cfg.cheats[CHEAT_99_LIVES]) bbl_poke(app.game, 0x7E008E, 0x99);
  // The debug stage select can start any area, but an area started directly
  // lacks the graphics the stage loads on the way (the Brick Corridor's walls,
  // the Library...) and its music. So stages always start at their first area,
  // and the chosen one is reached by completing the areas before it.
  bbl_poke16(app.game, 0x7E0094, la->kind == LEVEL_AREA ? 0 : la->area);
  app.lsArea = entry;
  app.lsPhase = LS_CONFIRM;
  app.lsFrames = 0;
  audio_clear();
}

// called every emulated frame; returns the joypad override (or -1 for none)
static int levelSelectTick(void) {
  if(app.lsPhase == LS_IDLE) return -1;
  Bbl* g = app.game;
  const LevelArea* la = &levelAreas[app.lsArea];
  app.lsFrames++;
  // the debug stage select flag is only needed until the choice is made
  if(app.lsPhase != LS_CONFIRM && !cfg.cheats[CHEAT_STAGE_SELECT]) bbl_poke(g, 0x7E1D7E, 0);
  switch(app.lsPhase) {
    case LS_CONFIRM:
      if(app.lsFrames < 6) {
        // holding Select while confirming opens the bonus section instead
        return (1 << BTN_START) | (la->kind == LEVEL_BONUS ? (1 << BTN_SELECT) : 0);
      }
      app.lsFrames = 0;
      if(la->kind == LEVEL_BONUS) app.lsPhase = LS_BONUS_WAIT;
      else if(la->kind == LEVEL_EXTRA) {
        // (no later tick clears the stage select flag)
        if(!cfg.cheats[CHEAT_STAGE_SELECT]) bbl_poke(g, 0x7E1D7E, 0);
        app.lsPhase = LS_IDLE;
      }
      // the stage intro belongs to the first area
      else app.lsPhase = (cfg.skipIntro || la->area > 0) ? LS_INTRO : LS_WAIT_PLAY;
      return 0;
    case LS_BONUS_WAIT:
      // the menu is ready well within this time
      if(app.lsFrames >= 200) {
        bbl_poke16(g, 0x7E0442, la->area);
        app.lsPhase = LS_BONUS_PICK;
        app.lsFrames = 0;
      }
      return 0;
    case LS_BONUS_PICK:
      if(app.lsFrames < 6) return 1 << BTN_START;
      app.lsPhase = LS_IDLE;
      return 0;
    case LS_INTRO:
    case LS_WAIT_PLAY:
      // playing ($80 = 4 once the area has loaded)
      if(hud_inGameplay(g) && bbl_peek16(g, 0x7E0080) == 4) {
        if(la->kind == LEVEL_AREA && bbl_peek16(g, 0x7E0094) < la->area) {
          // Children difficulty leaves the last areas of some stages out:
          // complete the areas on Normal, then switch back
          uint16_t counts = bbl_romRead(g, 0x818B13 + la->stage * 2) | (bbl_romRead(g, 0x818B14 + la->stage * 2) << 8);
          int count = cfg.levelDifficulty ? (counts & 0xff) : (counts >> 8);
          if(la->area >= count) bbl_poke16(g, 0x7E006C, 1);
          bbl_poke16(g, 0x7E0080, 5);             // area complete ($80:A5C6)
          app.lsPhase = LS_ADVANCE;
          app.lsFrames = 0;
          return 0;
        }
        app.lsPhase = LS_IDLE;
        return 0;
      }
      if(app.lsFrames > 60 * 120) { app.lsPhase = LS_IDLE; return -1; }
      if(app.lsPhase == LS_INTRO) {
        // $32 = 9 is the stage intro; Start pages through its text
        if(bbl_peek16(g, 0x7E0032) == 9) return (app.lsFrames % 16) < 8 ? (1 << BTN_START) : 0;
        return 0;
      }
      return -1;
    case LS_ADVANCE:
      if(app.lsFrames > 60 * 60) { bbl_poke16(g, 0x7E006C, cfg.levelDifficulty); app.lsPhase = LS_IDLE; return -1; }
      if(hud_inGameplay(g) && bbl_peek16(g, 0x7E0080) == 4) {
        if(bbl_peek16(g, 0x7E0094) < la->area) {
          bbl_poke16(g, 0x7E0080, 5);
          return 0;
        }
        bbl_poke16(g, 0x7E006C, cfg.levelDifficulty);
        app.lsPhase = LS_IDLE;
      }
      return 0;
  }
  return -1;
}

// ------------------------------------------------------------------ music

bool app_musicOpen(void) {
  if(music_isOpen()) return true;
  int size;
  const uint8_t* st = bg_state(&size);
  if(!st) return false;
  if(!music_open(app.romPath, st, size)) return false;
  audio_clear();
  return true;
}

void app_musicClose(void) {
  if(music_isOpen()) music_close();
  audio_clear();
}

// ------------------------------------------------------------------ rendering

// after a render target / device reset every texture is recreated on demand
static void resetTextures(void) {
  if(app.gameTex) SDL_DestroyTexture(app.gameTex);
  if(app.scanTex) SDL_DestroyTexture(app.scanTex);
  if(app.maskTex) SDL_DestroyTexture(app.maskTex);
  app.gameTex = app.scanTex = app.maskTex = NULL;
  app.gameTexW = app.gameTexH = app.scanTexH = app.maskTexW = 0;
  ui_shutdown();
  ui_init(app.ren);
}

static void ensureGameTexture(int w, int h, SDL_ScaleMode mode) {
  if(app.gameTex && app.gameTexW == w && app.gameTexH == h) {
    if(mode != app.gameTexMode) { SDL_SetTextureScaleMode(app.gameTex, mode); app.gameTexMode = mode; }
    return;
  }
  if(app.gameTex) SDL_DestroyTexture(app.gameTex);
  app.gameTex = SDL_CreateTexture(app.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
  SDL_SetTextureScaleMode(app.gameTex, mode);
  app.gameTexW = w;
  app.gameTexH = h;
  app.gameTexMode = mode;
}

static void ensureScanTexture(int lines, int level) {
  if(app.scanTex && app.scanTexH == lines && app.scanTexLevel == level) return;
  if(app.scanTex) SDL_DestroyTexture(app.scanTex);
  int h = lines * 2;
  uint32_t* px = malloc(h * 4);
  static const int alpha[4] = { 0, 70, 120, 170 };
  for(int i = 0; i < h; i++) px[i] = (i & 1) ? ((uint32_t)alpha[level] << 24) : 0;
  app.scanTex = SDL_CreateTexture(app.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, h);
  SDL_UpdateTexture(app.scanTex, NULL, px, 4);
  SDL_SetTextureBlendMode(app.scanTex, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(app.scanTex, SDL_ScaleModeLinear);
  free(px);
  app.scanTexH = lines;
  app.scanTexLevel = level;
}

static void ensureMaskTexture(int w) {
  if(app.maskTex && app.maskTexW == w) return;
  if(app.maskTex) SDL_DestroyTexture(app.maskTex);
  uint32_t* px = malloc(w * 4);
  static const uint32_t pattern[3] = { 0xffffc8c8, 0xffc8ffc8, 0xffc8c8ff };
  for(int i = 0; i < w; i++) px[i] = pattern[i % 3];
  app.maskTex = SDL_CreateTexture(app.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, 1);
  SDL_UpdateTexture(app.maskTex, NULL, px, w * 4);
  SDL_SetTextureBlendMode(app.maskTex, SDL_BLENDMODE_MOD);
  SDL_SetTextureScaleMode(app.maskTex, SDL_ScaleModeNearest);
  free(px);
  app.maskTexW = w;
}

static void renderFrame(void) {
  int W, H;
  SDL_GetRendererOutputSize(app.ren, &W, &H);
  SDL_SetRenderDrawColor(app.ren, 0, 0, 0, 255);
  SDL_RenderClear(app.ren);

  Bbl* g = app.game;
  int w = bbl_width(g), h = bbl_height(g);
  const uint32_t* src = bbl_pixels(g) + PPU_MAX_EXTRA - g->extraCols;

  // destination rectangle
  double par = cfg.aspect == ASPECT_SQUARE ? 1.0 : 8.0 / 7.0;
  double dispW = w * par, dispH = h;
  SDL_Rect dst;
  if(cfg.aspect == ASPECT_STRETCH) {
    dst.x = 0; dst.y = 0; dst.w = W; dst.h = H;
  } else {
    double scale = SDL_min(W / dispW, H / dispH);
    if(cfg.scaling == SCALE_INTEGER && scale >= 1.0) {
      int s = (int)(H / dispH);
      while(s > 1 && dispW * s > W + 2) s--;
      scale = s;
    }
    dst.w = (int)(dispW * scale + 0.5);
    if(dst.w > W) dst.w = W;
    dst.h = (int)(dispH * scale + 0.5);
    dst.x = (W - dst.w) / 2;
    dst.y = (H - dst.h) / 2;
  }

  // upload (with optional upscaling / prescaling)
  int filter = cfg.upscaler;
  int k = upscale_factor(filter);
  if(filter == UPSCALE_NONE && cfg.scaling == SCALE_SHARP_BILINEAR) {
    k = (dst.h + h - 1) / h;
    if(k < 1) k = 1;
    if(k > 6) k = 6;
  }
  SDL_ScaleMode mode = (cfg.scaling == SCALE_SHARP || cfg.scaling == SCALE_INTEGER) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
  ensureGameTexture(w * k, h * k, mode);
  if(k == 1) {
    SDL_UpdateTexture(app.gameTex, NULL, src, PPU_BUF_W * 4);
  } else {
    size_t need = (size_t)w * h * k * k;
    if(need > app.upCap) {
      free(app.upBuf);
      app.upBuf = malloc(need * 4);
      app.upCap = need;
    }
    if(filter == UPSCALE_NONE) upscale_nearest(src, w, h, PPU_BUF_W, app.upBuf, k);
    else upscale_run(filter, src, w, h, PPU_BUF_W, app.upBuf);
    SDL_UpdateTexture(app.gameTex, NULL, app.upBuf, w * k * 4);
  }
  if(!levelSelectHidden()) SDL_RenderCopy(app.ren, app.gameTex, NULL, &dst);

  if(cfg.scanlines > 0) {
    ensureScanTexture(h, cfg.scanlines);
    SDL_RenderCopy(app.ren, app.scanTex, NULL, &dst);
  }
  if(cfg.crtMask) {
    ensureMaskTexture(dst.w);
    SDL_RenderCopy(app.ren, app.maskTex, NULL, &dst);
  }

  int s = H / 360;
  if(s < 1) s = 1;
  if(!menu_isOpen() && levelSelectHidden()) {
    const char* t = "Loading level...";
    ui_textShadow((W - ui_textWidth(t, s * 2)) / 2, H / 2 - 8 * s, t, UI_WHITE, s * 2);
  }
  menu_draw(W, H);
  touch_render(app.ren, W, H);
  if(SDL_GetTicks() < app.toastUntil) {
    int tw = ui_textWidth(app.toast, s) + 16 * s;
    ui_frame(W - tw - 8 * s, 8 * s, tw, 14 * s, (UiColor){ 14, 26, 48, 232 }, (UiColor){ 76, 134, 200, 255 }, s > 1 ? s * 3 / 4 : 1);
    ui_text(W - tw, 11 * s + s / 2, app.toast, UI_WHITE, s);
  }
  if(cfg.showFps) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f FPS", app.fps);
    // bottom right, clear of the game's HUD along the top
    int fs = s < 2 ? 2 : s;
    ui_textShadow(W - ui_textWidth(buf, fs) - 8 * fs, H - 16 * fs, buf, UI_GREEN, fs);
  }
}

static void render(void) {
  renderFrame();
  if(recVidF) {
    // (read back before presenting: flip-model swap chains discard the back buffer)
    int W, H;
    SDL_GetRendererOutputSize(app.ren, &W, &H);
    static uint8_t* px;
    static size_t cap;
    if((size_t)W * H * 3 > cap) { free(px); cap = (size_t)W * H * 3; px = malloc(cap); }
    if(px && SDL_RenderReadPixels(app.ren, NULL, SDL_PIXELFORMAT_RGB24, px, W * 3) == 0) {
      uint16_t hdr[2] = { (uint16_t)W, (uint16_t)H };
      fwrite(hdr, 2, 2, recVidF);
      fwrite(px, 3, (size_t)W * H, recVidF);
    }
  }
  SDL_RenderPresent(app.ren);
}

static void screenshot(void) {
  char dir[1100], path[1200];
  snprintf(dir, sizeof(dir), "%sscreenshots", app.dataDir);
  makeDir(dir);
  time_t now = time(NULL);
  struct tm* t = localtime(&now);
  char stamp[64];
  strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", t);
  snprintf(path, sizeof(path), "%s/bbl_%s.png", dir, stamp);
  Bbl* g = app.game;
  int w = bbl_width(g), h = bbl_height(g);
  const uint32_t* px = bbl_pixels(g) + PPU_MAX_EXTRA - g->extraCols;
  uint8_t* out = malloc(w * h * 3);
  for(int y = 0; y < h; y++) {
    for(int x = 0; x < w; x++) {
      uint32_t c = px[y * PPU_BUF_W + x];
      out[(y * w + x) * 3] = c >> 16;
      out[(y * w + x) * 3 + 1] = c >> 8;
      out[(y * w + x) * 3 + 2] = c;
    }
  }
  bool ok = stbi_write_png(path, w, h, 3, out, w * 3) != 0;
  free(out);
  app_toast(ok ? "Screenshot saved" : "Screenshot failed");
}

// ------------------------------------------------------------------ emulation

static void stepGame(bool withAudio) {
  Bbl* g = app.game;
  scriptTicks++;
  int override = levelSelectTick();
  uint16_t mask = override >= 0 ? (uint16_t)override : (in_snesMask() | scriptHeld);
  bbl_setButtons(g, 1, mask);
  cheats_preFrame(g);
  bbl_runFrame(g);
  cheats_postFrame(g);
  int n = recPcmF ? recSamples() : audio_samplesForFrame();
  bbl_getSamples(g, app.audioBuf, n);
  if(recPcmF) {
    if(levelSelectHidden()) memset(app.audioBuf, 0, (size_t)n * 4);
    fwrite(app.audioBuf, 4, n, recPcmF);
  }
  if(withAudio) {
    if(levelSelectHidden()) audio_pushSilence(n);
    else audio_push(app.audioBuf, n);
  }
}

static void handleActions(void) {
  if(in_actionPressed(ACT_MENU)) {
    if(menu_isOpen()) menu_close();
    else { menu_open(); if(cfg.muteWhenPaused) audio_clear(); }
    in_clearNav();
  }
  if(in_actionPressed(ACT_FULLSCREEN)) {
    cfg.displayMode = cfg.displayMode == DISPLAY_WINDOWED ? DISPLAY_BORDERLESS : DISPLAY_WINDOWED;
    app_applyVideo();
    app_saveConfig();
  }
  if(in_actionPressed(ACT_WIDESCREEN)) {
    cfg.widescreen = cfg.widescreen == WS_OFF ? cfg.lastWidescreen : WS_OFF;
    app_applyGameSettings();
    app_toast(cfg.widescreen ? "Widescreen on" : "Widescreen off");
    app_saveConfig();
  }
  if(menu_isOpen()) return;
  if(in_actionPressed(ACT_QUICK_SAVE)) app_saveState(cfg.saveSlot);
  if(in_actionPressed(ACT_QUICK_LOAD)) app_loadState(cfg.saveSlot);
  if(in_actionPressed(ACT_SCREENSHOT)) screenshot();
}

// ------------------------------------------------------------------ ROM discovery

// the first file found that is a ROM but the wrong one (PAL, Japanese, hacked),
// so the "ROM needed" message can say why it wasn't used
static char romRejected[768];

static Bbl* tryRom(const char* path, char* err, int errSize) {
  Bbl* g = bbl_create(path, err, errSize);
  if(g) snprintf(app.romPath, sizeof(app.romPath), "%s", path);
  else if(!romRejected[0] && strncmp(err, "Could not read", 14)) snprintf(romRejected, sizeof(romRejected), "%s", err);
  return g;
}

static Bbl* searchDir(const char* dir) {
#ifdef _WIN32
  static const char* patterns[] = { "*.sfc", "*.smc", "*.swc", "*.fig" };
  for(int p = 0; p < 4; p++) {
    char pat[1200];
    snprintf(pat, sizeof(pat), "%s%s", dir, patterns[p]);
    struct _finddata_t fd;
    intptr_t hnd = _findfirst(pat, &fd);
    if(hnd == -1) continue;
    do {
      char path[1200], err[512];
      snprintf(path, sizeof(path), "%s%s", dir, fd.name);
      Bbl* g = tryRom(path, err, sizeof(err));
      if(g) { _findclose(hnd); return g; }
    } while(_findnext(hnd, &fd) == 0);
    _findclose(hnd);
  }
#else
  (void)dir;
#endif
  return NULL;
}

static bool askForRom(char* out, int size) {
#ifdef _WIN32
  OPENFILENAMEA ofn;
  memset(&ofn, 0, sizeof(ofn));
  out[0] = 0;
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "SNES ROM (*.sfc;*.smc)\0*.sfc;*.smc;*.swc;*.fig\0All files\0*.*\0";
  ofn.lpstrFile = out;
  ofn.nMaxFile = size;
  ofn.lpstrTitle = "Locate your Tiny Toon Adventures: Buster Busts Loose! (USA) ROM";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  return GetOpenFileNameA(&ofn) != 0;
#else
  (void)out; (void)size;
  return false;
#endif
}

#ifdef __ANDROID__
// Opens Android's file picker (BBLActivity.pickRom, Java) and waits for the
// result: the chosen file copied into app storage. False if cancelled.
static bool androidPickRom(char* out, int size) {
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  jobject act = (jobject)SDL_AndroidGetActivity();
  if(!env || !act) return false;
  jclass cls = (*env)->GetObjectClass(env, act);
  jmethodID pick = (*env)->GetStaticMethodID(env, cls, "pickRom", "()V");
  jmethodID poll = (*env)->GetStaticMethodID(env, cls, "pollRomResult", "()Ljava/lang/String;");
  bool ok = false;
  if(pick && poll) {
    (*env)->CallStaticVoidMethod(env, cls, pick);
    for(;;) {
      SDL_PumpEvents();
      SDL_Delay(50);
      jstring s = (jstring)(*env)->CallStaticObjectMethod(env, cls, poll);
      if(!s) continue;   // (still choosing)
      const char* p = (*env)->GetStringUTFChars(env, s, NULL);
      snprintf(out, size, "%s", p ? p : "");
      ok = p && p[0];
      if(p) (*env)->ReleaseStringUTFChars(env, s, p);
      (*env)->DeleteLocalRef(env, s);
      break;
    }
  }
  (*env)->DeleteLocalRef(env, cls);
  (*env)->DeleteLocalRef(env, act);
  return ok;
}
#endif

static Bbl* findRom(int argc, char** argv) {
  char err[512] = {0};
  Bbl* g = NULL;
  for(int i = 1; i < argc && !g; i++) {
    if(!strcmp(argv[i], "--script")) { i++; continue; }
    if(argv[i][0] != '-') g = tryRom(argv[i], err, sizeof(err));
  }
  if(!g && cfg.romPath[0]) g = tryRom(cfg.romPath, err, sizeof(err));
#ifdef __ANDROID__
  // the ROM picked once with Android's file picker, copied into app storage
  // (BBLActivity.pickRom); asked for again until a usable one is chosen
  char kept[1100];
  snprintf(kept, sizeof(kept), "%srom.sfc", app.dataDir);
  if(!g) g = tryRom(kept, err, sizeof(err));
  while(!g) {
    char msg[1024];
    if(romRejected[0]) snprintf(msg, sizeof(msg), "That file can't be used.\n\n%s\n\nPlease choose the USA version.", romRejected);
    else snprintf(msg, sizeof(msg), "This port needs the original game ROM to run:\n\n"
      "Tiny Toon Adventures - Buster Busts Loose! (USA)\n\n"
      "Choose the .sfc file on the next screen. It is copied into the app, so you only do this once.");
    const SDL_MessageBoxButtonData buttons[] = {
      { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose ROM" },
      { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
    };
    const SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, NULL, APP_TITLE, msg, 2, buttons, NULL };
    int choice = 0;
    if(SDL_ShowMessageBox(&box, &choice) != 0 || choice != 1) return NULL;
    char path[1024];
    if(!androidPickRom(path, sizeof(path))) continue;   // (cancelled: ask again)
    romRejected[0] = 0;
    g = tryRom(path, err, sizeof(err));
    if(!g) {
      if(!romRejected[0]) snprintf(romRejected, sizeof(romRejected), "%s", err);
      remove(path);
    }
  }
#else
  if(!g) g = searchDir(app.baseDir);
  if(!g) {
    char cwd[1024];
    if(_getcwd(cwd, sizeof(cwd) - 2)) {
      strcat(cwd, "\\");
      g = searchDir(cwd);
    }
  }
  if(!g) {
    // developer layout: exe in build/Release, ROM in the project folder
    char up[1100];
    snprintf(up, sizeof(up), "%s..\\", app.baseDir);
    for(int i = 0; i < 3 && !g; i++) {
      g = searchDir(up);
      strcat(up, "..\\");
    }
  }
#endif
  if(!g && romRejected[0]) {
    char msg[1024];
    snprintf(msg, sizeof(msg), "A ROM was found but it isn't the right version.\n\n%s\n\n"
      "Select the USA version in the next window.", romRejected);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, APP_TITLE, msg, NULL);
  } else if(!g) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, APP_TITLE,
      "This port needs the original game ROM to run:\n\n"
      "    Tiny Toon Adventures - Buster Busts Loose! (USA)\n\n"
      "No ROM was found next to the program. Put the .sfc file in the same\n"
      "folder as the program, or select it in the next window\n"
      "(it will be remembered).", NULL);
  }
  while(!g) {
    char path[1024];
    if(!askForRom(path, sizeof(path))) return NULL;
    g = tryRom(path, err, sizeof(err));
    if(!g) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_TITLE, err, NULL);
  }
  snprintf(cfg.romPath, sizeof(cfg.romPath), "%s", app.romPath);
  return g;
}

// ------------------------------------------------------------------ test scripts
// --script <file> drives the real frontend for automated testing:
//   wait <frames> | nav <up|down|left|right|ok|back|extra> | hold <btn> <frames>
//   shot <file.png> | menu | level <stage> <area> | set <name> <value> | quit


static void scriptShot(const char* path) {
  // draw a fresh frame and read it back before presenting (flip-model swap
  // chains discard the back buffer on present)
  renderFrame();
  int W, H;
  SDL_GetRendererOutputSize(app.ren, &W, &H);
  uint8_t* px = malloc((size_t)W * H * 4);
  if(SDL_RenderReadPixels(app.ren, NULL, SDL_PIXELFORMAT_ABGR8888, px, W * 4) == 0) {
    stbi_write_png(path, W, H, 4, px, W * 4);
  }
  free(px);
  SDL_RenderPresent(app.ren);
}

static int navCode(const char* s) {
  if(!strcmp(s, "up")) return NAV_UP;
  if(!strcmp(s, "down")) return NAV_DOWN;
  if(!strcmp(s, "left")) return NAV_LEFT;
  if(!strcmp(s, "right")) return NAV_RIGHT;
  if(!strcmp(s, "ok")) return NAV_OK;
  if(!strcmp(s, "back")) return NAV_BACK;
  if(!strcmp(s, "extra")) return NAV_EXTRA;
  return NAV_NONE;
}

static int btnCode(const char* s) {
  static const char* names[BTN_COUNT] = { "b", "y", "select", "start", "up", "down", "left", "right", "a", "x", "l", "r" };
  for(int i = 0; i < BTN_COUNT; i++) if(!strcmp(s, names[i])) return i;
  return -1;
}

// called after render(); pendingShot is taken from the frame just drawn
static void scriptStep(void) {
  if(!scriptFile) return;
  if(scriptHoldFrames > 0 && --scriptHoldFrames == 0) scriptHeld = 0;
  if(scriptWait > 0) {
    // waits count emulated frames (or render loops while paused)
    scriptWait -= scriptTicks > 0 ? scriptTicks : 0;
    scriptTicks = 0;
    if(scriptWait > 0) return;
  }
  if(scriptSleepUntil && SDL_GetTicks() < scriptSleepUntil) return;
  if(scriptWaitFile[0]) {
    FILE* wf = fopen(scriptWaitFile, "rb");
    if(!wf) return;
    fclose(wf);
    scriptWaitFile[0] = 0;
  }
  scriptSleepUntil = 0;
  if(scriptWaitReady) {
    if(!bg_ready() && !bg_failed()) return;
    scriptWaitReady = false;
  }
  char line[512];
  while(fgets(line, sizeof(line), scriptFile)) {
    char cmd[64] = {0}, a1[256] = {0}, a2[256] = {0};
    sscanf(line, "%63s %255s %255s", cmd, a1, a2);
    if(!cmd[0] || cmd[0] == '#') continue;
    if(!strcmp(cmd, "wait")) { scriptWait = atoi(a1); return; }
    if(!strcmp(cmd, "nav")) { in_injectNav(navCode(a1)); scriptWait = 2; return; }
    if(!strcmp(cmd, "hold")) { int b = btnCode(a1); if(b >= 0) scriptHeld |= 1 << b; scriptHoldFrames = atoi(a2); continue; }
    if(!strcmp(cmd, "shot")) { scriptShot(a1); continue; }
    if(!strcmp(cmd, "key") || !strcmp(cmd, "keyup")) {
      // key <scancode name, '_' for spaces>: simulated key press/release
      for(char* p = a1; *p; p++) if(*p == '_') *p = ' ';
      SDL_Event ev;
      SDL_zero(ev);
      ev.type = !strcmp(cmd, "key") ? SDL_KEYDOWN : SDL_KEYUP;
      ev.key.keysym.scancode = SDL_GetScancodeFromName(a1);
      SDL_PushEvent(&ev);
      scriptWait = 2;
      return;
    }
    if(!strcmp(cmd, "status")) { fprintf(stderr, "menu=%d\n", menu_isOpen()); continue; }
    if(!strcmp(cmd, "menu")) { if(!menu_isOpen()) menu_open(); else menu_close(); continue; }
    if(!strcmp(cmd, "level")) { app_startLevel(atoi(a1)); continue; }
    if(!strcmp(cmd, "waitready")) { scriptWaitReady = true; return; }
    if(!strcmp(cmd, "set")) {
      static const struct { const char* name; int* v; bool video; } opts[] = {
        { "widescreen", &cfg.widescreen, false }, { "upscaler", &cfg.upscaler, false },
        { "scaling", &cfg.scaling, false }, { "scanlines", &cfg.scanlines, false },
        { "crtmask", &cfg.crtMask, false }, { "colour", &cfg.colourMode, false },
        { "aspect", &cfg.aspect, false }, { "monitor", &cfg.monitor, true },
        { "windowscale", &cfg.windowScale, true }, { "vsync", &cfg.vsync, true },
        { "hud", &cfg.hudReanchor, false }, { "spritelimit", &cfg.spriteLimit, false },
        { "fps", &cfg.showFps, false }, { "brightness", &cfg.brightness, false },
        { "contrast", &cfg.contrast, false }, { "saturation", &cfg.saturation, false },
        { "gamma", &cfg.gamma, false }, { "volume", &cfg.volume, false },
        { "mono", &cfg.monoAudio, false }, { "difficulty", &cfg.levelDifficulty, false },
        { "skipintro", &cfg.skipIntro, false }, { "slot", &cfg.saveSlot, false },
      };
      int v = atoi(a2);
      bool found = false;
      for(int i = 0; i < (int)(sizeof(opts) / sizeof(opts[0])); i++) {
        if(strcmp(a1, opts[i].name)) continue;
        *opts[i].v = v;
        if(opts[i].video) app_applyVideo();
        found = true;
      }
      if(!strncmp(a1, "cheat", 5) && a1[5]) {
        int c = atoi(a1 + 5);
        if(c >= 0 && c < CHEAT_COUNT) { cfg.cheats[c] = v; found = true; }
      }
      if(!found) fprintf(stderr, "set: unknown option %s\n", a1);
      app_applyGameSettings();
      continue;
    }
    if(!strcmp(cmd, "mem")) {
#ifdef _WIN32
      PROCESS_MEMORY_COUNTERS pmc;
      if(GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        fprintf(stderr, "mem working=%u KB private=%u KB fps=%.1f loops=%.1f\n", (unsigned)(pmc.WorkingSetSize / 1024),
                (unsigned)(pmc.PagefileUsage / 1024), app.fps, app.loopsPerSec);
#else
      fprintf(stderr, "mem fps=%.1f loops=%.1f\n", app.fps, app.loopsPerSec);
#endif
      continue;
    }
    if(!strcmp(cmd, "display")) { cfg.displayMode = atoi(a1); app_applyVideo(); scriptWait = 30; return; }
    if(!strcmp(cmd, "minimize")) { SDL_MinimizeWindow(app.win); scriptWait = 30; return; }
    if(!strcmp(cmd, "restore")) { SDL_RestoreWindow(app.win); SDL_RaiseWindow(app.win); scriptWait = 30; return; }
    if(!strcmp(cmd, "mark")) { FILE* mf = fopen(a1, "wb"); if(mf) fclose(mf); continue; }
    if(!strcmp(cmd, "waitfile")) { snprintf(scriptWaitFile, sizeof(scriptWaitFile), "%s", a1); return; }
    if(!strcmp(cmd, "view")) {
      Bbl* g = app.game;
      fprintf(stderr, "view extra=%d shift=%d cam=%d,%d w=%d h=%d clip=%d..%d state=%d/%d\n", g->extraCols, g->viewShift,
        bbl_peek16(g, 0x7E1902), bbl_peek16(g, 0x7E1912), bbl_peek(g, 0x7E1925), bbl_peek(g, 0x7E1924),
        g->snes->ppu->clipLeft, g->snes->ppu->clipRight, bbl_peek16(g, 0x7E0030), bbl_peek16(g, 0x7E0032));
      continue;
    }
    if(!strcmp(cmd, "loadstate")) {
      uint8_t* d; int n;
      if(bbl_readFile(a1, &d, &n)) { fprintf(stderr, "loadstate %s: %d\n", a1, bbl_loadState(app.game, d, n)); free(d); }
      continue;
    }
    if(!strcmp(cmd, "st")) {
      Bbl* g = app.game;
      fprintf(stderr, "st f=%u diff=%d lives=%02X bright=%d blank=%d mode=%d/%d state=%d stage=%d area=%d cam=%d,%d pc=%02X:%04X\n", g->frameCount, bbl_peek(g, 0x7E006C),
        bbl_peek(g, 0x7E008E), g->snes->ppu->brightness, g->snes->ppu->forcedBlank,
        bbl_peek16(g, 0x7E0030), bbl_peek16(g, 0x7E0032), bbl_peek16(g, 0x7E0080), bbl_peek16(g, 0x7E0068),
        bbl_peek16(g, 0x7E0094), bbl_peek16(g, 0x7E1902), bbl_peek16(g, 0x7E1912), g->snes->cpu->k, g->snes->cpu->pc);
      continue;
    }
    if(!strcmp(cmd, "verify")) {
      for(int l = 0; l < 2; l++) {
        int n, bad = bbl_viewVerify(app.game, l, &n, 0);
        fprintf(stderr, "verify L%d checked=%d bad=%d\n", l, n, bad);
      }
      continue;
    }
    if(!strcmp(cmd, "audio")) {
      float l, r;
      audio_levels(&l, &r);
      fprintf(stderr, "audio peak=%.3f,%.3f volume=%d mono=%d music=%d state=%d track=%d frames=%u\n", l, r, cfg.volume, cfg.monoAudio,
        music_isOpen(), music_state(), music_current(), music_playFrames());
      continue;
    }
    if(!strcmp(cmd, "sleep")) { scriptSleepUntil = SDL_GetTicks() + atoi(a1); return; }
    if(!strcmp(cmd, "info")) {
      int x, y, w, h, ow, oh;
      SDL_GetWindowPosition(app.win, &x, &y);
      SDL_GetWindowSize(app.win, &w, &h);
      SDL_GetRendererOutputSize(app.ren, &ow, &oh);
      fprintf(stderr, "window flags=%08X pos=%d,%d size=%dx%d out=%dx%d display=%d focus=%d ls=%d\n",
        SDL_GetWindowFlags(app.win), x, y, w, h, ow, oh, SDL_GetWindowDisplayIndex(app.win), !app.focusLost, app.lsPhase);
      continue;
    }
    if(!strcmp(cmd, "displays")) {
      for(int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        SDL_DisplayMode cur, desk;
        SDL_GetCurrentDisplayMode(i, &cur); SDL_GetDesktopDisplayMode(i, &desk);
        fprintf(stderr, "display %d %s: current %dx%d@%d desktop %dx%d@%d\n", i, SDL_GetDisplayName(i), cur.w, cur.h, cur.refresh_rate, desk.w, desk.h, desk.refresh_rate);
      }
      SDL_DisplayMode wm;
      SDL_GetWindowDisplayMode(app.win, &wm);
      fprintf(stderr, "window on display %d, window mode %dx%d@%d\n", SDL_GetWindowDisplayIndex(app.win), wm.w, wm.h, wm.refresh_rate);
      continue;
    }
    if(!strcmp(cmd, "renderer")) {
      SDL_RendererInfo ri;
      SDL_GetRendererInfo(app.ren, &ri);
      fprintf(stderr, "renderer=%s\n", ri.name);
      continue;
    }
    if(!strcmp(cmd, "rec")) {
      char pv[1100], pa[1100];
      snprintf(pv, sizeof(pv), "%s.vid", a1); snprintf(pa, sizeof(pa), "%s.pcm", a1);
      if(recVidF) { fclose(recVidF); fclose(recPcmF); }
      recVidF = fopen(pv, "wb"); recPcmF = fopen(pa, "wb"); recFracF = 0;
      if(!recVidF || !recPcmF) { fprintf(stderr, "rec: cannot write %s\n", a1); if(recVidF) fclose(recVidF); if(recPcmF) fclose(recPcmF); recVidF = recPcmF = NULL; }
      continue;
    }
    if(!strcmp(cmd, "recstop")) { if(recVidF) { fclose(recVidF); fclose(recPcmF); } recVidF = recPcmF = NULL; continue; }
    if(!strcmp(cmd, "winsize")) {
      cfg.displayMode = DISPLAY_WINDOWED; app_applyVideo();
      SDL_SetWindowSize(app.win, atoi(a1), atoi(a2));
      app_applyGameSettings();
      scriptWait = 30;
      return;
    }
    if(!strcmp(cmd, "quit")) { app.running = false; return; }
  }
  fclose(scriptFile);
  scriptFile = NULL;
}

// ------------------------------------------------------------------ main

#ifdef _WIN32
static void writeModuleOf(FILE* f, void* addr) {
  HMODULE m = NULL;
  char name[MAX_PATH] = "?";
  if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m)) {
    GetModuleFileNameA(m, name, MAX_PATH);
  }
  const char* base = strrchr(name, '\\');
  fprintf(f, "%p  %s+%llX\n", addr, base ? base + 1 : name, (unsigned long long)((uintptr_t)addr - (uintptr_t)m));
}

// writes crash.txt (module + offset per frame, resolvable with the PDBs) for bug reports
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
  char path[1200];
  snprintf(path, sizeof(path), "%scrash.txt", app.dataDir);
  FILE* f = fopen(path, "w");
  if(f) {
    fprintf(f, "exception %08lX\n", ep->ExceptionRecord->ExceptionCode);
    writeModuleOf(f, ep->ExceptionRecord->ExceptionAddress);
    void* frames[32];
    USHORT n = CaptureStackBackTrace(0, 32, frames, NULL);
    for(USHORT i = 0; i < n; i++) writeModuleOf(f, frames[i]);
    fclose(f);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  SetUnhandledExceptionFilter(crashHandler);
  SetProcessDPIAware();
#endif
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#ifdef __ANDROID__
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   // (Back opens the menu)
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_TITLE, SDL_GetError(), NULL);
    return 1;
  }
#ifdef __ANDROID__
  // settings, saves and the ROM live in the app's own storage
  const char* internal = SDL_AndroidGetInternalStoragePath();
  snprintf(app.baseDir, sizeof(app.baseDir), "%s/", internal ? internal : ".");
#else
  char* base = SDL_GetBasePath();
  snprintf(app.baseDir, sizeof(app.baseDir), "%s", base ? base : ".\\");
  SDL_free(base);
#endif
  if(dirWritable(app.baseDir)) {
    snprintf(app.dataDir, sizeof(app.dataDir), "%s", app.baseDir);
  } else {
    char* pref = SDL_GetPrefPath("BusterBustsLoosePC", "BusterBustsLoose");
    snprintf(app.dataDir, sizeof(app.dataDir), "%s", pref ? pref : app.baseDir);
    SDL_free(pref);
  }
  snprintf(app.cfgPath, sizeof(app.cfgPath), "%sbbl.ini", app.dataDir);
  config_load(app.cfgPath);
  {
    char saves[1100];
    snprintf(saves, sizeof(saves), "%ssaves", app.dataDir);
    makeDir(saves);
  }

  for(int i = 1; i < argc - 1; i++) {
    if(!strcmp(argv[i], "--script")) scriptFile = fopen(argv[i + 1], "r");
  }
  app.game = findRom(argc, argv);
  if(!app.game) { SDL_Quit(); return 1; }
  cheats_init(app.game);
  hud_init(app.game);
  bg_start(app.romPath);

  int pw, ph;
  pictureSize(&pw, &ph);
  int startDisp = targetDisplay();
#ifdef __ANDROID__
  app.win = SDL_CreateWindow(APP_TITLE, 0, 0, 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
#else
  app.win = SDL_CreateWindow(APP_TITLE, SDL_WINDOWPOS_CENTERED_DISPLAY(startDisp), SDL_WINDOWPOS_CENTERED_DISPLAY(startDisp),
    pw * cfg.windowScale, ph * cfg.windowScale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
#endif
  if(!app.win) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_TITLE, SDL_GetError(), NULL);
    return 1;
  }
  SDL_SetWindowMinimumSize(app.win, 256, 224);
  // Direct3D 11 copes with fullscreen switches and alt-tab; SDL's default
  // Direct3D 9 renderer crashes when an exclusive fullscreen window is restored
  Uint32 renFlags = SDL_RENDERER_ACCELERATED | (cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
#ifdef _WIN32
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif
  app.ren = SDL_CreateRenderer(app.win, -1, renFlags);
  if(!app.ren) {
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
    app.ren = SDL_CreateRenderer(app.win, -1, renFlags);
  }
  if(!app.ren) app.ren = SDL_CreateRenderer(app.win, -1, 0);
  ui_init(app.ren);
  in_init();
  if(!audio_init()) app_toast("No audio device");
  app_applyVideo();
  app_applyGameSettings();
  SDL_ShowWindow(app.win);

  app.running = true;
  app.lastCounter = SDL_GetPerformanceCounter();
  app.fpsTicks = SDL_GetTicks();
#ifdef __ANDROID__
  app_toast("Tap MENU or press Back for the menu");
#else
  app_toast(cfg.selectOpensMenu ? "Esc or Select opens the menu" : "Esc or Select+Start opens the menu");
#endif
  const double frameTime = 1.0 / SNES_FPS;

  while(app.running) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
      switch(e.type) {
        case SDL_QUIT: if(scriptFile) fprintf(stderr, "SDL_QUIT\n"); app.running = false; break;
        case SDL_RENDER_TARGETS_RESET:
        case SDL_RENDER_DEVICE_RESET:
          resetTextures();
          break;
        case SDL_WINDOWEVENT:
          if(scriptFile) fprintf(stderr, "winevent %d\n", e.window.event);
          if(e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) app.focusLost = true;
          if(e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) app.focusLost = false;
          if(e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && cfg.widescreen == WS_AUTO) app_applyGameSettings();
          break;
        case SDL_DROPFILE: SDL_free(e.drop.file); break;
        // (Android: the app going to the background; it may be closed there)
        case SDL_APP_WILLENTERBACKGROUND: app_saveConfig(); app.focusLost = true; break;
        case SDL_APP_DIDENTERFOREGROUND: app.focusLost = false; app.lastCounter = SDL_GetPerformanceCounter(); break;
        default: break;
      }
      in_handleEvent(&e);
    }
    in_setMenuActive(menu_isOpen());
    in_update();
    handleActions();
    if(menu_isOpen()) {
      // (touch: tapping a row, dragging the list)
      int tx, ty;
      if(touch_takeTap(&tx, &ty)) menu_tap(tx, ty);
      for(int sc = touch_takeScroll(menu_rowHeight()); sc != 0; sc += sc > 0 ? -1 : 1) in_injectNav(sc > 0 ? NAV_DOWN : NAV_UP);
      menu_update();
    } else {
      in_clearNav();
    }

    // timing: run as many emulated frames as real time asks for
    uint64_t now = SDL_GetPerformanceCounter();
    double dt = (double)(now - app.lastCounter) / SDL_GetPerformanceFrequency();
    app.lastCounter = now;
    if(dt > 0.25) dt = 0.25;
    app.frameAcc += dt;
    int frames = 0;
    bool locked = false;   // one frame per refresh of a 60 Hz display
    while(app.frameAcc >= frameTime) { app.frameAcc -= frameTime; frames++; }
    // with vsync on a ~60 Hz display, lock to one frame per refresh; the audio
    // resampler absorbs the tiny speed difference
    if(cfg.vsync) {
      // the refresh rate the window is shown at: the display's current mode (a
      // window's own display mode is only what exclusive fullscreen would use,
      // and a 144 Hz monitor locked to one frame per refresh ran at ~47 FPS)
      SDL_DisplayMode dm;
      int refresh = 0;
      bool exclusive = (SDL_GetWindowFlags(app.win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN;
      int display = SDL_GetWindowDisplayIndex(app.win);
      if(exclusive ? SDL_GetWindowDisplayMode(app.win, &dm) == 0 : (display >= 0 && SDL_GetCurrentDisplayMode(display, &dm) == 0))
        refresh = dm.refresh_rate;
      if(refresh >= 59 && refresh <= 61) {
        // one frame per refresh; vsync timing jitters around 16.7 ms, so a
        // time budget would skip a frame after a short wait and drop the one a
        // long wait owes. Catch up only after a refresh was really missed.
        frames = dt > frameTime * 1.5 ? (int)(dt / frameTime + 0.5) : 1;
        app.frameAcc = 0;
        locked = true;
      }
    }
    if(frames > 4) frames = 4;
    if(recVidF) frames = 1;   // recording: one game frame per picture

    bool paused = menu_isOpen() || (app.focusLost && cfg.pauseOnFocusLoss);
    if(!paused) {
      bool ff = cfg.fastForward && in_actionHeld(ACT_FAST_FORWARD) && !recVidF;
      if(levelSelectHidden()) { ff = false; frames = 12; }
      if(ff) frames = frames > 0 ? 6 : 0;
      for(int i = 0; i < frames; i++) stepGame(i == frames - 1);
      app.fpsFrames += frames;
    } else {
      scriptTicks++;
    }
    if(paused && menu_musicActive()) {
      for(int i = 0; i < frames; i++) {
        int n = recPcmF ? recSamples() : audio_samplesForFrame();
        music_frame(app.audioBuf, n);
        audio_push(app.audioBuf, n);
        if(recPcmF) fwrite(app.audioBuf, 4, n, recPcmF);
      }
    } else if(paused && recPcmF) {
      int n = recSamples();
      memset(app.audioBuf, 0, (size_t)n * 4);
      fwrite(app.audioBuf, 4, n, recPcmF);
    }
    uint32_t t = SDL_GetTicks();
    app.fpsLoops++;
    if(t - app.fpsTicks >= 1000) {
      app.fps = app.fpsFrames * 1000.0 / (t - app.fpsTicks);
      app.loopsPerSec = app.fpsLoops * 1000.0 / (t - app.fpsTicks);
      app.fpsFrames = 0;
      app.fpsLoops = 0;
      app.fpsTicks = t;
    }
    // Present when there is a new frame (or the refresh is what paces the
    // loop). On a faster display (100/144 Hz) or without vsync that keeps
    // presents at 60 a second: no spinning, and variable refresh (G-Sync /
    // FreeSync) displays can show every frame for the same time.
    bool present = locked || frames > 0;
    if(SDL_GetWindowFlags(app.win) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(15); // nothing to draw; don't spin
    } else if(present) {
      render();
    }
    scriptStep();
    if(!present || !cfg.vsync) SDL_Delay(1);
  }

  app_saveConfig();
  music_close();
  bg_shutdown();
  audio_shutdown();
  in_shutdown();
  ui_shutdown();
  bbl_destroy(app.game);
  SDL_DestroyRenderer(app.ren);
  SDL_DestroyWindow(app.win);
  SDL_Quit();
  return 0;
}
