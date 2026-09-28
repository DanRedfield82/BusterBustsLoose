// CPU pixel-art upscalers: the Scale2x/Scale3x (EPX) family and xBR level 2.
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "upscale.h"

const char* upscaleNames[UPSCALE_COUNT] = {
  "None", "Scale2x", "Scale3x", "Scale4x", "xBR 2x", "xBR 4x"
};

int upscale_factor(int filter) {
  switch(filter) {
    case UPSCALE_SCALE2X: return 2;
    case UPSCALE_SCALE3X: return 3;
    case UPSCALE_SCALE4X: return 4;
    case UPSCALE_XBR2X: return 2;
    case UPSCALE_XBR4X: return 4;
    default: return 1;
  }
}

void upscale_nearest(const uint32_t* src, int w, int h, int pitch, uint32_t* dst, int f) {
  int dw = w * f;
  for(int y = 0; y < h; y++) {
    uint32_t* row = dst + (size_t)y * f * dw;
    const uint32_t* s = src + (size_t)y * pitch;
    for(int x = 0; x < w; x++) {
      uint32_t c = s[x];
      for(int k = 0; k < f; k++) row[x * f + k] = c;
    }
    for(int k = 1; k < f; k++) memcpy(row + (size_t)k * dw, row, dw * 4);
  }
}

// ---- Scale2x / Scale3x (AdvanceMAME rules) ----

static void scale2x(const uint32_t* src, int w, int h, int pitch, uint32_t* dst) {
  int dw = w * 2;
  for(int y = 0; y < h; y++) {
    const uint32_t* r0 = src + (size_t)(y > 0 ? y - 1 : 0) * pitch;
    const uint32_t* r1 = src + (size_t)y * pitch;
    const uint32_t* r2 = src + (size_t)(y < h - 1 ? y + 1 : y) * pitch;
    uint32_t* d0 = dst + (size_t)(y * 2) * dw;
    uint32_t* d1 = d0 + dw;
    for(int x = 0; x < w; x++) {
      int xl = x > 0 ? x - 1 : 0, xr = x < w - 1 ? x + 1 : x;
      uint32_t B = r0[x], D = r1[xl], E = r1[x], F = r1[xr], H = r2[x];
      uint32_t e0 = E, e1 = E, e2 = E, e3 = E;
      if(B != H && D != F) {
        if(D == B) e0 = D;
        if(B == F) e1 = F;
        if(D == H) e2 = D;
        if(H == F) e3 = F;
      }
      d0[x * 2] = e0; d0[x * 2 + 1] = e1;
      d1[x * 2] = e2; d1[x * 2 + 1] = e3;
    }
  }
}

static void scale3x(const uint32_t* src, int w, int h, int pitch, uint32_t* dst) {
  int dw = w * 3;
  for(int y = 0; y < h; y++) {
    const uint32_t* r0 = src + (size_t)(y > 0 ? y - 1 : 0) * pitch;
    const uint32_t* r1 = src + (size_t)y * pitch;
    const uint32_t* r2 = src + (size_t)(y < h - 1 ? y + 1 : y) * pitch;
    uint32_t* d0 = dst + (size_t)(y * 3) * dw;
    uint32_t* d1 = d0 + dw;
    uint32_t* d2 = d1 + dw;
    for(int x = 0; x < w; x++) {
      int xl = x > 0 ? x - 1 : 0, xr = x < w - 1 ? x + 1 : x;
      uint32_t A = r0[xl], B = r0[x], C = r0[xr];
      uint32_t D = r1[xl], E = r1[x], F = r1[xr];
      uint32_t G = r2[xl], H = r2[x], I = r2[xr];
      uint32_t o[9] = { E, E, E, E, E, E, E, E, E };
      if(B != H && D != F) {
        o[0] = D == B ? D : E;
        o[1] = ((D == B && E != C) || (B == F && E != A)) ? B : E;
        o[2] = B == F ? F : E;
        o[3] = ((D == B && E != G) || (D == H && E != A)) ? D : E;
        o[5] = ((B == F && E != I) || (H == F && E != C)) ? F : E;
        o[6] = D == H ? D : E;
        o[7] = ((D == H && E != I) || (H == F && E != G)) ? H : E;
        o[8] = H == F ? F : E;
      }
      memcpy(&d0[x * 3], &o[0], 12);
      memcpy(&d1[x * 3], &o[3], 12);
      memcpy(&d2[x * 3], &o[6], 12);
    }
  }
}

// ---- xBR level 2 (after Hyllian's reference implementation) ----
//
// The source is copied into a padded buffer (2 pixels on every side) together
// with a packed YUV value per pixel, so neighbourhood reads need no bounds
// checks. Each output block is produced in one pass; the four corners use the
// same rule with rotated neighbourhoods and rotated output cells.

static uint32_t* padC;
static int* padY;
static int padCap;

static inline int yuvOf(uint32_t c) {
  int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
  int y = (299 * r + 587 * g + 114 * b) / 1000;
  int u = (-169 * r - 331 * g + 500 * b) / 1000 + 128;
  int v = (500 * r - 419 * g - 81 * b) / 1000 + 128;
  return (y << 16) | (u << 8) | v;
}

static inline int df(int a, int b) {
  return abs(((a >> 16) & 0xff) - ((b >> 16) & 0xff)) * 48 +
         abs(((a >> 8) & 0xff) - ((b >> 8) & 0xff)) * 7 +
         abs((a & 0xff) - (b & 0xff)) * 6;
}

#define EQ(a, b) (df(a, b) < 155)

static inline uint32_t mix(uint32_t d, uint32_t s, int w) {
  int dr = (d >> 16) & 0xff, dg = (d >> 8) & 0xff, db = d & 0xff;
  int sr = (s >> 16) & 0xff, sg = (s >> 8) & 0xff, sb = s & 0xff;
  dr += ((sr - dr) * w) >> 8;
  dg += ((sg - dg) * w) >> 8;
  db += ((sb - db) * w) >> 8;
  return (uint32_t)((dr << 16) | (dg << 8) | db);
}

// neighbourhood cells used by the corner rule, as (dx, dy) for the canonical
// (bottom-right) corner
enum { N_I, N_H, N_F, N_G, N_C, N_D, N_B, N_F4, N_I4, N_H5, N_I5, N_COUNT };
static const int baseOff[N_COUNT][2] = {
  { 1, 1 }, { 0, 1 }, { 1, 0 }, { -1, 1 }, { 1, -1 }, { -1, 0 }, { 0, -1 },
  { 2, 0 }, { 2, 1 }, { 0, 2 }, { 1, 2 },
};
static int rotOff[4][N_COUNT];
static int rotPitch = -1;
static int rot2[4][3];  // 2x output cells N1 N2 N3
static int rot4[4][8];  // 4x output cells N15 N14 N11 N3 N7 N10 N13 N12

// rotation used for the four corners: (x, y) -> (y, -x)
static void rotate(int r, double* x, double* y) {
  for(int i = 0; i < r; i++) { double t = *x; *x = *y; *y = -t; }
}

static void setupRotations(int pitch) {
  if(rotPitch == pitch) return;
  rotPitch = pitch;
  for(int r = 0; r < 4; r++) {
    for(int n = 0; n < N_COUNT; n++) {
      double x = baseOff[n][0], y = baseOff[n][1];
      rotate(r, &x, &y);
      rotOff[r][n] = (int)y * pitch + (int)x;
    }
    static const int c2[3][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 } };
    for(int k = 0; k < 3; k++) {
      double x = c2[k][0] - 0.5, y = c2[k][1] - 0.5;
      rotate(r, &x, &y);
      rot2[r][k] = (int)(y + 0.5) * 2 + (int)(x + 0.5);
    }
    static const int c4[8][2] = { { 3, 3 }, { 2, 3 }, { 3, 2 }, { 3, 0 }, { 3, 1 }, { 2, 2 }, { 1, 3 }, { 0, 3 } };
    for(int k = 0; k < 8; k++) {
      double x = c4[k][0] - 1.5, y = c4[k][1] - 1.5;
      rotate(r, &x, &y);
      rot4[r][k] = (int)(y + 1.5) * 4 + (int)(x + 1.5);
    }
  }
}

static void preparePadded(const uint32_t* src, int w, int h, int pitch) {
  int pw = w + 4, ph = h + 4;
  if(pw * ph > padCap) {
    free(padC);
    free(padY);
    padC = malloc(sizeof(uint32_t) * pw * ph);
    padY = malloc(sizeof(int) * pw * ph);
    padCap = pw * ph;
  }
  for(int y = 0; y < ph; y++) {
    int sy = y - 2;
    sy = sy < 0 ? 0 : (sy >= h ? h - 1 : sy);
    const uint32_t* row = src + (size_t)sy * pitch;
    uint32_t* pc = padC + y * pw;
    int* py = padY + y * pw;
    uint32_t last = 0xffffffff;
    int lastY = 0;
    for(int x = 0; x < pw; x++) {
      int sx = x - 2;
      sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
      uint32_t c = row[sx] & 0xffffff;
      pc[x] = c;
      if(c != last) { last = c; lastY = yuvOf(c); }
      py[x] = lastY;
    }
  }
  setupRotations(pw);
}

// evaluates one corner: 0 = nothing, 1 = left+up, 2 = left, 3 = up,
// 4 = diagonal, 5 = soft blend; *px receives the colour to blend towards
static inline int corner(int idx, int r, uint32_t* px) {
  const int* o = rotOff[r];
  uint32_t cE = padC[idx], cH = padC[idx + o[N_H]], cF = padC[idx + o[N_F]];
  if(cE == cH || cE == cF) return 0;
  int E = padY[idx], I = padY[idx + o[N_I]], H = padY[idx + o[N_H]], F = padY[idx + o[N_F]];
  int G = padY[idx + o[N_G]], C = padY[idx + o[N_C]], D = padY[idx + o[N_D]], B = padY[idx + o[N_B]];
  int F4 = padY[idx + o[N_F4]], I4 = padY[idx + o[N_I4]], H5 = padY[idx + o[N_H5]], I5 = padY[idx + o[N_I5]];
  int e = df(E, C) + df(E, G) + df(I, H5) + df(I, F4) + (df(H, F) << 2);
  int i = df(H, D) + df(H, I5) + df(F, I4) + df(F, B) + (df(E, I) << 2);
  *px = (df(E, F) <= df(E, H)) ? cF : cH;
  if((e < i) && ((!EQ(F, B) && !EQ(H, D)) || (EQ(E, I) && (!EQ(F, I4) && !EQ(H, I5))) || EQ(E, G) || EQ(E, C))) {
    int ke = df(F, G), ki = df(H, C);
    uint32_t cC = padC[idx + o[N_C]], cG = padC[idx + o[N_G]];
    bool ex2 = cE != cC && padC[idx + o[N_B]] != cC;
    bool ex3 = cE != cG && padC[idx + o[N_D]] != cG;
    bool left = ((ke << 1) <= ki) && ex3;
    bool up = (ke >= (ki << 1)) && ex2;
    if(left && up) return 1;
    if(left) return 2;
    if(up) return 3;
    return 4;
  }
  return (e <= i) ? 5 : 0;
}

static void xbr2x(const uint32_t* src, int w, int h, int pitch, uint32_t* dst) {
  preparePadded(src, w, h, pitch);
  int pw = w + 4, dw = w * 2;
  for(int y = 0; y < h; y++) {
    int idx = (y + 2) * pw + 2;
    uint32_t* d0 = dst + (size_t)(y * 2) * dw;
    for(int x = 0; x < w; x++, idx++) {
      uint32_t out[4];
      out[0] = out[1] = out[2] = out[3] = padC[idx];
      for(int r = 0; r < 4; r++) {
        uint32_t px;
        int t = corner(idx, r, &px);
        if(!t) continue;
        int n1 = rot2[r][0], n2 = rot2[r][1], n3 = rot2[r][2];
        switch(t) {
          case 1: out[n3] = mix(out[n3], px, 224); out[n2] = mix(out[n2], px, 64); out[n1] = out[n2]; break;
          case 2: out[n3] = mix(out[n3], px, 192); out[n2] = mix(out[n2], px, 64); break;
          case 3: out[n3] = mix(out[n3], px, 192); out[n1] = mix(out[n1], px, 64); break;
          case 4: out[n3] = mix(out[n3], px, 128); break;
          case 5: out[n3] = mix(out[n3], px, 64); break;
        }
      }
      d0[x * 2] = out[0]; d0[x * 2 + 1] = out[1];
      d0[dw + x * 2] = out[2]; d0[dw + x * 2 + 1] = out[3];
    }
  }
}

static void xbr4x(const uint32_t* src, int w, int h, int pitch, uint32_t* dst) {
  preparePadded(src, w, h, pitch);
  int pw = w + 4, dw = w * 4;
  for(int y = 0; y < h; y++) {
    int idx = (y + 2) * pw + 2;
    uint32_t* d0 = dst + (size_t)(y * 4) * dw;
    for(int x = 0; x < w; x++, idx++) {
      uint32_t e[16];
      uint32_t c = padC[idx];
      for(int k = 0; k < 16; k++) e[k] = c;
      for(int r = 0; r < 4; r++) {
        uint32_t px;
        int t = corner(idx, r, &px);
        if(!t) continue;
        const int* n = rot4[r]; // 0:N15 1:N14 2:N11 3:N3 4:N7 5:N10 6:N13 7:N12
        switch(t) {
          case 1: // shallow and steep
            e[n[6]] = mix(e[n[6]], px, 192);
            e[n[7]] = mix(e[n[7]], px, 64);
            e[n[0]] = e[n[1]] = e[n[2]] = px;
            e[n[5]] = e[n[3]] = e[n[7]];
            e[n[4]] = e[n[6]];
            break;
          case 2: // shallow edge
            e[n[0]] = e[n[1]] = px;
            e[n[2]] = mix(e[n[2]], px, 192);
            e[n[6]] = mix(e[n[6]], px, 192);
            e[n[5]] = mix(e[n[5]], px, 64);
            e[n[7]] = mix(e[n[7]], px, 64);
            break;
          case 3: // steep edge
            e[n[0]] = e[n[2]] = px;
            e[n[1]] = mix(e[n[1]], px, 192);
            e[n[4]] = mix(e[n[4]], px, 192);
            e[n[5]] = mix(e[n[5]], px, 64);
            e[n[3]] = mix(e[n[3]], px, 64);
            break;
          case 4: // diagonal
            e[n[2]] = mix(e[n[2]], px, 128);
            e[n[1]] = mix(e[n[1]], px, 128);
            e[n[0]] = px;
            break;
          case 5:
            e[n[0]] = mix(e[n[0]], px, 128);
            break;
        }
      }
      for(int row = 0; row < 4; row++) memcpy(d0 + (size_t)row * dw + x * 4, &e[row * 4], 16);
    }
  }
}

static uint32_t* tmpBuf;
static size_t tmpCap;

static uint32_t* tmp(size_t n) {
  if(n > tmpCap) {
    free(tmpBuf);
    tmpBuf = malloc(n * 4);
    tmpCap = n;
  }
  return tmpBuf;
}

void upscale_run(int filter, const uint32_t* src, int w, int h, int pitch, uint32_t* dst) {
  switch(filter) {
    case UPSCALE_SCALE2X: scale2x(src, w, h, pitch, dst); break;
    case UPSCALE_SCALE3X: scale3x(src, w, h, pitch, dst); break;
    case UPSCALE_SCALE4X: {
      uint32_t* t = tmp((size_t)w * h * 4);
      scale2x(src, w, h, pitch, t);
      scale2x(t, w * 2, h * 2, w * 2, dst);
      break;
    }
    case UPSCALE_XBR2X: xbr2x(src, w, h, pitch, dst); break;
    case UPSCALE_XBR4X: xbr4x(src, w, h, pitch, dst); break;
    default: upscale_nearest(src, w, h, pitch, dst, 1); break;
  }
}
