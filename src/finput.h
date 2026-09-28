#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

enum {
  NAV_NONE = 0, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK,
  NAV_PAGE_UP, NAV_PAGE_DOWN, NAV_EXTRA // NAV_EXTRA: secondary action (e.g. clear binding)
};

void in_init(void);
void in_shutdown(void);
void in_handleEvent(const SDL_Event* e);
// call once per rendered frame, after events
void in_update(void);
void in_setMenuActive(bool active);   // tells the input code whether the menu is showing

uint16_t in_snesMask(void);        // current SNES joypad state from all devices
bool in_actionPressed(int action); // ACT_* edge triggered this update
bool in_actionHeld(int action);
int in_nav(void);                  // next menu navigation event (auto repeat), NAV_NONE if none
void in_clearNav(void);
void in_injectNav(int nav);         // used by test scripts
const char* in_padName(void);      // name of the first connected controller or NULL
int in_padCount(void);

// binding capture for the controls menu
enum { CAPTURE_NONE = 0, CAPTURE_KEY, CAPTURE_PAD };
void in_beginCapture(int kind);
bool in_captureDone(int* code);    // true once something was pressed (code = -1 if cancelled)
bool in_capturing(void);

const char* in_keyName(int scancode);
const char* in_padButtonName(int code);
