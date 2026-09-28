#include <string.h>
#include <stdlib.h>
#include <SDL.h>
#include "audio.h"
#include "config.h"

static SDL_AudioDeviceID dev;
static int16_t mixBuf[4096 * 2];
static float peakL, peakR;
static int targetQueued;

bool audio_init(void) {
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = AUDIO_RATE;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = NULL; // we queue
  dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if(!dev) return false;
  // keep ~3 frames of audio queued: enough to ride out hiccups, low latency
  targetQueued = (int)(AUDIO_RATE / SNES_FPS * 3);
  SDL_PauseAudioDevice(dev, 0);
  return true;
}

void audio_shutdown(void) {
  if(dev) SDL_CloseAudioDevice(dev);
  dev = 0;
}

int audio_queued(void) {
  if(!dev) return 0;
  return (int)(SDL_GetQueuedAudioSize(dev) / 4);
}

int audio_samplesForFrame(void) {
  double base = AUDIO_RATE / SNES_FPS;
  if(!dev) return (int)base;
  // nudge the rate by up to +-0.5% to keep the queue near its target
  static double frac = 0;
  double err = (double)(targetQueued - audio_queued()) / targetQueued;
  if(err > 1) err = 1;
  if(err < -1) err = -1;
  double want = base * (1.0 + 0.005 * err) + frac;
  int n = (int)want;
  frac = want - n;
  if(n > 4096) n = 4096;
  return n;
}

void audio_push(const int16_t* samples, int count) {
  if(!dev || count <= 0) return;
  if(count > 4096) count = 4096;
  // drop audio if we are hopelessly behind (e.g. after a stall)
  if(audio_queued() > targetQueued * 4) SDL_ClearQueuedAudio(dev);
  int vol = cfg.volume * 256 / 100;
  int pl = 0, pr = 0;
  for(int i = 0; i < count; i++) {
    int l = samples[i * 2], r = samples[i * 2 + 1];
    if(cfg.monoAudio) l = r = (l + r) / 2;
    l = l * vol / 256;
    r = r * vol / 256;
    if(l > 32767) l = 32767; else if(l < -32768) l = -32768;
    if(r > 32767) r = 32767; else if(r < -32768) r = -32768;
    mixBuf[i * 2] = (int16_t)l;
    mixBuf[i * 2 + 1] = (int16_t)r;
    if(abs(l) > pl) pl = abs(l);
    if(abs(r) > pr) pr = abs(r);
  }
  peakL = pl / 32768.0f;
  peakR = pr / 32768.0f;
  SDL_QueueAudio(dev, mixBuf, count * 4);
}

void audio_pushSilence(int count) {
  if(!dev || count <= 0) return;
  if(count > 4096) count = 4096;
  if(audio_queued() > targetQueued) return;
  memset(mixBuf, 0, count * 4);
  peakL = peakR = 0;
  SDL_QueueAudio(dev, mixBuf, count * 4);
}

void audio_clear(void) {
  if(dev) SDL_ClearQueuedAudio(dev);
}

void audio_setPaused(bool paused) {
  if(dev) SDL_PauseAudioDevice(dev, paused ? 1 : 0);
}

void audio_levels(float* left, float* right) {
  *left = peakL;
  *right = peakR;
}
