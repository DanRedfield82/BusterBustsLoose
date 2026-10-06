#include <stdlib.h>
#include <string.h>
#include "finput.h"
#include "config.h"
#include "touch.h"

#define MAX_PADS 8
#define STICK_DEADZONE 12000
#define TRIGGER_THRESHOLD 16000
// menu navigation from pads: hold time before repeating, and repeat interval
#define NAV_REPEAT_DELAY_MS 400
#define NAV_REPEAT_RATE_MS 110
// the stick has to be pushed past PRESS to move the cursor and back under
// RELEASE before the same direction counts as a new press
#define NAV_STICK_PRESS 22000
#define NAV_STICK_RELEASE 12000

static SDL_GameController* pads[MAX_PADS];
static bool keyDown[SDL_NUM_SCANCODES];
static bool actionNow[ACT_COUNT], actionPrev[ACT_COUNT];
static int navQueue[16];
static int navHead, navTail;
static int navHeldDir;
static uint32_t navNextRepeat;
static int stickNavDir;

// Android's Back button / gesture opens the menu (and steps back inside it)
static bool backMenuPending;
static bool menuActive;

static int captureKind = CAPTURE_NONE;
static bool captureHasResult;
static int captureResult;

static void navPush(int n) {
  int next = (navTail + 1) % 16;
  if(next == navHead) return;
  navQueue[navTail] = n;
  navTail = next;
}

void in_init(void) {
  SDL_GameControllerEventState(SDL_ENABLE);
  for(int i = 0; i < SDL_NumJoysticks() && i < MAX_PADS; i++) {
    if(SDL_IsGameController(i)) pads[i] = SDL_GameControllerOpen(i);
  }
}

void in_shutdown(void) {
  for(int i = 0; i < MAX_PADS; i++) {
    if(pads[i]) SDL_GameControllerClose(pads[i]);
    pads[i] = NULL;
  }
}

static void openPad(int deviceIndex) {
  if(!SDL_IsGameController(deviceIndex)) return;
  SDL_GameController* c = SDL_GameControllerOpen(deviceIndex);
  if(!c) return;
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
  for(int i = 0; i < MAX_PADS; i++) {
    if(pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
      SDL_GameControllerClose(c); // already open
      return;
    }
  }
  for(int i = 0; i < MAX_PADS; i++) {
    if(!pads[i]) { pads[i] = c; return; }
  }
  SDL_GameControllerClose(c);
}

static void closePad(SDL_JoystickID id) {
  for(int i = 0; i < MAX_PADS; i++) {
    if(pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
      SDL_GameControllerClose(pads[i]);
      pads[i] = NULL;
    }
  }
}

void in_handleEvent(const SDL_Event* e) {
  touch_handleEvent(e);
  switch(e->type) {
    case SDL_CONTROLLERDEVICEADDED: openPad(e->cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED: closePad(e->cdevice.which); break;
    case SDL_KEYDOWN: {
      int sc = e->key.keysym.scancode;
      if(sc == SDL_SCANCODE_AC_BACK) {
        if(!e->key.repeat) { if(menuActive) navPush(NAV_BACK); else backMenuPending = true; }
        return;
      }
      touch_noteOtherInput();
      if(sc > 0 && sc < SDL_NUM_SCANCODES) keyDown[sc] = true;
      if(captureKind == CAPTURE_KEY && !e->key.repeat) {
        captureResult = (sc == SDL_SCANCODE_ESCAPE) ? -1 : sc;
        captureHasResult = true;
        captureKind = CAPTURE_NONE;
        return;
      }
      if(captureKind == CAPTURE_PAD && sc == SDL_SCANCODE_ESCAPE) {
        captureResult = -1; captureHasResult = true; captureKind = CAPTURE_NONE;
        return;
      }
      if(captureKind != CAPTURE_NONE) return;
      // fixed menu keys (repeat handled by the OS for keyboards)
      switch(sc) {
        case SDL_SCANCODE_UP: navPush(NAV_UP); break;
        case SDL_SCANCODE_DOWN: navPush(NAV_DOWN); break;
        case SDL_SCANCODE_LEFT: navPush(NAV_LEFT); break;
        case SDL_SCANCODE_RIGHT: navPush(NAV_RIGHT); break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_Z: if(!e->key.repeat) navPush(NAV_OK); break;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_X:
          if(!e->key.repeat) navPush(NAV_BACK); break;
        case SDL_SCANCODE_PAGEUP: navPush(NAV_PAGE_UP); break;
        case SDL_SCANCODE_PAGEDOWN: navPush(NAV_PAGE_DOWN); break;
        case SDL_SCANCODE_DELETE: if(!e->key.repeat) navPush(NAV_EXTRA); break;
        default: break;
      }
      break;
    }
    case SDL_KEYUP: {
      int sc = e->key.keysym.scancode;
      if(sc > 0 && sc < SDL_NUM_SCANCODES) keyDown[sc] = false;
      break;
    }
    case SDL_CONTROLLERBUTTONDOWN: {
      touch_noteOtherInput();
      if(captureKind == CAPTURE_PAD) {
        captureResult = e->cbutton.button;
        captureHasResult = true;
        captureKind = CAPTURE_NONE;
        return;
      }
      if(captureKind != CAPTURE_NONE) return;
      switch(e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A: navPush(NAV_OK); break;
        case SDL_CONTROLLER_BUTTON_B: navPush(NAV_BACK); break;
        case SDL_CONTROLLER_BUTTON_Y: navPush(NAV_EXTRA); break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: navPush(NAV_PAGE_UP); break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: navPush(NAV_PAGE_DOWN); break;
        default: break;
      }
      break;
    }
    case SDL_CONTROLLERAXISMOTION: {
      if(captureKind == CAPTURE_PAD && e->caxis.value > TRIGGER_THRESHOLD &&
         (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        captureResult = PAD_AXIS_BASE + (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        captureHasResult = true;
        captureKind = CAPTURE_NONE;
      }
      break;
    }
    default: break;
  }
}

static bool padBindHeld(int code) {
  if(code < 0) return false;
  for(int i = 0; i < MAX_PADS; i++) {
    if(!pads[i]) continue;
    if(code >= PAD_AXIS_BASE) {
      int axis = code == PAD_AXIS_BASE ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
      if(SDL_GameControllerGetAxis(pads[i], axis) > TRIGGER_THRESHOLD) return true;
    } else if(code < SDL_CONTROLLER_BUTTON_MAX) {
      if(SDL_GameControllerGetButton(pads[i], code)) return true;
    }
  }
  return false;
}

static void stickDirs(bool* up, bool* down, bool* left, bool* right) {
  *up = *down = *left = *right = false;
  for(int i = 0; i < MAX_PADS; i++) {
    if(!pads[i]) continue;
    int x = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTX);
    int y = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTY);
    if(x < -STICK_DEADZONE) *left = true;
    if(x > STICK_DEADZONE) *right = true;
    if(y < -STICK_DEADZONE) *up = true;
    if(y > STICK_DEADZONE) *down = true;
  }
}

// Menu direction from the left sticks: only the dominant axis counts (no
// flicker between two directions on diagonals) and a direction is kept until
// the stick falls back under the release threshold.
static int stickMenuDir(void) {
  int bestMag = 0, bestDir = 0, heldMag = 0;
  for(int i = 0; i < MAX_PADS; i++) {
    if(!pads[i]) continue;
    int x = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTX);
    int y = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTY);
    int mag, d;
    if(abs(x) >= abs(y)) { mag = abs(x); d = x < 0 ? NAV_LEFT : NAV_RIGHT; }
    else { mag = abs(y); d = y < 0 ? NAV_UP : NAV_DOWN; }
    if(mag > bestMag) { bestMag = mag; bestDir = d; }
    // how far the stick still leans in the direction currently held
    int along = 0;
    switch(stickNavDir) {
      case NAV_LEFT: along = -x; break;
      case NAV_RIGHT: along = x; break;
      case NAV_UP: along = -y; break;
      case NAV_DOWN: along = y; break;
    }
    if(along > heldMag) heldMag = along;
  }
  if(stickNavDir != 0 && heldMag > NAV_STICK_RELEASE && (bestDir == stickNavDir || bestMag <= NAV_STICK_PRESS)) {
    return stickNavDir;
  }
  stickNavDir = bestMag > NAV_STICK_PRESS ? bestDir : 0;
  return stickNavDir;
}

void in_setMenuActive(bool active) { menuActive = active; }

// keys that already have a meaning while the menu is open
static bool isMenuNavKey(int sc) {
  switch(sc) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: case SDL_SCANCODE_Z:
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_X:
    case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_DELETE:
      return true;
    default:
      return false;
  }
}

static bool snesButtonHeld(int b) {
  for(int k = 0; k < 2; k++) {
    int sc = cfg.keyBind[b][k];
    if(sc > 0 && sc < SDL_NUM_SCANCODES && keyDown[sc]) return true;
  }
  return padBindHeld(cfg.padBind[b]);
}

// Select as the menu button: inside the menu, keys that navigate keep doing that
static bool selectMenuHeld(void) {
  for(int k = 0; k < 2; k++) {
    int sc = cfg.keyBind[BTN_SELECT][k];
    if(sc > 0 && sc < SDL_NUM_SCANCODES && keyDown[sc] && !(menuActive && isMenuNavKey(sc))) return true;
  }
  return padBindHeld(cfg.padBind[BTN_SELECT]);
}

uint16_t in_snesMask(void) {
  uint16_t m = 0;
  for(int b = 0; b < BTN_COUNT; b++) {
    // the game only uses Select as a second Start, so it can be the menu button
    if(b == BTN_SELECT && cfg.selectOpensMenu) continue;
    if(snesButtonHeld(b)) m |= 1 << b;
  }
  if(cfg.analogStick) {
    bool u, d, l, r;
    stickDirs(&u, &d, &l, &r);
    if(u) m |= 1 << BTN_UP;
    if(d) m |= 1 << BTN_DOWN;
    if(l) m |= 1 << BTN_LEFT;
    if(r) m |= 1 << BTN_RIGHT;
  }
  m |= touch_snesMask();
  // a real pad cannot press opposite directions at once
  if((m & (1 << BTN_LEFT)) && (m & (1 << BTN_RIGHT))) m &= ~((1 << BTN_LEFT) | (1 << BTN_RIGHT));
  if((m & (1 << BTN_UP)) && (m & (1 << BTN_DOWN))) m &= ~((1 << BTN_UP) | (1 << BTN_DOWN));
  return m;
}

void in_update(void) {
  touch_frame();
  memcpy(actionPrev, actionNow, sizeof(actionNow));
  for(int a = 0; a < ACT_COUNT; a++) {
    int sc = cfg.keyAction[a];
    bool held = sc > 0 && sc < SDL_NUM_SCANCODES && keyDown[sc];
    if(padBindHeld(cfg.padAction[a])) held = true;
    actionNow[a] = held;
  }
  if(touch_menuHeld() || backMenuPending) actionNow[ACT_MENU] = true;
  backMenuPending = false;
  if(touch_fastForwardHeld()) actionNow[ACT_FAST_FORWARD] = true;
  touch_setMenuOpen(menuActive);
  if(cfg.selectOpensMenu) {
    if(selectMenuHeld()) actionNow[ACT_MENU] = true;
  } else if(padBindHeld(SDL_CONTROLLER_BUTTON_BACK) && padBindHeld(SDL_CONTROLLER_BUTTON_START)) {
    // Select + Start together opens the menu (not every pad exposes a guide button)
    actionNow[ACT_MENU] = true;
  }
  // no shortcuts fire while a new binding is being entered, nor from the
  // button that was just bound (it is still held when the capture ends)
  if(captureKind != CAPTURE_NONE || captureHasResult) memcpy(actionPrev, actionNow, sizeof(actionNow));

  // pad d-pad / stick navigation with auto repeat. Timing is in milliseconds so
  // it does not depend on the display refresh rate.
  int dir = 0;
  if(padBindHeld(SDL_CONTROLLER_BUTTON_DPAD_UP)) dir = NAV_UP;
  else if(padBindHeld(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) dir = NAV_DOWN;
  else if(padBindHeld(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) dir = NAV_LEFT;
  else if(padBindHeld(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) dir = NAV_RIGHT;
  else dir = stickMenuDir();
  if(dir == 0 && menuActive) dir = touch_navDir();
  // (the touch BACK button)
  if(touch_takeBack() && menuActive && captureKind == CAPTURE_NONE) navPush(NAV_BACK);
  if(captureKind != CAPTURE_NONE) dir = 0;
  uint32_t now = SDL_GetTicks();
  if(dir == 0) {
    navHeldDir = 0;
  } else if(dir != navHeldDir) {
    navHeldDir = dir;
    navNextRepeat = now + NAV_REPEAT_DELAY_MS;
    navPush(dir); // NAV_UP..NAV_RIGHT share the indices 1..4
  } else if((int32_t)(now - navNextRepeat) >= 0) {
    navNextRepeat = now + NAV_REPEAT_RATE_MS;
    navPush(dir);
  }
}

bool in_actionPressed(int action) {
  return actionNow[action] && !actionPrev[action];
}

bool in_actionHeld(int action) {
  return actionNow[action];
}

int in_nav(void) {
  if(navHead == navTail) return NAV_NONE;
  int n = navQueue[navHead];
  navHead = (navHead + 1) % 16;
  return n;
}

void in_injectNav(int nav) {
  if(nav != NAV_NONE) navPush(nav);
}

void in_clearNav(void) {
  navHead = navTail = 0;
}

const char* in_padName(void) {
  for(int i = 0; i < MAX_PADS; i++) if(pads[i]) return SDL_GameControllerName(pads[i]);
  return NULL;
}

int in_padCount(void) {
  int n = 0;
  for(int i = 0; i < MAX_PADS; i++) if(pads[i]) n++;
  return n;
}

void in_beginCapture(int kind) {
  captureKind = kind;
  captureHasResult = false;
  in_clearNav();
}

bool in_captureDone(int* code) {
  if(!captureHasResult) return false;
  captureHasResult = false;
  *code = captureResult;
  return true;
}

bool in_capturing(void) {
  return captureKind != CAPTURE_NONE;
}

const char* in_keyName(int scancode) {
  if(scancode <= 0) return "-";
  const char* n = SDL_GetScancodeName(scancode);
  return (n && *n) ? n : "?";
}

const char* in_padButtonName(int code) {
  static const char* names[] = {
    "A (bottom)", "B (right)", "X (left)", "Y (top)", "Back/View", "Guide", "Start/Menu",
    "L-Stick", "R-Stick", "LB", "RB", "D-Up", "D-Down", "D-Left", "D-Right",
    "Misc", "Paddle1", "Paddle2", "Paddle3", "Paddle4", "Touchpad"
  };
  if(code < 0) return "-";
  if(code == PAD_AXIS_BASE) return "LT";
  if(code == PAD_AXIS_BASE + 1) return "RT";
  if(code < (int)(sizeof(names) / sizeof(names[0]))) return names[code];
  return "?";
}
