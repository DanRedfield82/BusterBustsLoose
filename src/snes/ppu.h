
#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Ppu Ppu;

// widescreen support: the renderer can draw extra columns on both sides of the
// normal 256 pixel wide picture. The output buffer is always PPU_BUF_W wide with
// the normal picture starting at column PPU_MAX_EXTRA.
#define PPU_MAX_EXTRA 128
#define PPU_BUF_W (256 + 2 * PPU_MAX_EXTRA)
#define PPU_BUF_H 239

#include "snes.h"
#include "statehandler.h"

typedef struct BgLayer {
  uint16_t hScroll;
  uint16_t vScroll;
  bool tilemapWider;
  bool tilemapHigher;
  uint16_t tilemapAdr;
  uint16_t tileAdr;
  bool bigTiles;
  bool mosaicEnabled;
} BgLayer;

typedef struct Layer {
  bool mainScreenEnabled;
  bool subScreenEnabled;
  bool mainScreenWindowed;
  bool subScreenWindowed;
} Layer;

typedef struct WindowLayer {
  bool window1enabled;
  bool window2enabled;
  bool window1inversed;
  bool window2inversed;
  uint8_t maskLogic;
} WindowLayer;

struct Ppu {
  Snes* snes;
  // vram access
  uint16_t vram[0x8000];
  uint16_t vramPointer;
  bool vramIncrementOnHigh;
  uint16_t vramIncrement;
  uint8_t vramRemapMode;
  uint16_t vramReadBuffer;
  // cgram access
  uint16_t cgram[0x100];
  uint8_t cgramPointer;
  bool cgramSecondWrite;
  uint8_t cgramBuffer;
  // oam access
  uint16_t oam[0x100];
  uint8_t highOam[0x20];
  // extension: 128 more sprites after the 128 in OAM (widescreen: the game
  // fills its table with the extra columns' sprites too); not in save states
  uint16_t extOam[0x100];
  uint8_t extHighOam[0x20];
  int extOamCount;
  // called for every word the game writes into VRAM through the data port
  void (*vramWriteHook)(void* ctx, uint16_t wordAdr);
  void* vramWriteCtx;
  // offset-per-tile (mode 2) values for the widescreen columns the game's
  // table doesn't reach (column <= 0 or > 32), vertical offsets only
  bool optExtValid;
  int optExtFirst, optExtCount;
  uint16_t optExtV[80];
  int16_t optDbg[80];   // debug: vertical offset used per column on line 150 (-1: none)
  uint8_t oamAdr;
  uint8_t oamAdrWritten;
  bool oamInHigh;
  bool oamInHighWritten;
  bool oamSecondWrite;
  uint8_t oamBuffer;
  // object/sprites
  bool objPriority;
  uint16_t objTileAdr1;
  uint16_t objTileAdr2;
  uint8_t objSize;
  uint8_t objPixelBuffer[PPU_BUF_W]; // line buffers (index = x + PPU_MAX_EXTRA)
  uint8_t objPriorityBuffer[PPU_BUF_W];
  bool timeOver;
  bool rangeOver;
  bool objInterlace;
  // background layers
  BgLayer bgLayer[4];
  uint8_t scrollPrev;
  uint8_t scrollPrev2;
  uint8_t mosaicSize;
  uint8_t mosaicStartLine;
  // layers
  Layer layer[5];
  // mode 7
  int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
  uint8_t m7prev;
  bool m7largeField;
  bool m7charFill;
  bool m7xFlip;
  bool m7yFlip;
  bool m7extBg;
  // mode 7 internal
  int32_t m7startX;
  int32_t m7startY;
  // windows
  WindowLayer windowLayer[6];
  uint8_t window1left;
  uint8_t window1right;
  uint8_t window2left;
  uint8_t window2right;
  // color math
  uint8_t clipMode;
  uint8_t preventMathMode;
  bool addSubscreen;
  bool subtractColor;
  bool halfColor;
  bool mathEnabled[6];
  uint8_t fixedColorR;
  uint8_t fixedColorG;
  uint8_t fixedColorB;
  // settings
  bool forcedBlank;
  uint8_t brightness;
  uint8_t mode;
  bool bg3priority;
  bool evenFrame;
  bool pseudoHires;
  bool overscan;
  bool frameOverscan; // if we are overscanning this frame (determined at 0,225)
  bool interlace;
  bool frameInterlace; // if we are interlacing this frame (determined at start vblank)
  bool directColor;
  // latching
  uint16_t hCount;
  uint16_t vCount;
  bool hCountSecond;
  bool vCountSecond;
  bool countersLatched;
  uint8_t ppu1openBus;
  uint8_t ppu2openBus;
  // pixel buffer (xrgb8888), PPU_BUF_W x PPU_BUF_H, line 0 = scanline 1
  uint32_t pixelBuffer[PPU_BUF_W * PPU_BUF_H];
  // widescreen / enhancement settings (not part of save states)
  // per-pixel render caches (kept per instance so several PPUs can run on different threads)
  int r_layerCache[4];
  uint16_t r_bgPixelBuf[4];
  uint8_t r_bgPrioBuf[4];
  bool r_bgWindowState[6]; // 0-3 (bg) 4 (spr) 5 (colorwind)
  uint32_t brightNow; // brightness multiplier (from bright_lut)
  // optional colour profile: maps a 15-bit BGR colour to xRGB8888 (applied
  // before the master brightness), NULL = plain 5 to 8 bit expansion
  const uint32_t* colorLut;
  int extraLeft;
  int extraRight;
  bool noSpriteLimit;
  // optional BG column remapping (HUD re-anchoring in widescreen). When
  // bgRemapLayer >= 0, lines with bgRemapLineActive[line-1] set sample that layer
  // at source column bgRemapSrc[line-1][x + PPU_MAX_EXTRA] (-1 = transparent).
  // playfield clip: outside [clipLeft, clipRight) BG1, BG2 and sprites are not
  // drawn (used to hide what lies beyond the level edges in widescreen)
  int clipLeft, clipRight;
  // per layer (BG1, BG2): outside [layerClipLeft, layerClipRight) the layer is
  // not drawn (what is behind it shows); for scenes whose parts beyond the
  // original 256 px must not be seen
  int layerClipLeft[2], layerClipRight[2];
  // BG3 left of game x bg3SplitX (x + viewShift[2]) from line bg3SplitTop on
  // (1-based, like the line records): drawn with horizontal scroll bg3SplitH
  // instead of its own, or not at all (bg3SplitHide); for a strip whose left
  // end the original never shows
  bool bg3Split, bg3SplitHide;
  int bg3SplitX, bg3SplitTop, bg3SplitH;
  // widescreen view shift: BG layers are drawn scrolled by viewShift[layer]
  // extra pixels (not on remapped HUD lines) and sprites by spriteShift, so the
  // picture can show more of the level near its edges than the game's camera does
  int viewShift[4];
  // BG1 vertical scroll of the camera, or -1: lines scrolled elsewhere show
  // it in the extra columns (see bbl.c)
  int bandRefVScroll;
  int spriteShift;
  // windows stay where the game put them on a 4:3 screen, whatever the view
  // shift, and the extra columns are outside them (football: the scoreboard's
  // dark backing and the pass box are windows)
  bool windowFixed;
  uint8_t debugLayerMask; // bit n set = hide layer n (0-3 bg, 4 obj), debugging aid
  // per-scanline scroll values of the last rendered frame (for frontend analysis)
  uint16_t lineHScroll[PPU_BUF_H][4];
  uint16_t lineVScroll[PPU_BUF_H][4];
  // writes to each BGn-HOFS so far, and per scanline: a line whose count differs
  // from the first line's was scrolled by a write in the middle of the frame
  uint8_t hScrollWrites[4];
  uint8_t lineHWrites[PPU_BUF_H][4];
  uint8_t lineMode[PPU_BUF_H];          // BG mode of each scanline
  uint16_t lineBg3Map[PPU_BUF_H];       // BG3 tilemap address of each scanline
  int bgRemapLayer;
  bool bgRemapLineActive[PPU_BUF_H];
  bool bgRemapLineContent[PPU_BUF_H];   // ...and the overlay shows something there
  bool bgRemapLineHud[PPU_BUF_H];       // ...and it is the HUD (re-anchored), not a fixed overlay
  int16_t bgRemapSrc[PPU_BUF_H][PPU_BUF_W];
  // optional per-sprite hook, lets the frontend move sprites (HUD re-anchoring).
  // Called during sprite evaluation with the oam index (0-127) and the decoded
  // position; returns the x position to draw at.
  int (*spriteXHook)(void* ctx, int index, int x, int y);
  void* spriteXHookCtx;
};

Ppu* ppu_init(Snes* snes);
void ppu_free(Ppu* ppu);
void ppu_reset(Ppu* ppu);
void ppu_handleState(Ppu* ppu, StateHandler* sh);
bool ppu_checkOverscan(Ppu* ppu);
void ppu_handleVblank(Ppu* ppu);
void ppu_handleFrameStart(Ppu* ppu);
void ppu_runLine(Ppu* ppu, int line);
uint8_t ppu_read(Ppu* ppu, uint8_t adr);
void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val);
void ppu_latchHV(Ppu* ppu);

#endif
