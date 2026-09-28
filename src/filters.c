// Colour profiles. The SNES outputs 15-bit colour that was tuned on CRT
// televisions; on modern panels the raw values can look flat or washed out.
// Each profile is a small set of adjustments baked into a 32K-entry table that
// the PPU applies to every 15-bit colour before the master brightness.
#include <math.h>
#include <string.h>
#include "filters.h"

typedef struct Profile {
  float gamma;      // exponent applied to linear-ish input (>1 = richer mid tones)
  float saturation;
  float contrast;
  float brightness;
  float rGain, gGain, bGain;
  float lift;       // raises black level
} Profile;

static const Profile profiles[COLOUR_COUNT] = {
  [COLOUR_ORIGINAL]    = { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f },
  [COLOUR_VIVID]       = { 1.06f, 1.28f, 1.06f, 1.02f, 1.00f, 1.00f, 1.00f, 0.00f },
  [COLOUR_EXTRA_VIVID] = { 1.10f, 1.55f, 1.10f, 1.03f, 1.00f, 1.00f, 1.00f, 0.00f },
  [COLOUR_NATURAL]     = { 1.14f, 1.06f, 1.02f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f },
  [COLOUR_WARM]        = { 1.14f, 1.10f, 1.02f, 1.00f, 1.04f, 1.00f, 0.90f, 0.00f },
  [COLOUR_COOL]        = { 1.04f, 1.10f, 1.02f, 1.03f, 0.95f, 1.00f, 1.06f, 0.00f },
  [COLOUR_SOFT]        = { 0.95f, 0.85f, 0.92f, 1.00f, 1.00f, 1.00f, 1.00f, 0.03f },
};

const char* colourModeNames[COLOUR_COUNT] = {
  "Original", "Vivid", "Extra vivid", "Natural (CRT)", "Warm CRT", "Cool", "Soft"
};

static uint32_t lut[32768];

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

bool filters_build(int mode, int brightness, int contrast, int saturation, int gamma) {
  if(mode < 0 || mode >= COLOUR_COUNT) mode = 0;
  if(mode == COLOUR_ORIGINAL && brightness == 100 && contrast == 100 && saturation == 100 && gamma == 100) {
    return false; // identity: let the PPU use its plain path
  }
  const Profile* p = &profiles[mode];
  float g = p->gamma * (100.0f / (gamma > 10 ? gamma : 10));
  float sat = p->saturation * saturation / 100.0f;
  float con = p->contrast * contrast / 100.0f;
  float bri = p->brightness * brightness / 100.0f;
  for(int c = 0; c < 32768; c++) {
    int r5 = c & 31, g5 = (c >> 5) & 31, b5 = (c >> 10) & 31;
    float rgb[3] = {
      ((r5 << 3) | (r5 >> 2)) / 255.0f,
      ((g5 << 3) | (g5 >> 2)) / 255.0f,
      ((b5 << 3) | (b5 >> 2)) / 255.0f,
    };
    for(int i = 0; i < 3; i++) rgb[i] = powf(rgb[i], g);
    rgb[0] *= p->rGain; rgb[1] *= p->gGain; rgb[2] *= p->bGain;
    float luma = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
    for(int i = 0; i < 3; i++) {
      float v = luma + (rgb[i] - luma) * sat;
      v = (v - 0.5f) * con + 0.5f;
      v = v * bri;
      v = p->lift + v * (1.0f - p->lift);
      rgb[i] = clamp01(v);
    }
    uint32_t r = (uint32_t)(rgb[0] * 255.0f + 0.5f);
    uint32_t gg = (uint32_t)(rgb[1] * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(rgb[2] * 255.0f + 0.5f);
    lut[c] = (r << 16) | (gg << 8) | b;
  }
  return true;
}

void filters_apply(Ppu* ppu, int mode, int brightness, int contrast, int saturation, int gamma) {
  ppu->colorLut = filters_build(mode, brightness, contrast, saturation, gamma) ? lut : NULL;
}
