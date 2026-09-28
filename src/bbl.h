// Buster Busts Loose! PC port - game wrapper around the SNES core
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "snes.h"

#define BBL_ROM_SIZE 0x100000
#define BBL_ROM_SHA1 "04bf92b242ba965c453b6eb13d522afb7db7996b"
#define BBL_ROM_CRC32 0xab48d27au

// SNES joypad bits as used by snes_setButtonState
enum {
  BTN_B = 0, BTN_Y, BTN_SELECT, BTN_START, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
  BTN_A, BTN_X, BTN_L, BTN_R, BTN_COUNT
};

typedef struct ObjPend ObjPend;

typedef struct Bbl {
  Snes* snes;
  uint8_t* rom;         // pristine copy of the ROM image
  int romSize;
  int extraCols;        // widescreen: extra pixels drawn on each side (0 = 4:3)
  bool hudReanchor;     // move HUD elements to the edges in widescreen
  bool noSpriteLimit;
  int cpuBoost;         // percent more CPU speed during widescreen gameplay
  uint32_t frameCount;
  // widescreen view shift (see bbl.c)
  int viewShift;
  int sprExtra, sprShift;           // sprite window currently patched into the ROM
  int sprLeft, sprRight;            //   and the X range it accepts
  uint16_t netSlot;                 // 2-3's mail net's slot this frame (autoRight), 0: none
  bool sprNet;                      // the pieces being written are the net's
  bool netGhost, netPrevValid;      // the net just gone round, shown going on to the left (netUpdate)
  bool ghostOn;                     // 2-3's collapse: the next car's enemy drawn ahead of its spawn (banditGhostUpdate)
  int ghostX, ghostY;
  uint32_t ghostSpawn, ghostSeen;   // (frame + 1)
  bool ghostPending;
  uint16_t ghostSlot;
  int netGhostX, netPrevX, netGhostY, netPrevY;
  bool sprNetGhostY;                // the net's pieces being drawn go to its ghost, at the ghost's height
  int parKey;                       // area the parallax estimate belongs to
  int parLast[3];                   // last camera x of BG1..BG3
  int parLastReg1;                  // last BG1 scroll register (pairs with BG3)
  int parSumReg1;
  int parSum[3];                    // accumulated camera movement per layer
  float parRatio[3];                // BG2/BG3 movement relative to BG1
  bool parIndep[3];                 // layer scrolls on its own (autoscroll)
  bool parValid;
  int parWarmup;                    // frames to wait before measuring (level load)
  int loadedFrames;                 // frames since a state load without line data
  int streamLeft, streamRight;      // columns streamed around the camera (widescreen)
  int stream2Left, stream2Right;    // the same for BG2 (its view shift differs)
  int extLeft[2], extRight[2];      // last level extent of BG1 / BG2 in this area
  int shownShift[2], shownCam;      // view shift actually shown (eased), camera
  int shownCamY;
  uint32_t camJumpFrame;            // frame + 1 of the last camera jump
  int jumpFromCam;                  // ...and where the camera was before it
  uint32_t shakeFrame;              // frame + 1 of the last screen-shake camera write (CAM_SHAKE)
  int shakeRest;                    // ...and the camera from before the shake
  bool gfxWide;                     // widescreen: the sprite set trigger may swap sooner / later, what lost its set is hidden (applyGfxTrigger)
  int gfxRight, gfxPicLo, gfxPicHi; // ...by up to this much; the picture's screen x range
  uint32_t drawnFrame;              // frame + 1 of drawnX: screen x of the objects drawn from the second sprite table (not Buster)
  int drawnN, drawnPrevN;           // ...and of the frame before (drawnPrevX)
  int16_t drawnX[96], drawnPrevX[96];
  uint16_t drawnSlot[96], drawnPrevSlot[96];
  uint8_t objHiTab[256];            // per slot (>> 5): the object draws from the second sprite table...
  uint16_t objHiType[256];          // ...while it has this type
  uint32_t gfxTrigFrame;            // frame + 1 of the live graphics triggers below (gfxTriggers)
  int gfxTrigN;
  int16_t gfxTrigX[4];
  bool gfxTrigBehind[4], gfxTrigPast[4];  // ...the side of each that has lost its sprite set
  uint16_t drawnPend;                // the object whose pieces are being written (0: none, or Buster)
  int drawnPendX;
  uint8_t pileFirst, pileCount;     // 2-1's barrel pile clear narrowed to these columns (count 0: as the game does it)
  bool shownValid;
  bool extValid[2];
  bool sweepFreeze;                 // level-load sweep running: keep stream offsets
  int sweepTarget;                  // camera x the sweep ends at
  int sweepFrames;
  uint32_t sweepTimeouts;           // statistics (sweeps that never reached their target)
  // widescreen object range (see bbl.c)
  bool objWide;                     // object range currently widened
  int objLo, objHi;                 // screen x range objects may live in
  int objDespawnA, objDespawnC;     // removal check operands patched into the ROM
  bool objEdgeValid;                // spawn edges below are valid
  int objEdgeL, objEdgeR;           // last widened spawn columns (world x)
  int objRow;                       // last camera y & $FFF0
  uint32_t objEarly, objEvicted;    // statistics (early spawns, slots given back)
  // Buster's screen bounds on auto-scrolling stages (see bbl.c)
  uint32_t playArea;                // area playAuto belongs to
  bool playAuto;                    // the camera scrolled by itself in this area
  bool playWide;                    // widened bounds in use
  bool playLoaded;                  // state just loaded
  int playLo, playHi;               // picture edges in Buster's coordinates
  // more sprites in widescreen (see bbl.c)
  bool sprExt;                      // the game is filling a second sprite table
  uint8_t sprBase[544];             // the first one meanwhile
  // sprites fixed to the 4:3 screen whatever the view shift (football's
  // scoreboard marker and pass box dot, see sprFixedX in bbl.c)
  bool sprFixedSlot;                // the object being drawn is one
  uint8_t oamFixedNext[128];        // sprite table entries written this frame
  uint8_t oamFixedShown[128];       // ...in the table the PPU shows
  // tilemap / collision ring repair (see bbl.c)
  bool ringReady;                   // writer hooks installed
  bool ringSeen[2];                 // BG1 / BG2 streamed by the game in this area
  bool ringAdopt[2];                // records unknown: learn them from the picture
  uint32_t ringArea;                // area / map the records belong to
  uint32_t ringRepaired;            // statistics (columns redrawn)
  int ringCamX[2], ringCamY[2];     // camera of each layer when last checked
  uint32_t ringCamFrame[2];         // frame that check stands for (+1)
  uint32_t ringOwner[2][64 * 64];   // world cell each ring cell holds
  uint8_t ringGameWrote[2][64 * 64]; // ring cells the game wrote into VRAM during the last frame
  uint8_t ringKept[2][64 * 64];     // ring cells shown as the game left them after a camera jump
  uint32_t ringRepairFrame;         // frame the repair last ran for
  uint32_t ringLineOff[2][PPU_BUF_H]; // per screen line: frame + 1 it was last scrolled away from the camera
  bool ringLinesStale;              // the per-line scroll record is from before a state load
  uint32_t collapseSeen;            // frame + 1 2-3's collapsing bridge band was last seen
  bool bandLive;                    // the band is on screen this frame (bandH: its scroll)
  int bandH;
  bool colShort;                    // the column writer running now fills only the rows above the band
  uint32_t objTakeovers;            // statistics (slots cleared for a spawn)
  struct ObjPend { uint16_t adr, who; uint8_t val; uint32_t frame; } objPend[32];
  int objPendN;                     // writes into such slots by other objects
  uint8_t objLineage[0x1000];       // per object slot: spawned by an extra object
  uint16_t objEntry[0x1000];        // per object slot: list entry it was spawned early from
  uint8_t objTaint[0x1000];         // per object slot: spawned early or kept alive by the wider range
  int16_t objCheckX[0x1000];        // per object slot: x at its last range check (frame + 1 in objCheckFrame)
  uint32_t objCheckFrame[0x1000];
  bool objAutoRight;                // this frame: the camera scrolls right by itself (autoRight)
  uint16_t objGen[0x1000];          // per object slot: counts the objects it has held
  uint16_t objParent[0x1000];       // per object slot: slot of the object that spawned it (0: none)
  uint16_t objParentGen[0x1000];    //   and that slot's objGen at the time
  uint32_t optArea;                 // offset-per-tile: area, and the camera range the game
  int optMinCam, optMaxCam;         //   has shown its offsets over
  bool optCamValid;
  uint16_t objHoldSlot, objHoldType; // extra object hidden from a spawn check
  uint16_t objRunning;              // object whose code the object loop is running (0: none)
  bool fbHeld;                      // football: Buster held at the right of the 4:3 screen
  uint16_t optPend[80];             // offset-per-tile values for the widescreen columns,
  bool optPendValid;                //   a frame before they reach the picture
  uint32_t optFrame;
  uint32_t objExtFrame[0x1000];     // per object slot ($0000-$1FFF / 2): last frame
                                    // it was alive only thanks to the wider range
} Bbl;

// Returns NULL and fills err on failure.
Bbl* bbl_create(const char* romPath, char* err, int errSize);
void bbl_destroy(Bbl* g);
void bbl_reset(Bbl* g, bool hard);
void bbl_setWidescreen(Bbl* g, int extraCols);
void bbl_setButtons(Bbl* g, int player, uint16_t mask);
void bbl_runFrame(Bbl* g);
// Picture: PPU_BUF_W wide buffer, visible region starts at column
// PPU_MAX_EXTRA - extraCols and is 256 + 2 * extraCols wide.
const uint32_t* bbl_pixels(Bbl* g);
int bbl_width(Bbl* g);
int bbl_height(Bbl* g);   // 224 (or 239 with overscan)
void bbl_getSamples(Bbl* g, int16_t* out, int samples);

// Memory helpers (WRAM $7E0000-$7FFFFF)
static inline uint8_t bbl_peek(Bbl* g, uint32_t adr) { return g->snes->ram[adr & 0x1ffff]; }
static inline uint16_t bbl_peek16(Bbl* g, uint32_t adr) { return g->snes->ram[adr & 0x1ffff] | (g->snes->ram[(adr + 1) & 0x1ffff] << 8); }
static inline void bbl_poke(Bbl* g, uint32_t adr, uint8_t v) { g->snes->ram[adr & 0x1ffff] = v; }
static inline void bbl_poke16(Bbl* g, uint32_t adr, uint16_t v) { g->snes->ram[adr & 0x1ffff] = v & 0xff; g->snes->ram[(adr + 1) & 0x1ffff] = v >> 8; }
// ROM patching (address given as SNES LoROM address, e.g. 0x80EF2C)
uint32_t bbl_romOffset(uint32_t snesAdr);
void bbl_romPatch(Bbl* g, uint32_t snesAdr, uint8_t value);
void bbl_romRestore(Bbl* g, uint32_t snesAdr);
uint8_t bbl_romRead(Bbl* g, uint32_t snesAdr);

// Calls a 65816 routine (ending in RTL) from the game's idle loop at $80:80C1.
// Steps the CPU until it is in the idle loop outside of NMI, then runs the
// routine as if the idle loop had called it, returning once it is done.
bool bbl_callRoutine(Bbl* g, uint32_t addr, uint16_t a);

// Debug: checks recorded tilemap ring cells against the level (see bbl.c)
int bbl_ringVerify(Bbl* g, int layer, int* checked);
int bbl_viewVerify(Bbl* g, int layer, int* checked, int verbose);
int bbl_viewFix(Bbl* g, int layer);
int bbl_ringCellInfo(Bbl* g, int layer, int cx, int cy, uint16_t* vram, uint16_t* model, uint32_t* owner);

// Save states (malloc'd buffer)
int bbl_saveState(Bbl* g, uint8_t** out);
bool bbl_loadState(Bbl* g, const uint8_t* data, int size);

bool bbl_readFile(const char* path, uint8_t** data, int* size);
bool bbl_writeFile(const char* path, const void* data, int size);
