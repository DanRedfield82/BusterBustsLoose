#pragma once
// Services the frontend (main.c) offers to the menus.
#include <stdbool.h>
#include "bbl.h"

Bbl* app_game(void);
void app_applyVideo(void);         // window mode, size, vsync
void app_applyGameSettings(void);  // widescreen, colours, sprite limit, cheats
void app_toast(const char* fmt, ...);
void app_resetGame(void);
bool app_saveState(int slot);
bool app_loadState(int slot);
bool app_stateExists(int slot, char* info, int infoSize);
bool app_levelSelectReady(void);
bool app_levelSelectFailed(void);
void app_startLevel(int entry);   // index into levelAreas
void app_quit(void);
void app_closeMenu(void);
void app_saveConfig(void);
bool app_musicOpen(void);          // prepares the music player instance
void app_musicClose(void);
int app_displayCount(void);
void app_displayName(int index, char* out, int size);
int app_windowWidth(void);
int app_windowHeight(void);
