#pragma once
#include <stdbool.h>
#include <stdint.h>

// Music player / sound test. Runs a second copy of the game with its game
// logic (NMI) switched off and drives the game's own sound routines.

typedef struct MusicTrack {
  const char* name;
  uint8_t index;       // entry in the game's music table at $82:FE69
  uint8_t preload;     // entry to load first for tracks that reuse samples (0 = none)
} MusicTrack;

extern const MusicTrack musicTracks[];
extern const int musicTrackCount;

enum { MUSIC_IDLE = 0, MUSIC_LOADING, MUSIC_PLAYING };

bool music_open(const char* romPath, const uint8_t* baseState, int baseSize);
void music_close(void);
bool music_isOpen(void);
void music_play(int track);
void music_stop(void);
void music_sfx(int id);
// run one frame of the sound instance and produce audio
void music_frame(int16_t* out, int samples);
int music_state(void);
int music_current(void);
uint32_t music_playFrames(void);   // frames since the current track started
// voice activity 0..1 for the 8 DSP channels
void music_voices(float levels[8]);
