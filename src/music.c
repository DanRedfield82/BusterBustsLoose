// Music player / sound test.
//
// The game keeps a table of 36 music entries at $82:FE69 (10 bytes each: three
// long pointers to data uploaded to the sound CPU plus the song command).
// $80:9A4D (A = entry) stops the sound driver (command $B7), and queues the
// upload which the idle loop performs via $80:9AA9. When the upload is done the
// song command is sent to APU port 0. Normally the NMI code sends commands to
// the port with an echo handshake; here NMI is disabled so we do that part.
#include <stdlib.h>
#include <string.h>
#include "music.h"
#include "bbl.h"
#include "dsp.h"

const MusicTrack musicTracks[] = {
  { "Title Theme",          2,  0 },
  { "Stage Intro",          17, 0 },
  { "Acme Looniversity",    3,  0 },
  { "Wild West",            4,  0 },
  { "Runaway Train",        30, 0 },
  { "Wild West Boss",       23, 0 },
  { "Scary Mansion",        5,  0 },
  { "Laboratory",           20, 0 },
  { "Football Game",        12, 0 },
  { "Sky High",             11, 0 },
  { "Boss Theme",           6,  0 },
  { "Star Warners",         10, 0 },
  { "Final Boss",           32, 0 },
  { "Ending",               27, 0 },
  { "Epilogue",             34, 0 },
  { "Staff Roll",           29, 0 },
  { "Password Screen",      19, 0 },
  { "Extra Theme 1",        7,  0 },
  { "Extra Theme 2",        8,  0 },
  { "Extra Theme 3",        31, 0 },
  { "Extra Theme 4",        26, 0 },
  { "Extra Theme 5",        22, 3 },
  { "Stage Clear",          24, 0 },
  { "Fanfare 1",            33, 0 },
  { "Fanfare 2",            35, 0 },
  { "Jingle",               14, 3 },
  { "Konami Jingle",        1,  3 },
};
const int musicTrackCount = sizeof(musicTracks) / sizeof(musicTracks[0]);

static Bbl* m;
static int state = MUSIC_IDLE;
static int current = -1;
static int pendingEntry = -1;   // entry to request once the preload finished
static uint8_t pendingSong;
static uint32_t playFrames;
static uint8_t cmdQueue[16];
static int cmdCount;
static bool waitingEcho;
static uint8_t lastCmd;

static void requestEntry(int entry);

static uint8_t songCommand(int entry) {
  return bbl_romRead(m, 0x82FE69 + entry * 10 + 9);
}

bool music_open(const char* romPath, const uint8_t* baseState, int baseSize) {
  if(m) return true;
  char err[256];
  m = bbl_create(romPath, err, sizeof(err));
  if(!m) return false;
  if(!bbl_loadState(m, baseState, baseSize)) { bbl_destroy(m); m = NULL; return false; }
  // stop the game: no NMI (game logic) and no IRQs; the idle loop keeps
  // running and performs sound uploads for us
  m->snes->nmiEnabled = false;
  m->snes->hIrqEnabled = false;
  m->snes->vIrqEnabled = false;
  bbl_poke(m, 0x7E1F02, bbl_peek(m, 0x7E1F02) & 0x0f); // NMITIMEN shadow used after uploads
  m->snes->ppu->forcedBlank = true;
  state = MUSIC_IDLE;
  current = -1;
  cmdCount = 0;
  waitingEcho = false;
  music_stop();
  // entry 0 uploads the common sample set without a song, so the sound test
  // works before any track has been played
  requestEntry(0);
  pendingEntry = -1;
  return true;
}

void music_close(void) {
  if(m) bbl_destroy(m);
  m = NULL;
  state = MUSIC_IDLE;
  current = -1;
}

bool music_isOpen(void) { return m != NULL; }

static void queueCmd(uint8_t c) {
  if(cmdCount < 16) cmdQueue[cmdCount++] = c;
}

static void requestEntry(int entry) {
  waitingEcho = false;
  bbl_callRoutine(m, 0x809A4D, entry);
  pendingSong = songCommand(entry);
  state = MUSIC_LOADING;
}

void music_play(int track) {
  if(!m || track < 0 || track >= musicTrackCount) return;
  current = track;
  cmdCount = 0;
  const MusicTrack* t = &musicTracks[track];
  if(t->preload) {
    requestEntry(t->preload);
    pendingEntry = t->index;
  } else {
    requestEntry(t->index);
    pendingEntry = -1;
  }
}

void music_stop(void) {
  if(!m) return;
  cmdCount = 0;
  waitingEcho = false;
  pendingEntry = -1;
  bbl_callRoutine(m, 0x808326, 0); // sends $B7 (stop) and waits for the driver
  state = MUSIC_IDLE;
  current = -1;
}

void music_sfx(int id) {
  if(!m) return;
  queueCmd((uint8_t)id); // sent once any upload has finished
}

static void pumpCommands(void) {
  Apu* apu = m->snes->apu;
  if(waitingEcho) {
    if(apu->outPorts[0] != lastCmd) return;
    waitingEcho = false;
  }
  if(cmdCount > 0) {
    lastCmd = cmdQueue[0];
    memmove(cmdQueue, cmdQueue + 1, --cmdCount);
    apu->inPorts[0] = lastCmd;
    waitingEcho = true;
  }
}

void music_frame(int16_t* out, int samples) {
  if(!m) { memset(out, 0, samples * 4); return; }
  if(state == MUSIC_LOADING) {
    // uploads run in the idle loop; run a few frames at once to keep it short
    for(int i = 0; i < 8 && bbl_peek16(m, 0x7E1E2C) != 0; i++) {
      bbl_runFrame(m);
      int16_t dummy[1068 * 2];
      bbl_getSamples(m, dummy, 800);
    }
    if(bbl_peek16(m, 0x7E1E2C) == 0) {
      if(pendingEntry >= 0) {
        int e = pendingEntry;
        pendingEntry = -1;
        requestEntry(e);
      } else if(pendingSong == 0) {
        state = MUSIC_IDLE; // samples only
      } else {
        queueCmd(0xB7);
        queueCmd(pendingSong);
        state = MUSIC_PLAYING;
        playFrames = 0;
      }
    }
  }
  if(state != MUSIC_LOADING) pumpCommands();
  bbl_runFrame(m);
  bbl_getSamples(m, out, samples);
  if(state == MUSIC_LOADING) memset(out, 0, samples * 4);
  if(state == MUSIC_PLAYING) playFrames++;
}

int music_state(void) { return state; }
int music_current(void) { return current; }
uint32_t music_playFrames(void) { return playFrames; }

void music_voices(float levels[8]) {
  for(int i = 0; i < 8; i++) levels[i] = 0;
  if(!m) return;
  Dsp* dsp = m->snes->apu->dsp;
  for(int i = 0; i < 8; i++) {
    int vol = abs(dsp->channel[i].volumeL) + abs(dsp->channel[i].volumeR);
    float env = dsp->channel[i].gain / 2047.0f;
    float v = env * (vol / 128.0f + 0.25f);
    levels[i] = v > 1 ? 1 : v;
  }
}
