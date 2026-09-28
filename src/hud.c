// HUD re-anchoring for widescreen.
//
// Buster Busts Loose! draws its in-game HUD on BG3 (32x32 tilemap, no scroll)
// in the top few tile rows: life/dash on the left, lives/stars on the right.
// Empty HUD cells use tile $2300. In widescreen the BG3 tilemap would simply
// wrap around, repeating the HUD in the extra columns. Instead we split the
// HUD rows into horizontal segments of tiles and push segments on the left
// half to the left screen edge and segments on the right half to the right
// edge. Segments crossing the centre (e.g. the football scoreboard) stay put.
//
// Other BG3 rows that are "overlay" rows (mostly $2300, like the football
// play menu) are clipped to the original 256 columns. BG3 rows used as real
// backgrounds (the starfield in the space stages) keep wrapping normally.
#include <string.h>
#include <stdlib.h>
#include "hud.h"

#define HUD_BLANK 0x2300
#define HUD_LINES 48      // only the top of the screen is treated as HUD
#define GAP_MERGE 2       // blank columns tolerated inside one segment

static int16_t hudTab[PPU_BUF_W];
static int16_t clipTab[PPU_BUF_W];

void hud_init(Bbl* g) {
  (void)g;
  for(int i = 0; i < PPU_BUF_W; i++) {
    int x = i - PPU_MAX_EXTRA;
    clipTab[i] = (x >= 0 && x < 256) ? x : -1;
  }
}

bool hud_inGameplay(Bbl* g) {
  // $30 = main mode (3 = in a stage), $32 = stage sub mode (10 = playing)
  return bbl_peek16(g, 0x7E0030) == 3 && bbl_peek16(g, 0x7E0032) == 0x0A;
}

static bool isOverlayRow(const uint16_t* row) {
  int n = 0;
  for(int c = 0; c < 32; c++) n += row[c] == HUD_BLANK;
  return n >= 1; // background rows (space starfield) never use the HUD blank tile
}

void hud_update(Bbl* g) {
  Ppu* ppu = g->snes->ppu;
  int extra = ppu->extraLeft;
  memset(ppu->bgRemapLineActive, 0, sizeof(ppu->bgRemapLineActive));
  memset(ppu->bgRemapLineHud, 0, sizeof(ppu->bgRemapLineHud));
  ppu->bgRemapLayer = -1;
  if(extra <= 0 || !hud_inGameplay(g)) return;
  BgLayer* bg = &ppu->bgLayer[2];
  // (per scanline: 2-3's tilting bridge runs mode 2 below a mode 1 HUD, with
  // BG3's map switched to the offset table there)
  if(bg->bigTiles) return;
  bool anyMode1 = false;
  for(int l = 0; l < HUD_LINES && !anyMode1; l++) anyMode1 = ppu->lineMode[l] == 1;
  if(!anyMode1) return;
  // (a 64 tile wide map, the train stages: the remapped pixels read the first
  // 32 columns, i.e. the left 32x32 screen; on its fixed lines the right half
  // is never seen in 4:3 and holds leftovers, 2-5's text boxes showed them)
  bool wide = bg->tilemapWider;
  int lines = ppu->frameOverscan ? 239 : 224;

  // Some stages split BG3 mid-frame (fixed HUD on top, scrolling foreground
  // below), so work per scanline with the scroll values of the last frame.
  // Only lines where BG3 is not scrolled horizontally are touched.
  #define LINE_ROW(l) ((((l) + ppu->lineVScroll[(l) - 1][2]) >> 3) & (bg->tilemapHigher ? 63 : 31))
  #define LINE_FIXED(l) ((ppu->lineHScroll[(l) - 1][2] & 0x3ff) == 0 && ppu->lineMode[(l) - 1] == 1)
  #define ROW_PTR_L(l, r) (&ppu->vram[(ppu->lineBg3Map[(l) - 1] + ((r) & 31) * 32 + ((r) >= 32 ? (wide ? 0x800 : 0x400) : 0)) & 0x7fff])

  // (the foreground band, see further down)
  uint8_t fgBand = ppu->lineHWrites[199][2];
  int fgScroll = (int)(g->parRatio[2] * bbl_peek16(g, 0x7E1902) + 0.5f) & 1023;
  bool bg3Scrolls = g->parValid && !g->parIndep[2] && g->parRatio[2] > 0.25f && fgBand != ppu->lineHWrites[0][2] &&
                    (fgScroll < 8 || fgScroll > 1016);

  // Which tile rows of the HUD area are the HUD. A text box reaching up into
  // the HUD area (the end of 3-3: its top border is the row just under
  // DASH) must keep its top rows with the rest of it instead of them moving
  // to the screen edge with the HUD. A box is a frame of tiles $2369-$2372
  // (corners, edges, the speech tail) on a blank background: rows with those
  // tiles in them are not HUD rows.
  uint64_t notHud = 0;
  for(int line = 1; line <= HUD_LINES; line++) {
    if(!LINE_FIXED(line)) continue;
    const uint16_t* row = ROW_PTR_L(line, LINE_ROW(line));
    for(int c = 0; c < 32; c++) {
      uint16_t t = row[c] & 0xE3FF;
      if(t >= 0x2369 && t <= 0x2372) { notHud |= 1ull << LINE_ROW(line); break; }
    }
  }
  #define HUD_ROW(l) (!((notHud >> LINE_ROW(l)) & 1))

  // columns used by HUD rows in the HUD area
  bool used[32] = {0};
  bool any = false;
  for(int line = 1; line <= HUD_LINES; line++) {
    if(!LINE_FIXED(line) || !HUD_ROW(line)) continue;
    const uint16_t* row = ROW_PTR_L(line, LINE_ROW(line));
    if(!isOverlayRow(row)) continue;
    for(int c = 0; c < 32; c++) {
      if(row[c] != HUD_BLANK) { used[c] = true; any = true; }
    }
  }

  // segments -> per column shift -> screen x to source x table
  bool doHud = any && g->hudReanchor;
  if(doHud) {
    int16_t colShift[32] = {0};
    int c = 0;
    while(c < 32) {
      if(!used[c]) { c++; continue; }
      int start = c, end = c, gap = 0;
      for(int k = c + 1; k < 32; k++) {
        if(used[k]) { end = k; gap = 0; }
        else if(++gap > GAP_MERGE) break;
      }
      // by where most of the segment is: a life bar with five hearts
      // reaches column 16 but belongs to the left edge; only a panel around
      // the centre (the football scoreboard) stays put
      int shift = 0;
      int mid2 = start + end;               // twice the segment's centre
      if(end < 16 || mid2 < 26) shift = -extra;
      else if(start >= 16 || mid2 > 36) shift = extra;
      for(int k = start; k <= end; k++) colShift[k] = shift;
      c = end + 1;
    }
    for(int i = 0; i < PPU_BUF_W; i++) hudTab[i] = -1;
    for(int col = 0; col < 32; col++) {
      if(!used[col]) continue;
      for(int px = 0; px < 8; px++) {
        int sx = col * 8 + px;
        int dx = sx + colShift[col] + PPU_MAX_EXTRA;
        if(dx >= 0 && dx < PPU_BUF_W) hudTab[dx] = sx;
      }
    }
  }

  // (where BG3 is a foreground moving with the camera below a mid-frame split,
  // like the mansion's candles, the lines after the split are not a fixed
  // overlay even while their scroll is 0 at the very start of the area; they
  // pan with the widescreen view. The band above the split stays as it is.)
  // (the foreground band is the one line 200 is in: BG3's rate is measured
  // there. Its scroll is 0 as a foreground only where the camera puts it at 0,
  // like at the start of the mansion; elsewhere a line with scroll 0 in that
  // band is an overlay drawn over it, like 2-5's text boxes.)
  for(int line = 1; line <= lines; line++) {
    if(!LINE_FIXED(line)) continue;           // scrolling BG3: leave alone
    const uint16_t* row = ROW_PTR_L(line, LINE_ROW(line));
    if(!isOverlayRow(row)) continue;          // background row: wrap as usual
    bool hudLine = doHud && line <= HUD_LINES && HUD_ROW(line);
    if(!hudLine && bg3Scrolls && ppu->lineHWrites[line - 1][2] == fgBand) continue;
    const int16_t* tab = hudLine ? hudTab : clipTab;
    memcpy(ppu->bgRemapSrc[line - 1], tab, sizeof(hudTab));
    ppu->bgRemapLineActive[line - 1] = true;
    bool content = false;
    for(int c = 0; c < 32 && !content; c++) content = row[c] != HUD_BLANK;
    ppu->bgRemapLineContent[line - 1] = content;
    ppu->bgRemapLineHud[line - 1] = hudLine;
  }
  #undef HUD_ROW
  #undef LINE_ROW
  #undef LINE_FIXED
  #undef ROW_PTR_L
  ppu->bgRemapLayer = 2;
}
