#include <string.h>
#include <stdlib.h>
#include "ui.h"
#include "uifont.h"

// Text uses anti-aliased Cascadia Mono Bold glyphs (uifont.h), baked at three
// sizes; each string is drawn from the smallest atlas at least as large as
// needed and scaled down smoothly. A `scale` of 1 is the old 8x8 font's size:
// capitals are 7 * scale pixels tall, starting at y.

static SDL_Renderer* ren;
static SDL_Texture* fontTex[UIFONT_SIZES];

#define ATLAS_COLS 16

void ui_init(SDL_Renderer* r) {
  ren = r;
  for(int s = 0; s < UIFONT_SIZES; s++) {
    const UiFontSize* f = &uifontSizes[s];
    int rows = (UIFONT_GLYPHS + ATLAS_COLS - 1) / ATLAS_COLS;
    int aw = f->cellW * ATLAS_COLS, ah = f->cellH * rows;
    uint32_t* px = calloc((size_t)aw * ah, 4);
    if(!px) continue;
    // white everywhere (so smooth scaling never darkens edges), coverage in alpha
    for(int i = 0; i < aw * ah; i++) px[i] = 0x00FFFFFF;
    int in = 0;
    for(int g = 0; g < UIFONT_GLYPHS; g++) {
      int gx = (g % ATLAS_COLS) * f->cellW, gy = (g / ATLAS_COLS) * f->cellH;
      int n = 0;
      while(n < f->cellW * f->cellH && in < f->dataLen) {
        uint8_t v = f->data[in++];
        if(v == 0) { n += in < f->dataLen ? f->data[in++] : 0; continue; }
        px[(gy + n / f->cellW) * aw + gx + n % f->cellW] = ((uint32_t)v << 24) | 0x00FFFFFF;
        n++;
      }
    }
    fontTex[s] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, aw, ah);
    if(fontTex[s]) {
      SDL_UpdateTexture(fontTex[s], NULL, px, aw * 4);
      SDL_SetTextureBlendMode(fontTex[s], SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(fontTex[s], SDL_ScaleModeLinear);
    }
    free(px);
  }
}

void ui_shutdown(void) {
  for(int s = 0; s < UIFONT_SIZES; s++) {
    if(fontTex[s]) SDL_DestroyTexture(fontTex[s]);
    fontTex[s] = NULL;
  }
}

// the next character of UTF-8 text as a glyph: ASCII, the em dash, or '?'
// for anything else the font lacks
static unsigned char nextGlyph(const char** p) {
  const unsigned char* s = (const unsigned char*)*p;
  unsigned char ch = *s++;
  if(ch >= 0x80) {
    bool emdash = ch == 0xE2 && s[0] == 0x80 && s[1] == 0x94;
    while((*s & 0xC0) == 0x80) s++;
    ch = emdash ? UI_GLYPH_EMDASH : '?';
  }
  *p = (const char*)s;
  return ch;
}

static int glyphIndex(unsigned char ch) {
  if(ch == UI_GLYPH_EMDASH) return 0;
  if(ch >= 32 && ch < 127) return ch - 31;
  return '?' - 31;
}

// the atlas for text of this scale, and the factor from atlas to screen pixels
static int pickSize(int scale, float* k) {
  float cap = 7.0f * (scale > 0 ? scale : 1);
  int s = 0;
  while(s < UIFONT_SIZES - 1 && uifontSizes[s].capH < cap) s++;
  *k = cap / uifontSizes[s].capH;
  return s;
}

void ui_text(int x, int y, const char* s, UiColor c, int scale) {
  float k;
  int si = pickSize(scale, &k);
  const UiFontSize* f = &uifontSizes[si];
  SDL_Texture* tex = fontTex[si];
  if(!tex) return;
  SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
  SDL_SetTextureAlphaMod(tex, c.a);
  float adv = f->advance * k;
  float cx = (float)x, top = y - f->capTop * k;
  while(*s) {
    unsigned char ch = nextGlyph(&s);
    if(ch == '\n') { cx = (float)x; top += 10.0f * scale; continue; }
    if(ch != ' ') {
      int g = glyphIndex(ch);
      SDL_Rect src = { (g % ATLAS_COLS) * f->cellW, (g / ATLAS_COLS) * f->cellH, f->cellW, f->cellH };
      SDL_FRect dst = { cx - k, top, f->cellW * k, f->cellH * k };
      SDL_RenderCopyF(ren, tex, &src, &dst);
    }
    cx += adv;
  }
}

void ui_textShadow(int x, int y, const char* s, UiColor c, int scale) {
  int o = scale > 1 ? scale / 2 : 1;
  ui_text(x + o, y + o, s, (UiColor){ 0, 0, 0, (uint8_t)(c.a / 2) }, scale);
  ui_text(x, y, s, c, scale);
}

int ui_textWidth(const char* s, int scale) {
  int n = 0, best = 0;
  while(*s) {
    if(nextGlyph(&s) == '\n') { if(n > best) best = n; n = 0; } else n++;
  }
  if(n > best) best = n;
  float k;
  int si = pickSize(scale, &k);
  return (int)(best * uifontSizes[si].advance * k + 0.5f);
}

void ui_rect(int x, int y, int w, int h, UiColor c) {
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
  SDL_Rect r = { x, y, w, h };
  SDL_RenderFillRect(ren, &r);
}

void ui_frame(int x, int y, int w, int h, UiColor fill, UiColor border, int t) {
  ui_rect(x, y, w, h, fill);
  ui_rect(x, y, w, t, border);
  ui_rect(x, y + h - t, w, t, border);
  ui_rect(x, y, t, h, border);
  ui_rect(x + w - t, y, t, h, border);
}
