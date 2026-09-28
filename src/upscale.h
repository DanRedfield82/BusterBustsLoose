#pragma once
#include <stdint.h>

enum {
  UPSCALE_NONE = 0,
  UPSCALE_SCALE2X,
  UPSCALE_SCALE3X,
  UPSCALE_SCALE4X,
  UPSCALE_XBR2X,
  UPSCALE_XBR4X,
  UPSCALE_COUNT
};

extern const char* upscaleNames[UPSCALE_COUNT];

// integer factor produced by a filter
int upscale_factor(int filter);
// src: w x h pixels with the given pitch (in pixels); dst: (w*f) x (h*f), tightly packed
void upscale_run(int filter, const uint32_t* src, int w, int h, int srcPitch, uint32_t* dst);
// plain pixel duplication (used for sharp-bilinear prescaling)
void upscale_nearest(const uint32_t* src, int w, int h, int srcPitch, uint32_t* dst, int factor);
