#pragma once
#include <stdbool.h>
#include <stdint.h>

#define AUDIO_RATE 48000
#define SNES_FPS 60.0988

bool audio_init(void);
void audio_shutdown(void);
// how many stereo samples the next emulated frame should produce (dynamic rate control)
int audio_samplesForFrame(void);
// queue stereo int16 samples, applying volume / mono settings
void audio_push(const int16_t* samples, int count);
void audio_pushSilence(int count);
int audio_queued(void);
void audio_clear(void);
void audio_setPaused(bool paused);
// peak levels of the last pushed block (0..1), for meters
void audio_levels(float* left, float* right);
