// Background preparation of a "stage select" save state.
//
// A second copy of the game is booted on a worker thread with the hidden
// stage select flag ($1D7E) set, pressing Start until the debug stage select
// screen is up ($30 = 3, $32 = 7, $19F0 = 1). The resulting state is used by
// the level select (set $68 = stage, $94 = area, press Start) and as the base
// for the music player (the sound driver is loaded at that point).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "bgtask.h"
#include "bbl.h"

static SDL_Thread* thread;
static SDL_atomic_t status; // 0 running, 1 ready, 2 failed
static SDL_atomic_t stopping; // the program is quitting
static uint8_t* stateData;
static int stateSize;
static char romPath[1024];

static bool atStageSelect(Bbl* g) {
  return bbl_peek16(g, 0x7E0030) == 3 && bbl_peek16(g, 0x7E0032) == 7 && bbl_peek16(g, 0x7E19F0) == 1;
}

static int worker(void* arg) {
  (void)arg;
  char err[512];
  Bbl* g = bbl_create(romPath, err, sizeof(err));
  if(!g) { SDL_AtomicSet(&status, 2); return 0; }
  bool ok = false;
  // tap Start through the intro and title until the stage select mode starts;
  // stop pressing the moment it does, otherwise the press would also confirm
  // the stage select and the saved state would already be past it
  for(int f = 0; f < 4000 && !ok && !SDL_AtomicGet(&stopping); f++) {
    bbl_poke(g, 0x7E1D7E, 1);
    bool selectMode = bbl_peek16(g, 0x7E0030) == 3 && bbl_peek16(g, 0x7E0032) == 7;
    bbl_setButtons(g, 1, (!selectMode && (f % 20) < 10) ? (1 << BTN_START) : 0);
    bbl_runFrame(g);
    if(atStageSelect(g)) {
      bbl_setButtons(g, 1, 0);
      for(int k = 0; k < 30; k++) bbl_runFrame(g);
      ok = atStageSelect(g);
    }
  }
  if(ok) {
    stateSize = bbl_saveState(g, &stateData);
    SDL_AtomicSet(&status, 1);
  } else {
    SDL_AtomicSet(&status, 2);
  }
  bbl_destroy(g);
  return 0;
}

void bg_start(const char* path) {
  snprintf(romPath, sizeof(romPath), "%s", path);
  SDL_AtomicSet(&status, 0);
  SDL_AtomicSet(&stopping, 0);
  thread = SDL_CreateThread(worker, "bbl-prepare", NULL);
  if(!thread) SDL_AtomicSet(&status, 2);
}

bool bg_ready(void) { return SDL_AtomicGet(&status) == 1; }
bool bg_failed(void) { return SDL_AtomicGet(&status) == 2; }

void bg_wait(void) {
  if(thread) {
    SDL_WaitThread(thread, NULL);
    thread = NULL;
  }
}

const uint8_t* bg_state(int* size) {
  if(!bg_ready()) return NULL;
  *size = stateSize;
  return stateData;
}

void bg_shutdown(void) {
  SDL_AtomicSet(&stopping, 1);
  bg_wait();
  free(stateData);
  stateData = NULL;
}
