#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

// On-screen touch controls (phones and tablets; touch screens on PC too): a
// D-pad, the four SNES face buttons, L/R, Start, Menu and fast forward drawn
// over the picture. Shown once the screen is touched (or from the start on
// Android), hidden again when a controller or the keyboard is used.

enum { TOUCH_OFF = 0, TOUCH_AUTO, TOUCH_ALWAYS };

void touch_handleEvent(const SDL_Event* e);
void touch_frame(void);                // once per input update, before the queries
void touch_noteOtherInput(void);       // a key or controller button was used
bool touch_visible(void);
uint16_t touch_snesMask(void);         // SNES buttons held on the overlay
bool touch_menuHeld(void);
bool touch_fastForwardHeld(void);
int touch_navDir(void);                // NAV_UP..NAV_RIGHT from the D-pad, 0 if none
bool touch_takeBack(void);             // the menu's BACK pressed since the last call
void touch_setMenuOpen(bool open);     // the menu shows only MENU and BACK (rows are tapped)
bool touch_takeTap(int* x, int* y);    // the menu was tapped away from the controls
int touch_takeScroll(int rowH);        // rows dragged since the last call (+ = down the list)
void touch_render(SDL_Renderer* r, int w, int h);
