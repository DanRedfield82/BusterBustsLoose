// In-game options menu (immediate mode). Each page is a function that lists
// its widgets every time it runs; the widget at the cursor reacts to the
// navigation event that is being processed.
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "menu.h"
#include "version.h"
#include "app.h"
#include "config.h"
#include "finput.h"
#include "ui.h"
#include "cheats.h"
#include "filters.h"
#include "upscale.h"
#include "levels.h"
#include "music.h"
#include "audio.h"

typedef void (*PageFn)(void);

enum { ROW_BUTTON, ROW_VALUE, ROW_HEADER, ROW_TEXT };

typedef struct Row {
  int type;
  bool selectable;
  char label[96];
  char value[64];
  const char* help;
  UiColor color;
  int column;        // highlighted value column for multi-value rows (-1 none)
  char value2[64];
  int bar;           // slider fill in percent, drawn as a bar before the value (-1 none)
} Row;

#define MAX_ROWS 128
#define MAX_DEPTH 8

static bool open;
static PageFn stack[MAX_DEPTH];
static int cursor[MAX_DEPTH];
static int scrollTop[MAX_DEPTH];
static int depth;
static const char* pageTitle;

static Row rows[MAX_ROWS];
static int rowCount, selCount;
static int curNav;
static bool navUsed;
static PageFn pushReq;
static bool popReq;
static void (*extraDraw)(int x, int y, int w, int h, int scale);
static int extraHeight; // rows reserved for extraDraw at the top of the list

// binding capture
static int captureTarget = -1;   // index into binding list
static int captureColumn;
static bool captureIsPad;
static bool captureIsAction;

// music page state
static bool musicPage;
static int musicTab;    // 0 music, 1 sfx
static int sfxId = 1;

// confirm dialog for destructive actions
static const char* confirmText;
static void (*confirmAction)(void);

// where the last drawn rows are, for touch (menu_tap)
typedef struct HitRow { int y0, y1, sel, type; bool twoCols; } HitRow;
static HitRow hits[MAX_ROWS];
static int hitCount, hitX0, hitX1, hitValueX, lastRowH = 12;

static void pageMain(void);

// ---------------------------------------------------------------- widgets

static Row* addRow(int type, bool selectable, const char* label) {
  if(rowCount >= MAX_ROWS) rowCount = MAX_ROWS - 1;
  Row* r = &rows[rowCount++];
  memset(r, 0, sizeof(*r));
  r->type = type;
  r->selectable = selectable;
  r->color = UI_WHITE;
  r->column = -1;
  r->bar = -1;
  snprintf(r->label, sizeof(r->label), "%s", label);
  return r;
}

static bool isActive(void) {
  return selCount == cursor[depth];
}

static void title(const char* t) { pageTitle = t; }

static void header(const char* text) {
  Row* r = addRow(ROW_HEADER, false, text);
  r->color = UI_YELLOW;
}

static void text(const char* s, UiColor c) {
  Row* r = addRow(ROW_TEXT, false, s);
  r->color = c;
}

static bool button(const char* label, const char* help) {
  Row* r = addRow(ROW_BUTTON, true, label);
  r->help = help;
  bool hit = false;
  if(isActive() && !navUsed && curNav == NAV_OK) { hit = true; navUsed = true; }
  selCount++;
  return hit;
}

static bool buttonValue(const char* label, const char* value, const char* help) {
  Row* r = addRow(ROW_BUTTON, true, label);
  r->help = help;
  snprintf(r->value, sizeof(r->value), "%s", value);
  bool hit = false;
  if(isActive() && !navUsed && curNav == NAV_OK) { hit = true; navUsed = true; }
  selCount++;
  return hit;
}

static void submenu(const char* label, PageFn page, const char* help) {
  char buf[100];
  snprintf(buf, sizeof(buf), "%s", label);
  Row* r = addRow(ROW_BUTTON, true, buf);
  r->help = help;
  snprintf(r->value, sizeof(r->value), ">");
  if(isActive() && !navUsed && (curNav == NAV_OK || curNav == NAV_RIGHT)) { pushReq = page; navUsed = true; }
  selCount++;
}

static bool choice(const char* label, int* v, const char* const* names, int n, const char* help) {
  Row* r = addRow(ROW_VALUE, true, label);
  r->help = help;
  if(*v < 0 || *v >= n) *v = 0;
  bool changed = false;
  if(isActive() && !navUsed) {
    if(curNav == NAV_LEFT) { *v = (*v + n - 1) % n; changed = true; navUsed = true; }
    else if(curNav == NAV_RIGHT || curNav == NAV_OK) { *v = (*v + 1) % n; changed = true; navUsed = true; }
  }
  snprintf(r->value, sizeof(r->value), "< %s >", names[*v]);
  selCount++;
  return changed;
}

static bool toggle(const char* label, int* v, const char* help) {
  static const char* const names[] = { "Off", "On" };
  int t = *v ? 1 : 0;
  bool c = choice(label, &t, names, 2, help);
  *v = t;
  if(c) rows[rowCount - 1].color = UI_WHITE;
  snprintf(rows[rowCount - 1].value, sizeof(rows[0].value), "%s", t ? "ON" : "OFF");
  rows[rowCount - 1].color = UI_WHITE;
  return c;
}

static bool slider(const char* label, int* v, int lo, int hi, int step, const char* fmt, const char* help) {
  Row* r = addRow(ROW_VALUE, true, label);
  r->help = help;
  bool changed = false;
  if(isActive() && !navUsed) {
    if(curNav == NAV_LEFT && *v > lo) { *v -= step; if(*v < lo) *v = lo; changed = true; navUsed = true; }
    else if(curNav == NAV_RIGHT && *v < hi) { *v += step; if(*v > hi) *v = hi; changed = true; navUsed = true; }
    else if(curNav == NAV_LEFT || curNav == NAV_RIGHT) navUsed = true;
  }
  snprintf(r->value, sizeof(r->value), fmt, *v);
  r->bar = (hi > lo) ? (*v - lo) * 100 / (hi - lo) : 0;
  selCount++;
  return changed;
}

static void confirm(const char* textMsg, void (*action)(void)) {
  confirmText = textMsg;
  confirmAction = action;
}

// ---------------------------------------------------------------- pages

static void pageVideo(void) {
  static const char* const displayNames[] = { "Windowed", "Borderless", "Fullscreen" };
  static const char* const wsNames[] = { "Off (4:3)", "16:10", "16:9", "21:9", "Fit window" };
  static const char* const aspectNames[] = { "4:3 (CRT)", "Square pixels", "Stretch" };
  static const char* const scaleNames[] = { "Sharp", "Smooth", "Sharp bilinear", "Integer" };
  static const char* const scanNames[] = { "Off", "Light", "Medium", "Heavy" };
  static const char* const limitNames[] = { "Original", "Unlimited" };
  title("VIDEO");
  header("Display");
#ifdef __ANDROID__
  // (a phone shows the game on the whole screen: no window, monitor or size)
  (void)displayNames;
#else
  if(choice("Display mode", &cfg.displayMode, displayNames, DISPLAY_COUNT, "Windowed, borderless fullscreen or exclusive fullscreen (F11)")) app_applyVideo();
  {
    static char monNames[8][96];
    static const char* monPtrs[8];
    int n = app_displayCount();
    if(n < 1) n = 1;
    if(n > 8) n = 8;
    for(int i = 0; i < n; i++) { app_displayName(i, monNames[i], sizeof(monNames[i])); monPtrs[i] = monNames[i]; }
    if(cfg.monitor >= n) cfg.monitor = 0;
    if(choice("Monitor", &cfg.monitor, monPtrs, n, "Which display the game uses")) app_applyVideo();
  }
  if(slider("Window size", &cfg.windowScale, 1, 8, 1, "%dx", "Window size as a multiple of the SNES resolution")) app_applyVideo();
#endif
  if(toggle("V-Sync", &cfg.vsync, "Sync to the monitor refresh; turn off if you see stutter on high refresh displays")) app_applyVideo();
  header("Widescreen");
  if(choice("Widescreen", &cfg.widescreen, wsNames, WS_COUNT, "Show more of the level to the sides during gameplay (F10)")) app_applyGameSettings();
  if(toggle("Re-anchor HUD", &cfg.hudReanchor, "Move the life/dash and lives/stars displays to the screen edges in widescreen")) app_applyGameSettings();
  header("Picture");
  if(choice("Aspect ratio", &cfg.aspect, aspectNames, ASPECT_COUNT, "4:3 matches a TV of the era; square pixels are sharper but narrower")) app_applyGameSettings();
  if(choice("Upscale filter", &cfg.upscaler, upscaleNames, UPSCALE_COUNT, "Smooth pixel-art upscaling: Scale2x/3x/4x keep it crisp, xBR rounds off edges")) {}
  choice("Scaling", &cfg.scaling, scaleNames, SCALE_COUNT, "Sharp: nearest. Smooth: bilinear. Sharp bilinear: crisp without uneven pixels");
  choice("Scanlines", &cfg.scanlines, scanNames, 4, "Darkened lines like a CRT television");
  toggle("CRT mask", &cfg.crtMask, "Aperture grille pattern (looks best at 1440p and above)");
  if(choice("Sprite limit", &cfg.spriteLimit, limitNames, 2, "Unlimited removes sprite flicker and dropout, useful in widescreen")) app_applyGameSettings();
  toggle("Show FPS", &cfg.showFps, "Show the frame rate in the corner");
}

static void pageColours(void) {
  title("COLOURS");
  header("Colour profile");
  bool ch = false;
  ch |= choice("Profile", &cfg.colourMode, colourModeNames, COLOUR_COUNT,
    "Vivid boosts colour for modern panels; Natural/Warm mimic a CRT television");
  header("Fine tuning");
  ch |= slider("Brightness", &cfg.brightness, 50, 150, 5, "%d%%", "Overall brightness");
  ch |= slider("Contrast", &cfg.contrast, 50, 150, 5, "%d%%", "Difference between dark and bright areas");
  ch |= slider("Saturation", &cfg.saturation, 0, 200, 5, "%d%%", "Colour intensity (0 = greyscale)");
  ch |= slider("Gamma", &cfg.gamma, 50, 150, 5, "%d%%", "Brightness of mid tones");
  if(button("Reset colours", "Back to the original colours")) {
    cfg.colourMode = 0; cfg.brightness = cfg.contrast = cfg.saturation = cfg.gamma = 100;
    ch = true;
  }
  if(ch) app_applyGameSettings();
}

static void pageAudio(void) {
  title("AUDIO");
  slider("Volume", &cfg.volume, 0, 100, 5, "%d%%", "Master volume");
  static const char* const outNames[] = { "Stereo", "Mono" };
  choice("Sound output", &cfg.monoAudio, outNames, 2, "Stereo or mono sound");
  toggle("Mute in menu", &cfg.muteWhenPaused, "Silence the game while this menu is open");
}

static void pageCheats(void) {
  title("CHEATS");
  Bbl* g = app_game();
  for(int i = 0; i < CHEAT_COUNT; i++) {
    if(toggle(cheatInfo[i].name, &cfg.cheats[i], cheatInfo[i].help)) cheats_set(g, i, cfg.cheats[i]);
  }
  if(button("Disable all cheats", "Turn every cheat off")) {
    for(int i = 0; i < CHEAT_COUNT; i++) { cfg.cheats[i] = 0; cheats_set(g, i, 0); }
  }
}

static void pageLevels(void) {
  title("LEVEL SELECT");
  if(app_levelSelectFailed()) {
    text("The level select could not be prepared.", UI_RED);
    return;
  }
  if(!app_levelSelectReady()) {
    text("Preparing level select, one moment...", UI_GREY);
    return;
  }
  static const char* const diffNames[] = { "Children", "Normal", "Challenge" };
  choice("Difficulty", &cfg.levelDifficulty, diffNames, 3, "Difficulty used when starting from the level select");
  toggle("Skip stage intro", &cfg.skipIntro, "Go straight into the action instead of playing the stage's story intro");
  for(int s = 0; s < levelStageCount; s++) {
    header(levelStages[s].name);
    for(int a = 0; a < levelStages[s].areaCount; a++) {
      int idx = levelStages[s].firstArea + a;
      const LevelArea* la = &levelAreas[idx];
      char label[96];
      if(la->kind == LEVEL_AREA) snprintf(label, sizeof(label), "  %d-%d  %s", la->stage + 1, la->area + 1, la->name);
      else snprintf(label, sizeof(label), "  %s", la->name);
      const char* help = la->kind == LEVEL_BONUS ? "Play this bonus game (your current progress is not saved)"
                                                 : "Start here (your current progress is not saved)";
      if(button(label, help)) {
        app_startLevel(idx);
        app_closeMenu();
      }
    }
  }
}

static const char* const snesButtonNames[BTN_COUNT] = {
  "B  (jump)", "Y  (drop kick)", "Select", "Start  (pause)", "Up", "Down", "Left", "Right",
  "A", "X  (drop kick)", "L  (dash)", "R  (dash)"
};
static const char* const actionNames[ACT_COUNT] = {
  "Open menu", "Fast forward", "Quick save", "Quick load", "Screenshot", "Toggle fullscreen", "Toggle widescreen"
};

static void startCapture(int target, int column, bool pad, bool action) {
  captureTarget = target;
  captureColumn = column;
  captureIsPad = pad;
  captureIsAction = action;
  in_beginCapture(pad ? CAPTURE_PAD : CAPTURE_KEY);
}

static void pageKeyboard(void) {
  static int column;
  title("KEYBOARD");
  text("Left/Right: column   Confirm: rebind   Del: clear", UI_GREY);
  header("SNES buttons");
  for(int b = 0; b < BTN_COUNT; b++) {
    Row* r = addRow(ROW_VALUE, true, (b == BTN_SELECT && cfg.selectOpensMenu) ? "Select  (menu)" : snesButtonNames[b]);
    r->help = "Press confirm, then the new key (Esc cancels)";
    snprintf(r->value, sizeof(r->value), "%s", in_keyName(cfg.keyBind[b][0]));
    snprintf(r->value2, sizeof(r->value2), "%s", in_keyName(cfg.keyBind[b][1]));
    if(isActive()) {
      r->column = column;
      if(!navUsed) {
        if(curNav == NAV_LEFT) { column = 0; navUsed = true; }
        else if(curNav == NAV_RIGHT) { column = 1; navUsed = true; }
        else if(curNav == NAV_OK) { startCapture(b, column, false, false); navUsed = true; }
        else if(curNav == NAV_EXTRA) { cfg.keyBind[b][column] = 0; navUsed = true; }
      }
    }
    selCount++;
  }
  header("Shortcuts");
  for(int a = 0; a < ACT_COUNT; a++) {
    char val[64];
    snprintf(val, sizeof(val), "%s", in_keyName(cfg.keyAction[a]));
    Row* r = addRow(ROW_VALUE, true, actionNames[a]);
    r->help = "Press confirm, then the new key (Esc cancels)";
    snprintf(r->value, sizeof(r->value), "%s", val);
    if(isActive() && !navUsed) {
      if(curNav == NAV_OK) { startCapture(a, 0, false, true); navUsed = true; }
      else if(curNav == NAV_EXTRA) { cfg.keyAction[a] = 0; navUsed = true; }
    }
    selCount++;
  }
  if(button("Reset keyboard to defaults", "Restore the default keys")) config_defaultKeys(&cfg);
}

static void pageGamepad(void) {
  title("GAMEPAD");
  const char* name = in_padName();
  char buf[128];
  snprintf(buf, sizeof(buf), "Controller: %s", name ? name : "none connected");
  text(buf, name ? UI_GREEN : UI_GREY);
  header("SNES buttons");
  for(int b = 0; b < BTN_COUNT; b++) {
    Row* r = addRow(ROW_VALUE, true, (b == BTN_SELECT && cfg.selectOpensMenu) ? "Select  (menu)" : snesButtonNames[b]);
    r->help = "Press confirm, then the new button (Esc on keyboard cancels, Y clears)";
    snprintf(r->value, sizeof(r->value), "%s", in_padButtonName(cfg.padBind[b]));
    if(isActive() && !navUsed) {
      if(curNav == NAV_OK) { startCapture(b, 0, true, false); navUsed = true; }
      else if(curNav == NAV_EXTRA) { cfg.padBind[b] = PAD_UNBOUND; navUsed = true; }
    }
    selCount++;
  }
  toggle("Left stick moves", &cfg.analogStick, "Use the left analog stick as a d-pad");
  header("Shortcuts");
  for(int a = 0; a < ACT_COUNT; a++) {
    Row* r = addRow(ROW_VALUE, true, actionNames[a]);
    r->help = a != ACT_MENU ? "Press confirm, then the new button" :
      (cfg.selectOpensMenu ? "The Select button also opens the menu" : "Select + Start together also opens the menu");
    snprintf(r->value, sizeof(r->value), "%s", in_padButtonName(cfg.padAction[a]));
    if(isActive() && !navUsed) {
      if(curNav == NAV_OK) { startCapture(a, 0, true, true); navUsed = true; }
      else if(curNav == NAV_EXTRA) { cfg.padAction[a] = PAD_UNBOUND; navUsed = true; }
    }
    selCount++;
  }
  if(button("Reset gamepad to defaults", "Restore the default buttons")) config_defaultPad(&cfg);
}

static void pageControls(void) {
  title("CONTROLS");
  toggle("Select opens menu", &cfg.selectOpensMenu,
    "Use the Select button (Back/View/Share/Minus) as the menu button; the game only uses it as a second Start");
#ifdef __ANDROID__
  toggle("Fast forward", &cfg.fastForward, "Shows a FAST button at the top of the screen; holding it speeds the game up");
#else
  toggle("Fast forward", &cfg.fastForward, "Holding the fast forward button (Tab / right trigger) speeds the game up");
#endif
  static const char* const touchNames[] = { "Off", "Auto", "Always" };
  choice("Touch controls", &cfg.touchControls, touchNames, 3,
    "On-screen buttons for touch screens; Auto shows them once the screen is touched and hides them when a controller is used");
  slider("Touch opacity", &cfg.touchOpacity, 10, 100, 10, "%d%%", "How solid the on-screen buttons look");
  submenu("Keyboard", pageKeyboard, "Change keyboard keys");
  submenu("Gamepad", pageGamepad, "Change controller buttons (XInput, DualShock, Switch Pro and more)");
}

static void musicDraw(int x, int y, int w, int h, int s) {
  (void)h;
  float lv[8];
  music_voices(lv);
  int barW = (w - 16 * s) / 8;
  int maxH = 24 * s;
  for(int i = 0; i < 8; i++) {
    int bh = (int)(lv[i] * maxH);
    int bx = x + 8 * s + i * barW;
    ui_rect(bx + s, y + maxH - bh, barW - 2 * s, bh, (UiColor){ (uint8_t)(90 + i * 20), (uint8_t)(200 - i * 10), 255, 230 });
    ui_rect(bx + s, y + maxH, barW - 2 * s, s, UI_DIM);
  }
  char status[128];
  int cur = music_current();
  if(music_state() == MUSIC_LOADING) snprintf(status, sizeof(status), "Loading %s...", cur >= 0 ? musicTracks[cur].name : "sound samples");
  else if(music_state() == MUSIC_PLAYING && cur >= 0) {
    uint32_t sec = music_playFrames() / 60;
    snprintf(status, sizeof(status), "Now playing: %s  %u:%02u", musicTracks[cur].name, sec / 60, sec % 60);
  } else snprintf(status, sizeof(status), "Stopped");
  ui_textShadow(x + 8 * s, y + maxH + 4 * s, status, UI_CYAN, s);
}

static void pageMusic(void) {
  static const char* const tabs[] = { "Music", "Sound effects" };
  title("MUSIC PLAYER");
  musicPage = true;
  if(!music_isOpen()) {
    if(app_levelSelectFailed()) { text("The sound player could not be started.", UI_RED); return; }
    if(!app_musicOpen()) { text("Starting the sound player...", UI_GREY); return; }
  }
  extraDraw = musicDraw;
  extraHeight = 4;
  choice("Mode", &musicTab, tabs, 2, "Switch between the soundtrack and the sound test");
  if(button("Stop", "Stop playback")) music_stop();
  if(musicTab == 0) {
    header("Soundtrack");
    for(int i = 0; i < musicTrackCount; i++) {
      char label[96];
      snprintf(label, sizeof(label), "%2d. %s", i + 1, musicTracks[i].name);
      bool playing = music_current() == i;
      if(buttonValue(label, playing ? (music_state() == MUSIC_LOADING ? "..." : "PLAYING") : "", "Confirm to play")) music_play(i);
      if(playing) rows[rowCount - 1].color = UI_CYAN;
    }
  } else {
    header("Sound test");
    char label[64];
    snprintf(label, sizeof(label), "Sound effect");
    Row* r = addRow(ROW_VALUE, true, label);
    r->help = "Left/Right chooses an effect, confirm plays it";
    if(isActive() && !navUsed) {
      if(curNav == NAV_LEFT) { sfxId = sfxId > 1 ? sfxId - 1 : 0x8C; navUsed = true; }
      else if(curNav == NAV_RIGHT) { sfxId = sfxId < 0x8C ? sfxId + 1 : 1; navUsed = true; }
      else if(curNav == NAV_OK) { music_sfx(sfxId); navUsed = true; }
    }
    snprintf(r->value, sizeof(r->value), "< %02X >", sfxId);
    selCount++;
    static const struct { const char* name; int id; } jingles[] = {
      { "Jingle A0", 0xA0 }, { "Jingle A1", 0xA1 }, { "Jingle AD", 0xAD }, { "Jingle AF", 0xAF },
    };
    for(int i = 0; i < 4; i++) {
      if(button(jingles[i].name, "Short jingles that use the samples of the last played song")) music_sfx(jingles[i].id);
    }
  }
}

static void pageCredits(void) {
  title("CREDITS");
  text("Find me over at:", UI_WHITE);
  text("", UI_WHITE);
  text("YouTube — https://www.youtube.com/adhgaming", UI_CYAN);
  text("Reddit — u/Darhkwing", UI_CYAN);
  text("GitHub — https://github.com/Darhkfox", UI_CYAN);
}

static void doReset(void) { app_resetGame(); app_closeMenu(); }
static void doQuit(void) { app_quit(); }
static void doSaveState(void) { if(app_saveState(cfg.saveSlot)) app_closeMenu(); }

static void pageMain(void) {
  title("BUSTER BUSTS LOOSE!");
  if(button("Resume", "Back to the game")) app_closeMenu();
  // Save states: one slot at a time (F5 and F9 use it too); overwriting a
  // save asks first
  static char loadHelp[160], saveHelp[160], overwrite[64];
  char info[128];
  bool stateThere = app_stateExists(cfg.saveSlot, info, sizeof(info));
  const char* when = strncmp(info, "Saved ", 6) ? info : info + 6;
  if(stateThere) snprintf(loadHelp, sizeof(loadHelp), "Back to the moment saved in slot %d (F9)", cfg.saveSlot);
  else snprintf(loadHelp, sizeof(loadHelp), "Slot %d is empty", cfg.saveSlot);
  if(buttonValue("Load state", stateThere ? when : "EMPTY", loadHelp)) {
    if(app_loadState(cfg.saveSlot)) app_closeMenu();
  }
  if(!stateThere) rows[rowCount - 1].color = UI_GREY;
  if(stateThere) snprintf(saveHelp, sizeof(saveHelp), "Save the game in slot %d, over the save from %s (F5)", cfg.saveSlot, when);
  else snprintf(saveHelp, sizeof(saveHelp), "Save the game exactly as it is now in slot %d (F5)", cfg.saveSlot);
  if(button("Save state", saveHelp)) {
    if(stateThere) {
      snprintf(overwrite, sizeof(overwrite), "Overwrite slot %d?", cfg.saveSlot);
      confirm(overwrite, doSaveState);
    } else doSaveState();
  }
  slider("Slot", &cfg.saveSlot, 1, 9, 1, "< %d >", "The save slot Load state, Save state, F5 and F9 use (1 to 9)");
  rows[rowCount - 1].bar = -1;   // (slots are numbers, not an amount)
  submenu("Level select", pageLevels, "Jump to any stage and area");
  submenu("Cheats", pageCheats, "Infinite lives, invincibility and more");
  submenu("Video", pageVideo, "Widescreen, fullscreen, upscaling and CRT effects");
  submenu("Colours", pageColours, "Colour profiles for modern displays");
  submenu("Audio", pageAudio, "Volume and sound output");
  submenu("Music player", pageMusic, "Listen to the soundtrack and try the sound test");
  submenu("Controls", pageControls, "Keyboard and gamepad buttons");
  if(button("Reset game", "Restart from the power-on screens")) confirm("Reset the game?", doReset);
  submenu("Credits", pageCredits, "Where to find me");
  if(button("Quit", "Close the game")) confirm("Quit Buster Busts Loose?", doQuit);
}

// ---------------------------------------------------------------- driver

void menu_open(void) {
  open = true;
  depth = 0;
  stack[0] = pageMain;
  cursor[0] = 0;
  scrollTop[0] = 0;
  confirmText = NULL;
  in_clearNav();
}

void menu_close(void) {
  if(musicPage) app_musicClose();
  musicPage = false;
  open = false;
  captureTarget = -1;
  app_saveConfig();
}

bool menu_isOpen(void) { return open; }
bool menu_musicActive(void) { return open && musicPage; }

static void runPage(int nav) {
  rowCount = 0;
  selCount = 0;
  curNav = nav;
  navUsed = false;
  pushReq = NULL;
  popReq = false;
  extraDraw = NULL;
  extraHeight = 0;
  pageTitle = "";
  bool wasMusic = musicPage;
  musicPage = false;
  stack[depth]();
  if(wasMusic && !musicPage) app_musicClose();
  if(!navUsed) {
    switch(nav) {
      case NAV_UP: if(selCount) cursor[depth] = (cursor[depth] + selCount - 1) % selCount; break;
      case NAV_DOWN: if(selCount) cursor[depth] = (cursor[depth] + 1) % selCount; break;
      case NAV_PAGE_UP: cursor[depth] -= 8; if(cursor[depth] < 0) cursor[depth] = 0; break;
      case NAV_PAGE_DOWN: cursor[depth] += 8; if(cursor[depth] >= selCount) cursor[depth] = selCount - 1; break;
      case NAV_BACK: case NAV_LEFT:
        if(nav == NAV_LEFT && depth == 0) break;
        if(depth > 0) popReq = true;
        else if(nav == NAV_BACK) { menu_close(); return; }
        break;
      default: break;
    }
  }
  if(cursor[depth] >= selCount) cursor[depth] = selCount > 0 ? selCount - 1 : 0;
  if(cursor[depth] < 0) cursor[depth] = 0;
  if(pushReq && depth < MAX_DEPTH - 1) {
    depth++;
    stack[depth] = pushReq;
    cursor[depth] = 0;
    scrollTop[depth] = 0;
  } else if(popReq) {
    depth--;
  }
}

void menu_tap(int x, int y) {
  if(!open || captureTarget >= 0 || confirmText) return;   // (a dialog takes OK / BACK)
  if(x < hitX0 || x >= hitX1) return;
  for(int i = 0; i < hitCount; i++) {
    const HitRow* h = &hits[i];
    if(y < h->y0 || y >= h->y1) continue;
    cursor[depth] = h->sel;
    if(h->twoCols) return;   // (key and button bindings are set with a keyboard or pad)
    int nav = NAV_OK;
    if(h->type == ROW_VALUE) nav = x >= hitValueX && x < hitValueX + (hitX1 - hitValueX) / 2 ? NAV_LEFT : NAV_RIGHT;
    in_injectNav(nav);
    return;
  }
}

int menu_rowHeight(void) { return lastRowH; }

void menu_update(void) {
  if(!open) return;
  if(captureTarget >= 0) {
    int code;
    if(in_captureDone(&code)) {
      if(code >= 0) {
        if(captureIsPad) {
          if(captureIsAction) cfg.padAction[captureTarget] = code;
          else cfg.padBind[captureTarget] = code;
        } else {
          if(captureIsAction) cfg.keyAction[captureTarget] = code;
          else cfg.keyBind[captureTarget][captureColumn] = code;
        }
      }
      captureTarget = -1;
      in_clearNav();
    }
    return;
  }
  if(confirmText) {
    int n;
    while((n = in_nav()) != NAV_NONE) {
      if(n == NAV_OK) { void (*a)(void) = confirmAction; confirmText = NULL; a(); return; }
      if(n == NAV_BACK) { confirmText = NULL; return; }
    }
    return;
  }
  int n;
  int guard = 0;
  while(open && (n = in_nav()) != NAV_NONE && guard++ < 16) runPage(n);
  if(open) runPage(NAV_NONE);
}

// colours (the Buster's Hidden Treasure PC menu's)
#define C_PANEL    ((UiColor){ 14, 26, 48, 232 })
#define C_BORDER   ((UiColor){ 76, 134, 200, 255 })
#define C_LINE     ((UiColor){ 76, 134, 200, 110 })
#define C_BAR      ((UiColor){ 38, 76, 130, 230 })
#define C_TITLE    ((UiColor){ 255, 208, 64, 255 })
#define C_DIM      ((UiColor){ 110, 130, 166, 255 })
#define C_LABEL    ((UiColor){ 159, 178, 214, 255 })
#define C_VALUE    ((UiColor){ 214, 226, 245, 255 })
#define C_ON       ((UiColor){ 124, 232, 124, 255 })
#define C_LINK     ((UiColor){ 110, 200, 255, 255 })
#define C_HELP     ((UiColor){ 140, 158, 190, 255 })

static bool sameColour(UiColor a, UiColor b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// a row's own colour in the menu's palette
static UiColor rowColour(UiColor c, bool active) {
  if(active) return UI_WHITE;
  if(sameColour(c, UI_WHITE)) return C_LABEL;
  if(sameColour(c, UI_CYAN)) return C_LINK;
  if(sameColour(c, UI_GREEN)) return C_ON;
  if(sameColour(c, UI_GREY) || sameColour(c, UI_DIM)) return C_DIM;
  if(sameColour(c, UI_YELLOW)) return C_TITLE;
  return c;
}

void menu_draw(int winW, int winH) {
  if(!open) return;
#ifdef __ANDROID__
  int s = winH / 200;   // (bigger rows for fingers)
#else
  int s = winH / 270;
#endif
  if(winW / 420 < s) s = winW / 420;
  if(s < 1) s = 1;
  int ch = 8 * s;
  int rowH = 11 * s;
  int panelW = 60 * ch;
  if(panelW > winW - 2 * ch) panelW = winW - 2 * ch;
  int panelH = winH - 4 * ch;
  if(panelH > 30 * rowH + 8 * ch) panelH = 30 * rowH + 8 * ch;
  int px = (winW - panelW) / 2, py = (winH - panelH) / 2;
  int bt = s > 1 ? s * 3 / 4 : 1;   // border thickness

  ui_rect(0, 0, winW, winH, (UiColor){ 0, 0, 0, 110 });
  ui_frame(px, py, panelW, panelH, C_PANEL, C_BORDER, bt);
  ui_text(px + (panelW - ui_textWidth(pageTitle, s)) / 2, py + ch + s, pageTitle, C_TITLE, s);
  if(depth == 0) ui_text(px + panelW - 2 * ch - ui_textWidth(BBL_VERSION, s), py + ch + s, BBL_VERSION, C_DIM, s);
  ui_rect(px + 2 * ch, py + 3 * ch, panelW - 4 * ch, bt, C_LINE);

  int listTop = py + 4 * ch + extraHeight * rowH;
  int listBottom = py + panelH - 5 * ch;
  if(extraDraw) extraDraw(px + ch, py + 4 * ch, panelW - 2 * ch, extraHeight * rowH, s);
  int visible = (listBottom - listTop) / rowH;
  if(visible < 1) visible = 1;

  // find the row of the cursor for scrolling
  int sel = 0, cursorRow = 0;
  for(int i = 0; i < rowCount; i++) {
    if(rows[i].selectable) {
      if(sel == cursor[depth]) { cursorRow = i; break; }
      sel++;
    }
  }
  int top = scrollTop[depth];
  if(cursorRow < top + 1) top = cursorRow - 1;
  if(cursorRow >= top + visible - 1) top = cursorRow - visible + 2;
  if(top > rowCount - visible) top = rowCount - visible;
  if(top < 0) top = 0;
  scrollTop[depth] = top;

  const char* help = NULL;
  sel = 0;
  hitCount = 0;
  hitX0 = px + ch;
  hitX1 = px + panelW - ch;
  hitValueX = px + panelW / 2;
  lastRowH = rowH;
  for(int i = 0; i < rowCount; i++) {
    Row* r = &rows[i];
    int mySel = r->selectable ? sel++ : -1;
    if(i < top || i >= top + visible) continue;
    int y = listTop + (i - top) * rowH;
    bool active = r->selectable && mySel == cursor[depth];
    if(r->selectable && hitCount < MAX_ROWS) hits[hitCount++] = (HitRow){ y - s * 2, y - s * 2 + rowH, mySel, r->type, r->value2[0] != 0 };
    if(active) {
      ui_rect(px + ch, y - s * 2, panelW - 2 * ch, rowH, C_BAR);
      ui_rect(px + ch, y - s * 2, bt * 2, rowH, C_BORDER);   // accent at the left of the bar
      help = r->help;
    }
    if(r->type == ROW_HEADER) {
      int lw = ui_textWidth(r->label, s);
      ui_text(px + 2 * ch, y, r->label, C_TITLE, s);
      ui_rect(px + 2 * ch + lw + ch, y + 7 * s / 2, panelW - 5 * ch - lw, bt, C_LINE);
      continue;
    }
    ui_text(px + (r->type == ROW_TEXT ? 2 : 3) * ch, y, r->label, rowColour(r->color, active), s);
    UiColor vcol = active ? UI_WHITE : C_VALUE;
    if(r->value2[0]) {
      int colW = 14 * ch;
      int x2 = px + panelW - 2 * ch - colW;
      int x1 = x2 - colW - ch;
      bool capturingHere = captureTarget >= 0 && active;
      const char* v1 = capturingHere && captureColumn == 0 ? "press a key" : r->value;
      const char* v2 = capturingHere && captureColumn == 1 ? "press a key" : r->value2;
      if(active && r->column == 0) ui_rect(x1 - s * 2, y - s * 2, colW, rowH, (UiColor){ 76, 134, 200, 150 });
      if(active && r->column == 1) ui_rect(x2 - s * 2, y - s * 2, colW, rowH, (UiColor){ 76, 134, 200, 150 });
      ui_text(x1, y, v1, active && r->column == 0 ? UI_WHITE : C_VALUE, s);
      ui_text(x2, y, v2, active && r->column == 1 ? UI_WHITE : C_VALUE, s);
    } else if(r->value[0]) {
      const char* v = r->value;
      if(captureTarget >= 0 && active) v = captureIsPad ? "press a button" : "press a key";
      int vw = ui_textWidth(v, s);
      int vx = px + panelW - 2 * ch - vw;
      if(!strcmp(v, "ON")) vcol = C_ON;
      else if(!strcmp(v, "OFF")) vcol = active ? C_VALUE : C_DIM;
      else if(!strcmp(v, ">")) vcol = active ? UI_WHITE : C_BORDER;
      ui_text(vx, y, v, vcol, s);
      if(r->bar >= 0) {
        // slider: a track with the filled part before the number
        int bw = 12 * ch, bh = 2 * s > 2 ? 2 * s : 2;
        int bx = px + panelW - 2 * ch - 6 * ch - bw, by = y + 7 * s / 2 - bh / 2;
        ui_rect(bx, by, bw, bh, (UiColor){ 30, 48, 82, 255 });
        ui_rect(bx, by, bw * r->bar / 100, bh, active ? (UiColor){ 120, 180, 245, 255 } : C_BORDER);
        ui_rect(bx + bw * r->bar / 100 - s, by - s * 2, s * 2 > 2 ? s * 2 : 2, bh + s * 4, active ? UI_WHITE : C_VALUE);
      }
    }
  }
  // scroll indicators
  if(top > 0) ui_text(px + panelW - 2 * ch, listTop - rowH / 2 - ch / 2, "^", C_DIM, s);
  if(top + visible < rowCount) ui_text(px + panelW - 2 * ch, listBottom, "v", C_DIM, s);

  // help line (word wrapped to two lines)
  ui_rect(px + 2 * ch, py + panelH - 4 * ch - s, panelW - 4 * ch, bt, C_LINE);
  if(help) {
    int cw = ui_textWidth("M", s);
    int maxChars = (panelW - 4 * ch) / (cw > 0 ? cw : ch);
    int len = (int)strlen(help);
    if(len <= maxChars) {
      ui_text(px + 2 * ch, py + panelH - 2 * ch - 4 * s, help, C_HELP, s);
    } else {
      int cut = maxChars;
      while(cut > 0 && help[cut] != ' ') cut--;
      if(cut == 0) cut = maxChars;
      char a[160], b[160];
      snprintf(a, sizeof(a), "%.*s", cut, help);
      snprintf(b, sizeof(b), "%.*s", maxChars, help + cut + (help[cut] == ' '));
      ui_text(px + 2 * ch, py + panelH - 3 * ch - 2 * s, a, C_HELP, s);
      ui_text(px + 2 * ch, py + panelH - 2 * ch, b, C_HELP, s);
    }
  }

  if(confirmText) {
    int w = ui_textWidth(confirmText, s) + 8 * ch;
    if(w < 36 * ch) w = 36 * ch;
    int h = 7 * ch;
    int x = (winW - w) / 2, y = (winH - h) / 2;
    ui_frame(x, y, w, h, (UiColor){ 20, 30, 56, 250 }, C_TITLE, bt);
    ui_text(x + (w - ui_textWidth(confirmText, s)) / 2, y + 2 * ch, confirmText, UI_WHITE, s);
    const char* hint = "Confirm = yes    Back = no";
    ui_text(x + (w - ui_textWidth(hint, s)) / 2, y + 4 * ch + s, hint, C_HELP, s);
  }
}
