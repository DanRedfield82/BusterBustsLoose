// bblprobe - headless developer tool for testing the port / reverse engineering.
// usage: bblprobe <rom> <script-file | -e "cmd; cmd; ...">
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "bbl.h"
#include "cheats.h"
#include "hud.h"
#include "filters.h"
#include "upscale.h"
#include "music.h"
#include "stb_image_write.h"

static Bbl* g;
static uint16_t held;
static uint32_t wlo = 1, whi = 0, rlo = 1, rhi = 0;
static int watchLimit = 200;
static FILE* traceFile;
static uint8_t* traceSeen;
static uint32_t lastPc;
static uint32_t keepAdr[16];
static uint8_t keepVal[16];
static int keepCount;
static uint32_t bpAdr[8];
static int bpCount, bpLimit = 100;
static const char* romPathArg;
#include <windows.h>
static uint64_t SDL_GetPerformanceCounterStub(void) {
  LARGE_INTEGER c, f;
  QueryPerformanceCounter(&c);
  QueryPerformanceFrequency(&f);
  return (uint64_t)(c.QuadPart * 1e9 / f.QuadPart);
}

static uint16_t parseButtons(const char* s) {
  uint16_t m = 0;
  char buf[128];
  strncpy(buf, s, 127); buf[127] = 0;
  for(char* t = strtok(buf, "+,"); t; t = strtok(NULL, "+,")) {
    if(!strcmp(t, "b")) m |= 1 << BTN_B;
    else if(!strcmp(t, "y")) m |= 1 << BTN_Y;
    else if(!strcmp(t, "select")) m |= 1 << BTN_SELECT;
    else if(!strcmp(t, "start")) m |= 1 << BTN_START;
    else if(!strcmp(t, "up")) m |= 1 << BTN_UP;
    else if(!strcmp(t, "down")) m |= 1 << BTN_DOWN;
    else if(!strcmp(t, "left")) m |= 1 << BTN_LEFT;
    else if(!strcmp(t, "right")) m |= 1 << BTN_RIGHT;
    else if(!strcmp(t, "a")) m |= 1 << BTN_A;
    else if(!strcmp(t, "x")) m |= 1 << BTN_X;
    else if(!strcmp(t, "l")) m |= 1 << BTN_L;
    else if(!strcmp(t, "r")) m |= 1 << BTN_R;
    else if(!strcmp(t, "none")) m = 0;
  }
  return m;
}

static void onWrite(Snes* s, uint32_t adr, uint8_t val) {
  uint32_t a = adr;
  uint8_t bank = adr >> 16;
  uint16_t lo = adr & 0xffff;
  if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && lo < 0x2000) a = 0x7e0000 | lo;
  else if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && lo < 0x8000) a = lo;
  if(a >= wlo && a <= whi && watchLimit > 0) {
    watchLimit--;
    printf("  W %06X = %02X  pc=%02X:%04X f=%u A=%04X X=%04X Y=%04X\n", a, val, s->cpu->k, lastPc & 0xffff, g->frameCount, s->cpu->a, s->cpu->x, s->cpu->y);
  }
}

static void onRead(Snes* s, uint32_t adr, uint8_t val) {
  uint32_t a = adr;
  uint8_t bank = adr >> 16;
  uint16_t lo = adr & 0xffff;
  if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && lo < 0x2000) a = 0x7e0000 | lo;
  else if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && lo < 0x8000) a = lo;
  if(a >= rlo && a <= rhi && watchLimit > 0) {
    watchLimit--;
    printf("  R %06X -> %02X  pc=%02X:%04X f=%u\n", a, val, s->cpu->k, lastPc & 0xffff, g->frameCount);
  }
}

// ring of the last executed instructions, dumped when a breakpoint hits
typedef struct { uint32_t pc; uint16_t a, x, y, sp; uint8_t db, m, xf; } Hist;
static Hist hist[256];
static int histPos, histDump;

static void onExec(Cpu* c) {
  lastPc = (c->k << 16) | c->pc;
  Hist* h = &hist[histPos++ & 255];
  h->pc = lastPc; h->a = c->a; h->x = c->x; h->y = c->y; h->sp = c->sp; h->db = c->db; h->m = c->mf; h->xf = c->xf;
  for(int i = 0; i < bpCount; i++) {
    if(lastPc == bpAdr[i] && bpLimit > 0) {
      bpLimit--;
      for(int k = histDump; k > 0; k--) {
        Hist* e = &hist[(histPos - 1 - k) & 255];
        printf("  h %06X A=%04X X=%04X Y=%04X S=%04X DB=%02X m=%d x=%d\n", e->pc, e->a, e->x, e->y, e->sp, e->db, e->m, e->xf);
      }
      printf("  stk: %02X %02X %02X %02X %02X %02X %02X %02X %02X", g->snes->ram[(c->sp+1)&0x1fff], g->snes->ram[(c->sp+2)&0x1fff], g->snes->ram[(c->sp+3)&0x1fff], g->snes->ram[(c->sp+4)&0x1fff], g->snes->ram[(c->sp+5)&0x1fff], g->snes->ram[(c->sp+6)&0x1fff], g->snes->ram[(c->sp+7)&0x1fff], g->snes->ram[(c->sp+8)&0x1fff], g->snes->ram[(c->sp+9)&0x1fff]);
      printf("  BP %06X f=%u A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X m=%d x=%d\n", lastPc, g->frameCount, c->a, c->x, c->y, c->sp, c->dp, c->db, c->mf, c->xf);
    }
  }
  if(traceFile) {
    uint32_t idx = lastPc & 0xffffff;
    if(!(traceSeen[idx >> 3] & (1 << (idx & 7)))) {
      traceSeen[idx >> 3] |= 1 << (idx & 7);
      fprintf(traceFile, "%06X f=%u m=%d x=%d A=%04X X=%04X Y=%04X D=%04X DB=%02X\n", idx, g->frameCount, c->mf, c->xf, c->a, c->x, c->y, c->dp, c->db);
    }
  }
}

static void shot(const char* path) {
  int w = bbl_width(g), h = bbl_height(g);
  const uint32_t* px = bbl_pixels(g);
  uint8_t* out = malloc(w * h * 3);
  for(int y = 0; y < h; y++) {
    for(int x = 0; x < w; x++) {
      uint32_t c = px[y * PPU_BUF_W + PPU_MAX_EXTRA - g->extraCols + x];
      out[(y * w + x) * 3 + 0] = c >> 16;
      out[(y * w + x) * 3 + 1] = c >> 8;
      out[(y * w + x) * 3 + 2] = c;
    }
  }
  stbi_write_png(path, w, h, 3, out, w * 3);
  free(out);
  printf("shot %s (%dx%d) frame %u\n", path, w, h, g->frameCount);
}

// rec <path>: every frame from then on goes to <path>.vid (per frame: uint16
// width, height, then RGB rows of the visible picture) and <path>.pcm (48 kHz
// stereo s16, frame-locked) until recstop (trailer footage)
static FILE* recVid;
static FILE* recPcm;
static double recFrac;
static int recFilter;   // rec <path> <filter>: the picture through that upscale filter

static void recFrame(void) {
  if(!recVid) return;
  int w = bbl_width(g), h = bbl_height(g);
  const uint32_t* px = bbl_pixels(g) + PPU_MAX_EXTRA - g->extraCols;
  int pitch = PPU_BUF_W;
  static uint32_t* up;
  int k = recFilter > 0 ? upscale_factor(recFilter) : 1;
  if(k > 1) {
    if(!up) up = malloc((size_t)PPU_BUF_W * PPU_BUF_H * 16 * 4);
    upscale_run(recFilter, px, w, h, PPU_BUF_W, up);
    px = up; w *= k; h *= k; pitch = w;
  }
  uint16_t hdr[2] = { (uint16_t)w, (uint16_t)h };
  fwrite(hdr, 2, 2, recVid);
  static uint8_t row[PPU_BUF_W * 4 * 3];
  for(int y = 0; y < h; y++) {
    const uint32_t* src = px + (size_t)y * pitch;
    for(int x = 0; x < w; x++) {
      uint32_t c = src[x];
      row[x * 3] = (uint8_t)(c >> 16); row[x * 3 + 1] = (uint8_t)(c >> 8); row[x * 3 + 2] = (uint8_t)c;
    }
    fwrite(row, 3, w, recVid);
  }
  double want = 48000.0 / 60.0988 + recFrac;
  int n = (int)want;
  recFrac = want - n;
  static int16_t buf[4096 * 2];
  bbl_getSamples(g, buf, n);
  fwrite(buf, 4, n, recPcm);
}

static void runFrames(int n) {
  for(int i = 0; i < n; i++) {
    bbl_setButtons(g, 1, held);
    for(int k = 0; k < keepCount; k++) bbl_poke(g, keepAdr[k], keepVal[k]);
    cheats_preFrame(g);
    bbl_runFrame(g);
    cheats_postFrame(g);
    recFrame();
  }
}

static void doCmd(char* line) {
  while(isspace((unsigned char)*line)) line++;
  if(!*line || *line == '#') return;
  char cmd[64] = {0}, a1[256] = {0}, a2[256] = {0}, a3[256] = {0}, a4[256] = {0}, a5[256] = {0};
  sscanf(line, "%63s %255s %255s %255s %255s %255s", cmd, a1, a2, a3, a4, a5);
  if(!strcmp(cmd, "frames")) runFrames(atoi(a1));
  else if(!strcmp(cmd, "rec")) {
    char pv[300], pa[300];
    snprintf(pv, sizeof(pv), "%s.vid", a1); snprintf(pa, sizeof(pa), "%s.pcm", a1);
    if(recVid) { fclose(recVid); fclose(recPcm); }
    recVid = fopen(pv, "wb"); recPcm = fopen(pa, "wb"); recFrac = 0; recFilter = atoi(a2);
    if(!recVid || !recPcm) printf("rec: cannot write %s\n", a1);
  }
  else if(!strcmp(cmd, "recstop")) { if(recVid) { fclose(recVid); fclose(recPcm); } recVid = recPcm = NULL; }
  else if(!strcmp(cmd, "hold")) held = parseButtons(a1);
  else if(!strcmp(cmd, "release")) held = 0;
  else if(!strcmp(cmd, "tap")) {
    int n = a2[0] ? atoi(a2) : 4;
    held = parseButtons(a1); runFrames(n); held = 0; runFrames(n);
  }
  else if(!strcmp(cmd, "shot")) shot(a1);
  else if(!strcmp(cmd, "bgstate")) {
    // same boot script as src/bgtask.c
    bbl_reset(g, true);
    for(int f = 0; f < 4000; f++) {
      bbl_poke(g, 0x7E1D7E, 1);
      bool selectMode = bbl_peek16(g, 0x7E0030) == 3 && bbl_peek16(g, 0x7E0032) == 7;
      bbl_setButtons(g, 1, (!selectMode && (f % 20) < 10) ? (1 << BTN_START) : 0);
      bbl_runFrame(g);
      if(bbl_peek16(g, 0x7E0030) == 3 && bbl_peek16(g, 0x7E0032) == 7 && bbl_peek16(g, 0x7E19F0) == 1) {
        bbl_setButtons(g, 1, 0);
        for(int k = 0; k < 30; k++) bbl_runFrame(g);
        printf("bgstate at frame %d: 19F0=%d 32=%d\n", f, bbl_peek16(g, 0x7E19F0), bbl_peek16(g, 0x7E0032));
        break;
      }
    }
  }
  else if(!strcmp(cmd, "scrolls")) {
    // prints BG1-3 horizontal scroll (BG3 sampled at line 200) and the BG1/BG2 cameras
    Ppu* pp = g->snes->ppu;
    printf("scroll h1=%d h2=%d h3=%d cam1=%d cam2=%d cam3=%d w=%d shift=%d r2=%.2f r3=%.2f clip=%d..%d vs=%d,%d,%d\n", pp->lineHScroll[100][0], pp->lineHScroll[100][1], pp->lineHScroll[199][2],
      bbl_peek16(g, 0x7E1902), bbl_peek16(g, 0x7E1942), bbl_peek16(g, 0x7E1982), bbl_peek(g, 0x7E1925), g->viewShift, g->parRatio[1], g->parRatio[2], pp->clipLeft, pp->clipRight, pp->viewShift[0], pp->viewShift[1], pp->viewShift[2]);
  }
  else if(!strcmp(cmd, "bench")) {
    // bench <frames>: emulation speed; then each upscale filter
    int n = atoi(a1);
    uint64_t t0 = SDL_GetPerformanceCounterStub();
    runFrames(n);
    uint64_t t1 = SDL_GetPerformanceCounterStub();
    printf("emulation: %.2f ms/frame\n",(t1 - t0) / 1e6 / n);
    int w = bbl_width(g), h = bbl_height(g);
    uint32_t* dst = malloc((size_t)w * h * 16 * 4);
    for(int f = 1; f < UPSCALE_COUNT; f++) {
      t0 = SDL_GetPerformanceCounterStub();
      for(int i = 0; i < 30; i++) upscale_run(f, bbl_pixels(g) + PPU_MAX_EXTRA - g->extraCols, w, h, PPU_BUF_W, dst);
      t1 = SDL_GetPerformanceCounterStub();
      printf("%s: %.2f ms\n",upscaleNames[f], (t1 - t0) / 1e6 / 30);
    }
    free(dst);
  }
  else if(!strcmp(cmd, "upshot")) {
    // upshot <filter> <file>
    int f = atoi(a1), k = upscale_factor(f);
    int w = bbl_width(g), h = bbl_height(g);
    uint32_t* dst = malloc((size_t)w * h * k * k * 4);
    upscale_run(f, bbl_pixels(g) + PPU_MAX_EXTRA - g->extraCols, w, h, PPU_BUF_W, dst);
    uint8_t* out = malloc((size_t)w * h * k * k * 3);
    for(int i = 0; i < w * h * k * k; i++) { out[i * 3] = dst[i] >> 16; out[i * 3 + 1] = dst[i] >> 8; out[i * 3 + 2] = dst[i]; }
    stbi_write_png(a2, w * k, h * k, 3, out, w * k * 3);
    free(out); free(dst);
  }
  else if(!strcmp(cmd, "ws")) bbl_setWidescreen(g, atoi(a1));
  else if(!strcmp(cmd, "hud")) g->hudReanchor = atoi(a1) != 0;
  else if(!strcmp(cmd, "colour")) filters_apply(g->snes->ppu, atoi(a1), 100, 100, 100, 100);
  else if(!strcmp(cmd, "layers")) g->snes->ppu->debugLayerMask = strtoul(a1, 0, 16);
  else if(!strcmp(cmd, "nolimit")) g->noSpriteLimit = atoi(a1) != 0;
  else if(!strcmp(cmd, "poke")) bbl_poke(g, strtoul(a1, 0, 16), strtoul(a2, 0, 16));
  else if(!strcmp(cmd, "keep") && keepCount < 16) { keepAdr[keepCount] = strtoul(a1, 0, 16); keepVal[keepCount++] = strtoul(a2, 0, 16); }
  else if(!strcmp(cmd, "unkeep")) keepCount = 0;
  else if(!strcmp(cmd, "romw")) bbl_romPatch(g, strtoul(a1, 0, 16), strtoul(a2, 0, 16));
  else if(!strcmp(cmd, "cheat")) cheats_set(g, atoi(a1), atoi(a2));
  else if(!strcmp(cmd, "peek")) {
    uint32_t adr = strtoul(a1, 0, 16);
    int n = a2[0] ? strtoul(a2, 0, 16) : 16;
    for(int i = 0; i < n; i++) {
      if(i % 16 == 0) printf("%s%06X:", i ? "\n" : "", adr + i);
      printf(" %02X", bbl_peek(g, adr + i));
    }
    printf("\n");
  }
  else if(!strcmp(cmd, "cell")) {
    uint16_t v, m; uint32_t o;
    int k = bbl_ringCellInfo(g, atoi(a1), atoi(a2), atoi(a3), &v, &m, &o);
    printf("cell L%s %s,%s kind=%d vram=%04X model=%04X owner=%d,%d\n", a1, a2, a3, k, v, m, (int16_t)(o >> 16), (int16_t)(o & 0xffff));
  }
  else if(!strcmp(cmd, "ramsig")) {
    // hash of WRAM without what legitimately differs in widescreen: scratch
    // ($00-$1F), sprite count and OAM buffer, stack, VRAM transfer queue and
    // column buffers, the collision ring ($7F:7000)
    static const uint32_t skip[][2] = { { 0x0000, 0x001F }, { 0x004A, 0x004B }, { 0x0098, 0x0099 }, { 0x00BC, 0x00BD },
      { 0x01E0, 0x03FF }, { 0x1E00, 0x1FFF }, { 0xE600, 0xEFFF }, { 0xF500, 0xF5FF }, { 0x17000, 0x17FFF } };
    uint32_t h = 2166136261u;
    for(uint32_t a = 0; a < 0x20000; a++) {
      bool sk = false;
      for(int i = 0; i < (int)(sizeof(skip) / sizeof(skip[0])); i++) if(a >= skip[i][0] && a <= skip[i][1]) { sk = true; break; }
      if(sk) continue;
      h = (h ^ g->snes->ram[a]) * 16777619u;
    }
    printf("ramsig %08X\n", h);
  }
  else if(!strcmp(cmd, "linescroll")) {
    // runs of lines with the same scroll values for one layer (0-2)
    int l = atoi(a1), start = 0;
    Ppu* pp = g->snes->ppu;
    for(int y = 1; y <= 224; y++) {
      if(y == 224 || pp->lineHScroll[y][l] != pp->lineHScroll[start][l] || pp->lineVScroll[y][l] != pp->lineVScroll[start][l] ||
         pp->bgRemapLineActive[y] != pp->bgRemapLineActive[start] || pp->lineHWrites[y][l] != pp->lineHWrites[start][l]) {
        printf("  lines %3d-%3d h=%d v=%d remap=%d writes=%d\n", start + 1, y, pp->lineHScroll[start][l], pp->lineVScroll[start][l],
               pp->bgRemapLineActive[start], pp->lineHWrites[start][l]);
        start = y;
      }
    }
  }
  else if(!strcmp(cmd, "ringdebug")) { extern int bbl_ringDebug; bbl_ringDebug = atoi(a1); }
  else if(!strcmp(cmd, "viewfix")) { printf("viewfix %d\n", bbl_viewFix(g, atoi(a1))); }
  else if(!strcmp(cmd, "viewcheck")) {
    for(int l = 0; l < 2; l++) { int n; int bad = bbl_viewVerify(g, l, &n, atoi(a1)); printf("viewcheck L%d checked=%d bad=%d\n", l, n, bad); }
  }
  else if(!strcmp(cmd, "noring")) { extern int bbl_noRing; bbl_noRing = atoi(a1); }
  else if(!strcmp(cmd, "boost")) g->cpuBoost = atoi(a1);
  else if(!strcmp(cmd, "objext")) { unsigned sl = strtoul(a1, 0, 16) & 0x1fff; printf("objext %04X ext=%u frame=%u wide=%d\n", sl, g->objExtFrame[sl >> 1], g->frameCount, g->objWide); }
  else if(!strcmp(cmd, "optdbg")) { for(int i = 0; i < 80; i++) printf("%d ", g->snes->ppu->optDbg[i]); printf("\n"); for(int i = 0; i < 80; i++) g->snes->ppu->optDbg[i] = -9; }
  else if(!strcmp(cmd, "optforce")) { extern int bbl_optForce; bbl_optForce = (int)strtol(a1, 0, 0); }
  else if(!strcmp(cmd, "optdelay")) { extern int bbl_optDelay; bbl_optDelay = atoi(a1); }
  else if(!strcmp(cmd, "objdebug")) { extern int bbl_objDebug; bbl_objDebug = atoi(a1); }
  else if(!strcmp(cmd, "vramlog")) { extern int bbl_vramLog; bbl_vramLog = atoi(a1); }
  else if(!strcmp(cmd, "vshift")) {
    Ppu* pp = g->snes->ppu;
    printf("vshift bg1=%d bg2=%d bg3=%d spr=%d parRatio=%.2f/%.2f parIndep=%d/%d\n", pp->viewShift[0], pp->viewShift[1], pp->viewShift[2],
           pp->spriteShift, g->parRatio[1], g->parRatio[2], g->parIndep[1], g->parIndep[2]);
  }
  else if(!strcmp(cmd, "objall")) { extern int bbl_objAll; bbl_objAll = atoi(a1); }
  else if(!strcmp(cmd, "noplay")) { extern int bbl_noPlay; bbl_noPlay = atoi(a1); }
  else if(!strcmp(cmd, "playfull")) { extern int bbl_playFull; bbl_playFull = atoi(a1); }
  else if(!strcmp(cmd, "netprio")) { extern int bbl_netPrio; bbl_netPrio = atoi(a1); }
  else if(!strcmp(cmd, "noobj")) { extern int bbl_noObj; bbl_noObj = atoi(a1); }
  else if(!strcmp(cmd, "vpoke")) g->snes->ppu->vram[strtoul(a1, 0, 16) & 0x7fff] = (uint16_t)strtoul(a2, 0, 16);   // test aid: VRAM word
  else if(!strcmp(cmd, "ringown")) {
    // test aid: record ring cell (a2, a3) of layer a1 as holding world cell (a4, a5); no post-load adopt
    int l = atoi(a1), cx = atoi(a2), cy = atoi(a3), ox = atoi(a4), oy = atoi(a5);
    int ri = ((cy >> 5) & 1) * 0x800 + ((cx >> 5) & 1) * 0x400 + (cy & 31) * 32 + (cx & 31);
    g->ringOwner[l][ri] = ((uint32_t)(ox & 0xFFFF) << 16) | (uint32_t)(oy & 0xFFFF);
    g->ringAdopt[l] = false;
  }
  else if(!strcmp(cmd, "ringcheck")) {
    for(int l = 0; l < 2; l++) { int n; int bad = bbl_ringVerify(g, l, &n); printf("ringcheck L%d checked=%d bad=%d\n", l, n, bad); }
  }
  else if(!strcmp(cmd, "objstat")) printf("objstat early=%u evicted=%u wide=%d lo=%d hi=%d repaired=%u sweeptimeouts=%u play=%d/%d %d..%d takeovers=%u\n", g->objEarly, g->objEvicted, g->objWide, g->objLo, g->objHi, g->ringRepaired, g->sweepTimeouts, g->playAuto, g->playWide, g->playLo, g->playHi, g->objTakeovers);
  else if(!strcmp(cmd, "st")) {
    // status line: frame mode sub state stage area camx camy lives hearts pc
    printf("st %u %d %d %d %d %d %d %d %d %d %06X\n", g->frameCount, bbl_peek16(g, 0x7E0030), bbl_peek16(g, 0x7E0032),
           bbl_peek16(g, 0x7E0080), bbl_peek16(g, 0x7E0068), bbl_peek16(g, 0x7E0094), bbl_peek16(g, 0x7E1902),
           bbl_peek16(g, 0x7E1912), bbl_peek(g, 0x7E008E), bbl_peek(g, 0x7E009E), (g->snes->cpu->k << 16) | g->snes->cpu->pc);
  }
  else if(!strcmp(cmd, "ram")) bbl_writeFile(a1, g->snes->ram, 0x20000);
  else if(!strcmp(cmd, "vram")) bbl_writeFile(a1, g->snes->ppu->vram, 0x10000);
  else if(!strcmp(cmd, "cgram")) bbl_writeFile(a1, g->snes->ppu->cgram, 0x200);
  else if(!strcmp(cmd, "aram")) bbl_writeFile(a1, g->snes->apu->ram, 0x10000);
  else if(!strcmp(cmd, "save")) {
    uint8_t* d; int n = bbl_saveState(g, &d); bbl_writeFile(a1, d, n); free(d);
  }
  else if(!strcmp(cmd, "load")) {
    uint8_t* d; int n;
    if(bbl_readFile(a1, &d, &n)) { printf("load %s: %d\n", a1, bbl_loadState(g, d, n)); free(d); }
    else printf("load failed %s\n", a1);
  }
  else if(!strcmp(cmd, "watchw")) {
    wlo = strtoul(a1, 0, 16); whi = a2[0] ? strtoul(a2, 0, 16) : wlo; watchLimit = a3[0] ? atoi(a3) : 200;
  }
  else if(!strcmp(cmd, "watchr")) {
    rlo = strtoul(a1, 0, 16); rhi = a2[0] ? strtoul(a2, 0, 16) : rlo; watchLimit = a3[0] ? atoi(a3) : 200;
  }
  else if(!strcmp(cmd, "bp") && bpCount < 8) { bpAdr[bpCount++] = strtoul(a1, 0, 16); bpLimit = a2[0] ? atoi(a2) : 100; }
  else if(!strcmp(cmd, "unbp")) bpCount = 0;
  else if(!strcmp(cmd, "hist")) histDump = atoi(a1) > 255 ? 255 : atoi(a1);
  else if(!strcmp(cmd, "unwatch")) { wlo = rlo = 1; whi = rhi = 0; }
  else if(!strcmp(cmd, "trace")) {
    if(traceFile) fclose(traceFile);
    traceFile = NULL;
    if(a1[0] && strcmp(a1, "off")) {
      traceFile = fopen(a1, "w");
      if(!traceSeen) traceSeen = calloc(1, 0x200000);
    }
  }
  else if(!strcmp(cmd, "traceclear")) { if(traceSeen) memset(traceSeen, 0, 0x200000); }
  else if(!strcmp(cmd, "oam")) {
    Ppu* p = g->snes->ppu;
    for(int i = 0; i < 128; i++) {
      int x = p->oam[i * 2] & 0xff, y = p->oam[i * 2] >> 8;
      int hi = (p->highOam[i >> 2] >> ((i & 3) * 2)) & 3;
      x |= (hi & 1) << 8;
      int t = p->oam[i * 2 + 1];
      if(y >= 224 && y < 240 && !a1[0]) continue;
      printf("  %3d x=%3d y=%3d tile=%03X pal=%d prio=%d flip=%d%d big=%d\n", i, x, y, t & 0x1ff, (t >> 9) & 7, (t >> 12) & 3, (t >> 14) & 1, (t >> 15) & 1, hi >> 1);
    }
  }
  else if(!strcmp(cmd, "ppu")) {
    Ppu* p = g->snes->ppu;
    printf("mode=%d bg3prio=%d main=%d%d%d%d%d sub=%d%d%d%d%d objsize=%d objadr=%04X/%04X\n", p->mode, p->bg3priority,
      p->layer[0].mainScreenEnabled, p->layer[1].mainScreenEnabled, p->layer[2].mainScreenEnabled, p->layer[3].mainScreenEnabled, p->layer[4].mainScreenEnabled,
      p->layer[0].subScreenEnabled, p->layer[1].subScreenEnabled, p->layer[2].subScreenEnabled, p->layer[3].subScreenEnabled, p->layer[4].subScreenEnabled,
      p->objSize, p->objTileAdr1, p->objTileAdr2);
    for(int i = 0; i < 4; i++) {
      BgLayer* b = &p->bgLayer[i];
      printf(" bg%d h=%3d v=%3d map=%04X %s%s tiles=%04X big=%d win=%d%d\n", i + 1, b->hScroll, b->vScroll, b->tilemapAdr,
        b->tilemapWider ? "W" : "-", b->tilemapHigher ? "H" : "-", b->tileAdr, b->bigTiles, p->layer[i].mainScreenWindowed, p->windowLayer[i].window1enabled);
    }
    printf(" win1 %d-%d win2 %d-%d  cmath add=%d sub=%d half=%d en=%d%d%d%d%d%d\n", p->window1left, p->window1right, p->window2left, p->window2right,
      p->addSubscreen, p->subtractColor, p->halfColor, p->mathEnabled[0], p->mathEnabled[1], p->mathEnabled[2], p->mathEnabled[3], p->mathEnabled[4], p->mathEnabled[5]);
  }
  else if(!strcmp(cmd, "reset")) bbl_reset(g, true);
  else if(!strcmp(cmd, "echo")) printf("%s\n", line + 5);
  else if(!strcmp(cmd, "apuports")) {
    printf("in %02X %02X %02X %02X out %02X %02X %02X %02X\n", g->snes->apu->inPorts[0], g->snes->apu->inPorts[1], g->snes->apu->inPorts[2], g->snes->apu->inPorts[3],
      g->snes->apu->outPorts[0], g->snes->apu->outPorts[1], g->snes->apu->outPorts[2], g->snes->apu->outPorts[3]);
  }
  else if(!strcmp(cmd, "gloud")) {
    int n = atoi(a1);
    int16_t buf[800 * 2];
    double acc = 0;
    printf("  game loudness/s:");
    for(int i = 0; i < n; i++) {
      runFrames(1);
      bbl_getSamples(g, buf, 800);
      for(int k = 0; k < 1600; k++) acc += (double)buf[k] * buf[k];
      if(i % 60 == 59) { printf(" %d", (int)(acc > 0 ? 10 * log10(acc / 96000.0) : 0)); acc = 0; }
    }
    printf("  song=%02X\n", bbl_peek(g, 0x7E1E24));
  }
  else if(!strcmp(cmd, "callr")) printf("call: %d\n", bbl_callRoutine(g, strtoul(a1, 0, 16), strtoul(a2, 0, 16)));
  else if(!strcmp(cmd, "mopen")) {
    uint8_t* d; int n;
    if(bbl_readFile(a1, &d, &n)) { printf("music open: %d\n", music_open(romPathArg, d, n)); free(d); }
  }
  else if(!strcmp(cmd, "mplay")) music_play(atoi(a1));
  else if(!strcmp(cmd, "msfx")) music_sfx(strtoul(a1, 0, 16));
  else if(!strcmp(cmd, "mstop")) music_stop();
  else if(!strcmp(cmd, "mrec")) {
    // mrec <file.wav> <frames>: record the music instance, print loudness per second
    int n = atoi(a2);
    FILE* f = fopen(a1, "wb");
    int total = n * 800 * 4;
    uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,0x80,0xbb,0,0,0,0xee,2,0,4,0,16,0,'d','a','t','a'};
    *(uint32_t*)(hdr + 4) = 36 + total;
    *(uint32_t*)(hdr + 40) = total;
    fwrite(hdr, 1, 44, f);
    int16_t buf[800 * 2];
    double acc = 0; int lastLoud = -1;
    printf("  loudness/s:");
    for(int i = 0; i < n; i++) {
      music_frame(buf, 800);
      fwrite(buf, 1, sizeof(buf), f);
      for(int k = 0; k < 1600; k++) acc += (double)buf[k] * buf[k];
      if(i % 60 == 59) { int db = (int)(acc > 0 ? 10 * log10(acc / 96000.0) : 0); printf(" %d", db); if(db > 30) lastLoud = i / 60; acc = 0; }
    }
    printf("  (state=%d, last loud second %d)\n", music_state(), lastLoud);
    fclose(f);
  }
  else if(!strcmp(cmd, "wav")) {
    // record n frames of audio to a wav file
    int n = atoi(a2);
    FILE* f = fopen(a1, "wb");
    int total = n * 534 * 2 * 2;
    uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,0x80,0xbb,0,0,0,0xee,2,0,4,0,16,0,'d','a','t','a'};
    *(uint32_t*)(hdr + 4) = 36 + total;
    *(uint32_t*)(hdr + 40) = total;
    fwrite(hdr, 1, 44, f);
    int16_t buf[534 * 2];
    for(int i = 0; i < n; i++) {
      runFrames(1);
      bbl_getSamples(g, buf, 534);
      fwrite(buf, 1, sizeof(buf), f);
    }
    fclose(f);
  }
  else printf("unknown command: %s\n", cmd);
}

int main(int argc, char** argv) {
  if(argc < 3) {
    printf("usage: bblprobe <rom> <script | -e \"cmds\">\n");
    return 1;
  }
  char err[512];
  romPathArg = argv[1];
  g = bbl_create(argv[1], err, sizeof(err));
  if(!g) { printf("%s\n", err); return 1; }
  cheats_init(g);
  hud_init(g);
  snes_dbgWrite = onWrite;
  snes_dbgRead = onRead;
  cpu_dbgExec = onExec;
  if(!strcmp(argv[2], "-e")) {
    for(int i = 3; i < argc; i++) {
      char* s = argv[i];
      while(s && *s) {
        char* next = strchr(s, ';');
        if(next) *next++ = 0;
        doCmd(s);
        s = next;
      }
    }
  } else if(!strcmp(argv[2], "-")) {
    // interactive: one command per line on stdin, "sync" marks the end of a reply
    char line[1024];
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    while(fgets(line, sizeof(line), stdin)) {
      line[strcspn(line, "\r\n")] = 0;
      doCmd(line);
      printf("@sync\n");
      fflush(stdout);
    }
  } else {
    FILE* f = fopen(argv[2], "r");
    if(!f) { printf("cannot open %s\n", argv[2]); return 1; }
    char line[1024];
    while(fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n")] = 0;
      doCmd(line);
    }
    fclose(f);
  }
  if(traceFile) fclose(traceFile);
  bbl_destroy(g);
  return 0;
}
