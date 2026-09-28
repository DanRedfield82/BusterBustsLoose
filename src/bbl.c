#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bbl.h"
#include "cart.h"
#include "dsp.h"
#include "hud.h"

static uint32_t crc32_calc(const uint8_t* d, int n) {
  uint32_t c = 0xffffffffu;
  for(int i = 0; i < n; i++) {
    c ^= d[i];
    for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return ~c;
}

bool bbl_readFile(const char* path, uint8_t** data, int* size) {
  FILE* f = fopen(path, "rb");
  if(!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(n <= 0 || n > 64 * 1024 * 1024) { fclose(f); return false; }
  uint8_t* buf = malloc(n);
  if(fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return false; }
  fclose(f);
  *data = buf;
  *size = (int)n;
  return true;
}

bool bbl_writeFile(const char* path, const void* data, int size) {
  FILE* f = fopen(path, "wb");
  if(!f) return false;
  bool ok = fwrite(data, 1, size, f) == (size_t)size;
  fclose(f);
  return ok;
}

static void onCpuHook(void* ctx, uint32_t pc);
#define PLAY_MOVE 0x80E6A2
#define PLAY_EDGE 0x80CFA2
// Buster's code that moves his screen x with 8-bit maths on the pixel alone
// (STA $08,X; see playPixel)
static const uint32_t playPixelSites[] = { 0x80DA4E, 0x80E423, 0x80E5EB };
#define CAM_HOLD 0x83DC2F    // 2-3's car controller sets BG1's camera back to $0800 (STA $1902)
#define CAM_SHAKE 0x84F711   // 5-6's door shakes the screen: BG1's camera from a table ($81:EBB3: 2, -1, 1, -1)
#define GFX_RESTORE 0x82C8C4 // the sprite set trigger: CMP #$00C0 (x >= 192: the previous set back)
#define GFX_LOAD 0x82C8EB    // ...CMP #$0040 (x < 64: the next set)
static void gfxLoadCheck(Bbl* g);
static void gfxRestoreCheck(Bbl* g);
static void gfxDrawn(Bbl* g, uint16_t slot, int x);
static bool gfxHide(Bbl* g, uint16_t slot);
extern int bbl_objDebug;
#define PILE_CLEAR 0x83B290  // 2-1's barrel pile out of range: its picture cleared at a fixed place in BG1's ring
#define PILE_START 0x83B29B  // ...STA $00: the first row's VRAM address
#define PILE_COUNT 0x83B2A7  // ...STA $04: the words cleared per row
static void pileClear(Bbl* g);
#define CHASE_SWITCH 0x83E3CE // 2-5: BG1 goes over to the handcar chase's map (LDX #$1900 ... STA $26,X)
static void chaseSwitch(Bbl* g);
static void camHold(Bbl* g);
static void playMove(Bbl* g);
static void playEdge(Bbl* g);
static void playPixel(Bbl* g);
#define SPR_START 0x8090C1
#define SPR_FULL 0x8092AC
#define SPR_RANGE 0x80FF80   // the widescreen sprite range helper (applySpriteRange)
#define SPR_OBJ 0x8091FA     // an object's pieces are about to be written (X: its slot)
#define SPR_ATTR 0x809279    // a piece's attribute byte is about to be written (A)
#define SPR_ENTRY 0x8091F8   // the sprite pass is about to draw an object (X: its slot), with either writer
#define SPR_ATTR2 0x8091BA   // ...the other one ($80:913C, sprite pointers with bit 15): a piece's attribute byte (A)
#define SPR_Y 0x809200       // the object's y (A, 16-bit) is about to go to $0A for its pieces
static void sprAttr(Bbl* g);
static void netUpdate(Bbl* g);
static void sprRange(Bbl* g);
#define SPR_COPY 0x8081BD
#define OPT_TABLE 0x81FF59
static void sprFull(Bbl* g);
static void sprCopy(Bbl* g);
static int sprFixedX(void* ctx, int index, int x, int y);
static void optTable(Bbl* g);
static void ringVramWrite(void* ctx, uint16_t adr);
static void objInstallHooks(Bbl* g);
static uint16_t rom16(Bbl* g, uint32_t adr);
static uint32_t ringAreaKey(Bbl* g);
static void ringInstall(Bbl* g);
static void ringReset(Bbl* g);
static void collapseClip(Bbl* g, int shift);

// describes a rejected ROM from its internal header: which game and region it
// says it is, so the player learns why it can't be used (PAL, Japanese...)
static void describeRom(const uint8_t* rom, int size, char* out, int n) {
  static const char* const regions[] = {
    "Japanese", "USA", "European (PAL)", "Swedish (PAL)", "Finnish (PAL)", "Danish (PAL)",
    "French (PAL)", "Dutch (PAL)", "Spanish (PAL)", "German (PAL)", "Italian (PAL)",
    "Chinese", "Indonesian", "Korean", NULL, "Canadian", "Brazilian", "Australian (PAL)"
  };
  const uint8_t* h = NULL;
  static const int bases[] = { 0x7FC0, 0xFFC0 };
  for(int i = 0; i < 2 && !h; i++) {
    if(bases[i] + 32 > size) continue;
    const uint8_t* c = rom + bases[i];
    if(((c[28] | c[29] << 8) ^ (c[30] | c[31] << 8)) == 0xFFFF) h = c;
  }
  char title[22];
  int t = 0;
  if(h) {
    for(int i = 0; i < 21; i++) title[t++] = h[i] >= 0x20 && h[i] < 0x7F ? h[i] : ' ';
    while(t > 0 && title[t - 1] == ' ') t--;
  }
  title[t] = 0;
  if(!h || t == 0) {
    snprintf(out, n, "It doesn't look like a SNES game, or the dump is damaged.");
    return;
  }
  const char* region = h[25] < sizeof(regions) / sizeof(regions[0]) ? regions[h[25]] : NULL;
  bool thisGame = !strncmp(title, "TINYTOON adventures", 19);
  if(thisGame && h[25] == 1)
    snprintf(out, n, "It is the USA version, but the file has been modified\n(a hack, a patch or a bad dump).");
  else if(region)
    snprintf(out, n, "It is the %s version of \"%s\".", region, title);
  else
    snprintf(out, n, "It is \"%s\" (region %d).", title, h[25]);
}

Bbl* bbl_create(const char* romPath, char* err, int errSize) {
  uint8_t* data = NULL;
  int size = 0;
  if(!bbl_readFile(romPath, &data, &size)) {
    snprintf(err, errSize, "Could not read ROM file:\n%s", romPath);
    return NULL;
  }
  // strip a copier header if present
  uint8_t* rom = data;
  if(size == BBL_ROM_SIZE + 512) { rom += 512; size -= 512; }
  if(size != BBL_ROM_SIZE || crc32_calc(rom, size) != BBL_ROM_CRC32) {
    char what[160];
    describeRom(rom, size, what, sizeof(what));
    snprintf(err, errSize,
      "The file\n%s\ncan't be used. %s\n\n"
      "This port only works with the USA version:\n"
      "\"Tiny Toon Adventures - Buster Busts Loose! (USA)\"\n"
      "(1 MiB, CRC32 %08X). European, Japanese and modified\n"
      "versions are not supported.", romPath, what, BBL_ROM_CRC32);
    free(data);
    return NULL;
  }
  Bbl* g = calloc(1, sizeof(Bbl));
  g->rom = malloc(BBL_ROM_SIZE);
  memcpy(g->rom, rom, BBL_ROM_SIZE);
  g->romSize = BBL_ROM_SIZE;
  free(data);
  g->snes = snes_init();
  if(!snes_loadRom(g->snes, g->rom, g->romSize)) {
    snprintf(err, errSize, "The emulation core failed to load the ROM.");
    bbl_destroy(g);
    return NULL;
  }
  g->hudReanchor = true;
  g->cpuBoost = 20;   // enough to keep busy widescreen scenes as fast as in 4:3
  // the level-load camera sweep starts here, with the final camera still in $1902
  cpu_setHook(g->snes->cpu, 0x80C77C);
  g->snes->ppu->vramWriteHook = ringVramWrite;
  g->snes->ppu->vramWriteCtx = g;
  g->snes->ppu->spriteXHook = sprFixedX;
  g->snes->ppu->spriteXHookCtx = g;
  cpu_setHook(g->snes->cpu, PLAY_MOVE);
  cpu_setHook(g->snes->cpu, PLAY_EDGE);
  for(int i = 0; i < 3; i++) cpu_setHook(g->snes->cpu, playPixelSites[i]);
  cpu_setHook(g->snes->cpu, CAM_HOLD);
  cpu_setHook(g->snes->cpu, CAM_SHAKE);
  if(bbl_romRead(g, GFX_RESTORE) == 0xC9 && (bbl_romRead(g, GFX_RESTORE + 1) | bbl_romRead(g, GFX_RESTORE + 2) << 8) == 0x00C0 &&
     bbl_romRead(g, GFX_LOAD) == 0xC9 && (bbl_romRead(g, GFX_LOAD + 1) | bbl_romRead(g, GFX_LOAD + 2) << 8) == 0x0040) {
    cpu_setHook(g->snes->cpu, GFX_RESTORE);
    cpu_setHook(g->snes->cpu, GFX_LOAD);
  }
  cpu_setHook(g->snes->cpu, PILE_CLEAR);
  cpu_setHook(g->snes->cpu, PILE_START);
  cpu_setHook(g->snes->cpu, PILE_COUNT);
  if(bbl_romRead(g, CHASE_SWITCH) == 0xA2 && bbl_romRead(g, CHASE_SWITCH + 7) == 0x24 && bbl_romRead(g, CHASE_SWITCH + 9) == 0x1F &&
     bbl_romRead(g, CHASE_SWITCH + 10) == 0x90)
    cpu_setHook(g->snes->cpu, CHASE_SWITCH);
  cpu_setHook(g->snes->cpu, SPR_START);
  cpu_setHook(g->snes->cpu, SPR_FULL);
  cpu_setHook(g->snes->cpu, SPR_RANGE);
  cpu_setHook(g->snes->cpu, SPR_OBJ);
  cpu_setHook(g->snes->cpu, SPR_ATTR);
  cpu_setHook(g->snes->cpu, SPR_ENTRY);
  cpu_setHook(g->snes->cpu, SPR_ATTR2);
  cpu_setHook(g->snes->cpu, SPR_Y);
  cpu_setHook(g->snes->cpu, SPR_COPY);
  cpu_setHook(g->snes->cpu, OPT_TABLE);
  objInstallHooks(g);
  ringInstall(g);
  ringReset(g);
  g->snes->cpu->hookFn = onCpuHook;
  g->snes->cpu->hookCtx = g;
  return g;
}

void bbl_destroy(Bbl* g) {
  if(!g) return;
  if(g->snes) snes_free(g->snes);
  free(g->rom);
  free(g);
}

void bbl_reset(Bbl* g, bool hard) {
  snes_reset(g->snes, hard);
  g->frameCount = 0;
  g->sweepFreeze = false;
  g->sweepFrames = 0;
  g->shownValid = false;
  g->extValid[0] = g->extValid[1] = false;
  g->sprExt = false;
  g->snes->ppu->extOamCount = 0;
}

// Level streaming. The game draws level columns into a 512 px wide tilemap
// ring: a column 64 px beyond the screen edge whenever the camera crosses an
// 8 px boundary (offsets $80:C998 = +$140 and $80:C9E6 = -$40), rows for
// vertical scrolling ($80:CC87: start $80:CC97 = camera-$40, $80:CC8F/CCA0 =
// 49 columns), and the whole range when a level loads (a camera sweep: distance
// $80:C77D/$80:C787, steps $80:C778). Columns in void screens are never
// written, so tiles beyond a level edge are kept.
//
// In widescreen the streaming follows the visible window, which the view shift
// moves near level edges: the column offsets and the row start are set every
// frame to just outside the window, rows cover the whole ring and the load
// sweep pre-draws up to 640 px (sized per area when the load starts).
static const uint32_t streamSites[] = {
  0x80C998, 0x80C9E6, 0x80CC97,             // per frame: right / left column, row start
  0x80C77D, 0x80C787, 0x80C778,             // load sweep distance BG1 / BG2, steps
  0x80CC8F, 0x80CCA0,                       // row column count (LDA / ADC operands)
};

static void patch16(Bbl* g, uint32_t adr, uint16_t v) {
  bbl_romPatch(g, adr, v & 0xff);
  bbl_romPatch(g, adr + 1, v >> 8);
}

// column offsets for a layer whose view is shifted by `shift` (see below)
static void streamOffsetsFor(int e, int shift, int* rightOut, int* leftOut) {
  int right = (256 + e + shift + 7) & ~7;
  int left = (e - shift + 7) & ~7;
  int extra = (504 - right - left) / 2;
  if(extra > 64) extra = 64;
  if(extra < 0) extra = 0;
  extra &= ~7;
  right += extra;
  left += extra;
  if(right > 504) right = 504;
  if(left < 8) left = 8;
  if(left > 248) left = 248;
  *rightOut = right;
  *leftOut = left;
}

static void applyStreamOffsets(Bbl* g, int e, int shift) {
  // A column is drawn when the camera crosses an 8 px boundary, at camera +
  // offset. The offsets must be multiples of 8 (like the original $140/$40):
  // otherwise the column picked depends on where between two boundaries the
  // camera landed, and fast movement skips some columns and repeats others.
  // The smallest safe values are right >= 256 + e + shift (the column is in
  // place before it scrolls into view) and left >= e - shift. When the camera
  // reverses, the last column streamed on one side must not have reused a ring
  // slot the other side has not redrawn yet: right + left <= 504, which holds
  // for these values with margins up to 116.
  // right: column drawn when moving right; left: when moving left (camera -
  // left). The spare ring space goes to both sides (up to the original 64 px):
  // objects draw into the tilemap and collision where they stand, so like in
  // the original they must only live where the columns have been streamed.
  int right, left;
  streamOffsetsFor(e, shift, &right, &left);
  g->streamRight = right;
  g->stream2Right = right;           // BG2 follows until its own view is known
  g->stream2Left = left;
  g->streamLeft = left;
  patch16(g, 0x80C998, (uint16_t)right);
  patch16(g, 0x80C9E6, (uint16_t)(-left));
  patch16(g, 0x80CC97, (uint16_t)left);  // rows start at the left edge of the window
}

// the original column offsets, for gameplay shown 4:3 (scene43)
static void applyStreamOriginal(Bbl* g) {
  for(int i = 0; i < 3; i++) { bbl_romRestore(g, streamSites[i]); bbl_romRestore(g, streamSites[i] + 1); }
  g->streamRight = g->stream2Right = rom16(g, 0x80C998);
  g->streamLeft = g->stream2Left = (uint16_t)-rom16(g, 0x80C9E6);
}

// Other screen range checks on an object's x ($08,X, then ADC #a / CMP #c:
// x in [-a, c - a)): objects that draw themselves into the tilemap only while
// near the original screen (the Frontier Town barrel pile, $83:B2D2), act or
// stay only while on it, or remove themselves outside it ($83:A2D0). Widened
// to the streamed columns (16 px inside, for tiles drawn into the ring); the
// objects they keep beyond the original range count as extra (see the object
// range below). $83:EDAB checks a table entry against the camera.
static const uint32_t drawRangeSites[] = { 0x83B2D5, 0x83B26E, 0x8398C3, 0x83A2D0, 0x83F00B, 0x84CB9C, 0x84FCCF, 0x83EDAB };
static const bool drawRangeObject[] = { true, true, true, true, true, true, true, false };
// how far inside the streamed columns on the right: the barrel pile's parts
// draw a 32 px block from their x once, and whatever of it lies beyond the
// streamed columns is overwritten when they stream in (2-1's piles lost the
// right half of a barrel)
static const int drawRangeRight[] = { 32, 16, 16, 16, 16, 16, 16, 16 };

// The airship's blimp top ($84:85A0): Buster stands on it between two edges
// worked out each frame, the right one clamped to 288 ($84:85B6 CMP / $84:85BB
// LDA #$0120), so far right on a widescreen picture he fell through it. Raised
// by the extra columns. (The left one is clamped to 0 and the surface maths
// is unsigned, so on the airship Buster keeps the original left limit.)
static void applyAirship(Bbl* g, bool wide) {
  static const uint32_t sites[2] = { 0x8485B7, 0x8485BC };
  for(int i = 0; i < 2; i++) {
    if((bbl_romRead(g, sites[i]) | bbl_romRead(g, sites[i] + 1) << 8) != 0x0120) return;   // not the expected code
    if(wide) patch16(g, sites[i], (uint16_t)(0x120 + g->extraCols));
    else { bbl_romRestore(g, sites[i]); bbl_romRestore(g, sites[i] + 1); }
  }
}

// The graphics trigger ($82:C8AF), a level object that swaps the sprite
// graphics for the next part of a level (2-1 twice, the magicians in 2-4's
// carriage): it loads the next set once it is less than 64 px from the left
// of the screen and the previous one back once 192 px or more. At 4:3 what
// uses either set is off screen then; the widescreen picture can show both.
// The next set comes up to `right` px sooner (the extra columns right of the
// 4:3 screen: an early-spawned magician showed garbage there) unless an object
// behind the trigger (not Buster) was drawn inside the 4:3 screen, which would
// lose its graphics where the original shows them (2-1's hay bandit). The
// previous set comes back up to `right` px later only while something past
// the trigger is still in the picture and nothing behind it is (walking back
// in 2-1, the enemies behind the triggers came back scrambled). Objects
// drawn last frame or this one with pieces from the second sprite table
// count: the swapped sets all go there ($7000-$7FFF in VRAM), while Buster,
// stars and the like use the first (gfxDrawn, from SPR_ENTRY / SPR_ATTR).
static void applyGfxTrigger(Bbl* g, bool wide, int e, int shift) {
  int right = e + shift;
  if(right < 0) right = 0;
  if(right > 112) right = 112;   // (the next set sooner stays below 192 - 16: no swapping back and forth)
  g->gfxWide = wide && e > 0;   // (the windows below are empty when `right` is 0: the hiding still applies)
  g->gfxRight = right;
  g->gfxPicLo = shift - e;
  g->gfxPicHi = 256 + shift + e;
}

static void gfxDrawn(Bbl* g, uint16_t slot, int x) {
  if(g->drawnFrame != g->frameCount + 1) {
    // a new frame: the last one's list becomes the previous one
    g->drawnPrevN = 0;
    if(g->drawnFrame == g->frameCount) {
      memcpy(g->drawnPrevX, g->drawnX, g->drawnN * sizeof(g->drawnX[0]));
      memcpy(g->drawnPrevSlot, g->drawnSlot, g->drawnN * sizeof(g->drawnSlot[0]));
      g->drawnPrevN = g->drawnN;
    }
    g->drawnN = 0;
    g->drawnFrame = g->frameCount + 1;
  }
  if(g->drawnN < (int)(sizeof(g->drawnX) / sizeof(g->drawnX[0]))) { g->drawnSlot[g->drawnN] = slot; g->drawnX[g->drawnN++] = (int16_t)x; }
}

// an object drawn from the second sprite table (not Buster) last frame or this
// one at screen x in [lo, hi)?
static bool gfxDrawnIn(Bbl* g, int lo, int hi) {
  int n = g->drawnFrame == g->frameCount + 1 || g->drawnFrame == g->frameCount ? g->drawnN : 0;
  int np = g->drawnFrame == g->frameCount + 1 ? g->drawnPrevN : 0;
  for(int k = 0; k < n + np; k++) {
    int x = k < n ? g->drawnX[k] : g->drawnPrevX[k - n];
    if(x < lo || x >= hi) continue;
    if(bbl_objDebug) {
      uint16_t s = k < n ? g->drawnSlot[k] : g->drawnPrevSlot[k - n];
      printf("  (slot %04X type %04X x %d)\n", s, bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x10)), x);
    }
    return true;
  }
  return false;
}

// A: the trigger's x
static void gfxLoadCheck(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  int x = (int16_t)cpu->a;
  if(!g->gfxWide || x < 0x40 || x >= 0x40 + g->gfxRight) return;   // the original test decides
  bool behind = gfxDrawnIn(g, -32, x < 256 ? x : 256);
  if(bbl_objDebug) printf("  gfx trigger f=%u x=%d: next set %s\n", g->frameCount, x, behind ? "waits (behind it on the 4:3 screen)" : "now");
  if(!behind) cpu->a = 0;
}

static void gfxRestoreCheck(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  int x = (int16_t)cpu->a;
  if(!g->gfxWide) return;
  if(x >= 112 && x < 0xC0) {
    // sooner: something behind the trigger is back on the 4:3 screen (and nothing
    // past it is there). Walking back the original showed it scrambled until 192,
    // for good when the trigger stopped at 191 (2-1's hay bales).
    bool back = gfxDrawnIn(g, 0, x) && !gfxDrawnIn(g, x, 256);
    if(bbl_objDebug && back) printf("  gfx trigger f=%u x=%d: previous set sooner\n", g->frameCount, x);
    if(back) cpu->a = 0xC0;
    return;
  }
  if(x < 0xC0 || x >= 0xC0 + g->gfxRight) return;   // the original test decides
  bool past = gfxDrawnIn(g, x, g->gfxPicHi + 32), behind = gfxDrawnIn(g, g->gfxPicLo - 32, x);
  if(bbl_objDebug) printf("  gfx trigger f=%u x=%d: previous set %s\n", g->frameCount, x, past && !behind ? "waits" : "now");
  if(past && !behind) cpu->a = 0xBF;
}

// The live graphics triggers (type $x320), once a frame: where each stands and
// which side of it has lost its sprite set (the sets from $81:B5E9 by the
// trigger's +$5E: previous, its palette, next, its palette). Behind it once it
// has loaded the next set; past it before, unless the previous set is none
// (then going back loads nothing and the next set stays) or both are the same.
static void gfxTriggers(Bbl* g) {
  if(g->gfxTrigFrame == g->frameCount + 1) return;
  g->gfxTrigFrame = g->frameCount + 1;
  g->gfxTrigN = 0;
  for(uint16_t s = 0x0400; s < 0x1900 && g->gfxTrigN < 4; s += 0x60) {   // (the slots the sprite pass draws, $80:90E3)
    if((bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x10)) & 0x7FFF) != 0x0320) continue;
    int par = bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x5E)) & 0x7F;
    uint16_t prev = rom16(g, 0x81B5E9 + par * 8), next = rom16(g, 0x81B5ED + par * 8);
    bool loaded = bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x3C)) != 0;
    int k = g->gfxTrigN++;
    g->gfxTrigX[k] = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x08));
    g->gfxTrigBehind[k] = loaded && next && next != prev;
    g->gfxTrigPast[k] = !loaded && next && prev && next != prev;
  }
}

// An object drawn from the second sprite table whose set a trigger has swapped
// out is hidden outside the 4:3 screen, where the original would not show it
// either (it still acts), rather than shown scrambled: 2-1's hay bandit stays
// in the picture well after the swap while the duel's scroll shows the extra
// width behind Buster.
static bool gfxHide(Bbl* g, uint16_t slot) {
  int x = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x08));
  if(x >= -16 && x < 272) return false;
  int i = (slot >> 5) & 0xFF;
  if(!g->objHiTab[i] || g->objHiType[i] != bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x10))) return false;
  gfxTriggers(g);
  for(int k = 0; k < g->gfxTrigN; k++)
    if((g->gfxTrigBehind[k] && x < g->gfxTrigX[k]) || (g->gfxTrigPast[k] && x >= g->gfxTrigX[k])) return true;
  return false;
}

// Screen window tests of the form CMP #hi / BCC in / CMP #lo / BCx (x in
// [lo - $10000, hi) passes), in bank $84:
//  - $84:AFE7: 3-3's swinging platforms place each drum and chain piece and
//    remove the ones outside x -16..271, so at 21:9 they vanished mid-swing
//  - $84:8A71: an object kept just off the right of the screen outside
//    -32..295 (moved to x 288, LDA at $84:8A7B)
//  - $84:8ACD, $84:8D96: objects removing themselves outside -40..295
// The first two follow the picture; the removals follow the object range
// (objects they keep beyond the original window count as extra).
typedef struct { uint32_t site; uint16_t hi, lo; uint32_t reset; bool removal; } WinSite;
static const WinSite winSites[] = {
  { 0x84AFE7, 0x0110, 0xFFF0, 0, false },
  { 0x848A71, 0x0128, 0xFFE0, 0x848A7C, false },
  { 0x848ACD, 0x0128, 0xFFD8, 0, true },
  { 0x848D96, 0x0128, 0xFFD8, 0, true },
};

static bool winSiteOk(Bbl* g, const WinSite* w) {
  return bbl_romRead(g, w->site) == 0xC9 && rom16(g, w->site + 1) == w->hi && bbl_romRead(g, w->site + 5) == 0xC9 &&
         rom16(g, w->site + 6) == w->lo && (!w->reset || rom16(g, w->reset) == 0x0120);
}

static void applyWindowSites(Bbl* g, bool wide, int shift) {
  int e = g->extraCols;
  for(int i = 0; i < (int)(sizeof(winSites) / sizeof(winSites[0])); i++) {
    const WinSite* w = &winSites[i];
    if(!winSiteOk(g, w)) continue;
    if(!wide) {
      bbl_romRestore(g, w->site + 1); bbl_romRestore(g, w->site + 2);
      bbl_romRestore(g, w->site + 6); bbl_romRestore(g, w->site + 7);
      if(w->reset) { bbl_romRestore(g, w->reset); bbl_romRestore(g, w->reset + 1); }
      continue;
    }
    int hi = w->hi, lo = w->lo - 0x10000;
    if(w->removal) {
      // as far outside the object range as the original is outside 256 px
      int h = g->objHi - (320 - hi), l = g->objLo + (-64 - lo);
      if(h > hi) hi = h;
      if(l < lo) lo = l;
    } else {
      int right = e + shift, left = e - shift;
      if(right > 0) hi += right;
      if(left > 0) lo -= left;
    }
    patch16(g, w->site + 1, (uint16_t)hi);
    patch16(g, w->site + 6, (uint16_t)lo);
    if(w->reset) patch16(g, w->reset, (uint16_t)(0x0120 + hi - w->hi));
  }
}

// 2-3's collapsing bridge: after the camera jump, $83:E8B0 builds each trestle
// column into BG1 once it is less than 304 px right of the layer's position
// ($7EC084), just past the 4:3 screen; the widescreen picture showed rail
// with no trestle in its last 50 px. Built up to 16 px past the picture's
// right edge instead (no further: the ring repeats every 512 px, and a column
// built further ahead would land in the left margin). Not on the last
// stretch, where the game writes whole columns again (BG1's +$2E bit 15
// clear): the bridge stops there ($7EC084 comes to rest) and everything the
// original built falls; built further ahead, 96 px of bridge were left
// standing over the gap before the rock. The bridge moves on more than the
// extra 170 px in that stretch, so the original's own builder gets to all it
// would build anyway.
static void applyBridgeBuilder(Bbl* g, bool wide, int right) {
  if(bbl_romRead(g, 0x83E8BC) != 0xEF || rom16(g, 0x83E8BD) != 0xC084 || bbl_romRead(g, 0x83E8C3) != 0xC9 ||
     rom16(g, 0x83E8C4) != 0x0130) return;
  int t = 256 + right + 16;
  if(wide && t > 0x130 && (bbl_peek16(g, 0x7E192E) & 0x8000)) patch16(g, 0x83E8C4, (uint16_t)(t < 0x1F0 ? t : 0x1F0));
  else { bbl_romRestore(g, 0x83E8C4); bbl_romRestore(g, 0x83E8C5); }
}

static void applyDrawRanges(Bbl* g, bool wide) {
  applyAirship(g, wide);
  for(int i = 0; i < (int)(sizeof(drawRangeSites) / sizeof(drawRangeSites[0])); i++) {
    uint32_t a = drawRangeSites[i];
    if(bbl_romRead(g, a) != 0x69 || bbl_romRead(g, a + 3) != 0xC9) continue;   // not the expected code
    int add = bbl_romRead(g, a + 1) | (bbl_romRead(g, a + 2) << 8);
    int cmp = bbl_romRead(g, a + 4) | (bbl_romRead(g, a + 5) << 8);
    if(!wide) {
      for(int k = 1; k <= 5; k++) if(k != 3) bbl_romRestore(g, a + k);
      continue;
    }
    // the object's 32 px of tiles must stay inside what has been streamed
    int lo = -(g->streamLeft - 16), hi = g->streamRight - drawRangeRight[i];
    if(lo > -add) lo = -add;
    if(hi < cmp - add) hi = cmp - add;
    patch16(g, a + 1, (uint16_t)-lo);
    patch16(g, a + 4, (uint16_t)(hi - lo));
  }
}

static void applyStreamMargin(Bbl* g, int e) {
  if(e <= 0) {
    for(int i = 0; i < (int)(sizeof(streamSites) / sizeof(streamSites[0])); i++) {
      bbl_romRestore(g, streamSites[i]);
      bbl_romRestore(g, streamSites[i] + 1);
    }
    return;
  }
  patch16(g, 0x80C77D, 640);             // load sweep: pre-draw 640 px (onLoadSweep may shorten it)
  patch16(g, 0x80C787, 640);
  patch16(g, 0x80C778, 640 / 8);
  patch16(g, 0x80CC8F, 64);              // rows fill the whole ring
  patch16(g, 0x80CCA0, 64);             // (rows starting left of the level: ADC #count)
  applyStreamOffsets(g, e, 0);
}

// Sprites: the OAM writer at $80:9242 drops sprite pieces whose X is >= 256 or
// < -16. In widescreen a helper in free ROM space ($80:FF80, padding before the
// vectors) accepts the visible game-coordinate window, [-E+s-32, 256+E+s+16)
// for view shift s, with the 9th X bit set for pieces right of 255.
static void applySpriteRange(Bbl* g, int extra, int shift) {
  static const uint32_t hook = 0x80929F; // CMP #$FFF0 / BCS / BRA after the X >= 256 test
  if(g->sprExtra == extra && g->sprShift == shift) return;
  g->sprExtra = extra;
  g->sprShift = shift;
  if(extra <= 0) {
    for(int i = 0; i < 3; i++) bbl_romRestore(g, hook + i);
    for(int i = 0; i < 0x12; i++) bbl_romRestore(g, 0x80FF80 + i);
    return;
  }
  int right = 256 + extra + shift + 16;
  if(right < 256) right = 256;
  int left = -extra + shift - 32;
  if(left < right - 512) left = right - 512; // must not overlap in 9-bit coordinates
  if(left > -16) left = -16;
  g->sprRight = right;
  g->sprLeft = left;
  uint16_t r = (uint16_t)right, l = (uint16_t)left;
  const uint8_t code[] = {
    0xC9, r & 0xff, r >> 8,          // FF80 CMP #right
    0x90, 0x08,                      // FF83 BCC FF8D
    0xC9, l & 0xff, l >> 8,          // FF85 CMP #left
    0xB0, 0x04,                      // FF88 BCS FF8E
    0x4C, 0x89, 0x92,                // FF8A JMP $9289 (skip piece)
    0x38,                            // FF8D SEC (9th bit)
    0x4C, 0x5A, 0x92,                // FF8E JMP $925A (write piece)
  };
  for(int i = 0; i < (int)sizeof(code); i++) bbl_romPatch(g, 0x80FF80 + i, code[i]);
  bbl_romPatch(g, hook, 0x4C);       // JMP $FF80
  bbl_romPatch(g, hook + 1, 0x80);
  bbl_romPatch(g, hook + 2, 0xFF);
}

void bbl_setWidescreen(Bbl* g, int extraCols) {
  if(extraCols < 0) extraCols = 0;
  if(extraCols > 116) extraCols = 116; // see applyStreamOffsets
  g->extraCols = extraCols;
  applyStreamMargin(g, extraCols);
  g->sprExtra = -1;
  applySpriteRange(g, extraCols, 0);
}

// ---- widescreen view shift ------------------------------------------------
//
// Near the edge of a level the camera of the game stops, so the extra
// widescreen columns would look past the level. Instead the picture is shifted
// (rendering only; camera and game logic are untouched) so the level fills the
// screen. Level extents come from the screen map of the level ($1926 = pointer
// in bank $81, $1925/$1924 = width/height in screens - 1, $FF = no screen).
// Background layers are shifted by their own scroll rate relative to BG1,
// measured from the BG1/BG2 cameras ($1902, $1942) and the BG3 scroll register.

// camera struct offsets: +$02 x, +$12 y, +$24/+$25 height/width in screens - 1,
// +$26 screen map pointer (bank $81)
static bool screenExists(Bbl* g, uint32_t cam, int col, int row) {
  int w = bbl_peek(g, cam + 0x25) + 1, h = bbl_peek(g, cam + 0x24) + 1;
  if(col < 0 || row < 0 || col >= w || row >= h) return false;
  uint16_t ptr = (uint16_t)(bbl_peek16(g, cam + 0x26) + row * w + col);
  uint8_t v = ptr >= 0x8000 ? bbl_romRead(g, 0x810000 | ptr) : bbl_peek(g, 0x7E0000 | (ptr & 0x1fff));
  return v != 0xFF;
}

static bool columnExists(Bbl* g, uint32_t cam, int col, int row0, int row1) {
  return screenExists(g, cam, col, row0) && (row1 == row0 || screenExists(g, cam, col, row1));
}

// The train (2-3 to 2-5): one map of 32x6 screens where only the train's row
// is wide; above and below it most screens are void, open sky over the desert
// (Buster drops in through them at the start; the camera rises into them when
// he climbs onto 2-3's boxcar and falls through them when he falls off).
// The game draws nothing for a void screen (only its collision, $FF), so
// what the ring held there stays: the widescreen picture showed old train in
// the sky (2-3's hills). There the ring repair draws them empty (trainVoid),
// and a column counts as part of the level when it has any screen at all; as
// the rows in view alone the picture was clipped at the void screens and slid
// sideways whenever the camera went a line above the roof.
static bool trainVoid(Bbl* g, uint32_t cam) {
  return cam == 0x7E1900 && bbl_peek(g, 0x7E0068) == 1 && bbl_peek(g, 0x7E0094) >= 2 && bbl_peek(g, 0x7E0094) <= 4;
}

static bool trainColumn(Bbl* g, uint32_t cam, int col, int row0, int row1) {
  if(columnExists(g, cam, col, row0, row1)) return true;
  if(!trainVoid(g, cam)) return false;
  int h = bbl_peek(g, cam + 0x24) + 1;
  for(int r = 0; r < h; r++) if(screenExists(g, cam, col, r)) return true;
  return false;
}

// (strict: void screens always end it; what the game's camera can scroll over)
static bool layerExtentOf(Bbl* g, uint32_t cam, int* left, int* right, bool strict) {
  if(bbl_peek16(g, cam + 0x26) < 0x8000) return false;
  // (signed: a screen shake at a level's edge moves the camera a pixel or two
  // outside it, 5-6's door)
  int c = (int16_t)bbl_peek16(g, cam + 0x02), cy = (int16_t)bbl_peek16(g, cam + 0x12);
  int row0 = (cy < 0 ? 0 : cy) >> 8, row1 = (cy + 223) >> 8;
  int col = (c + 128) >> 8;
  if(strict ? !columnExists(g, cam, col, row0, row1) : !trainColumn(g, cam, col, row0, row1)) return false;
  int lo = col, hi = col;
  while(strict ? columnExists(g, cam, lo - 1, row0, row1) : trainColumn(g, cam, lo - 1, row0, row1)) lo--;
  while(strict ? columnExists(g, cam, hi + 1, row0, row1) : trainColumn(g, cam, hi + 1, row0, row1)) hi++;
  *left = lo * 256;
  *right = (hi + 1) * 256;
  return c >= *left && c + 256 <= *right; // camera outside: do not trust it
}

static bool layerExtent(Bbl* g, uint32_t cam, int* left, int* right) { return layerExtentOf(g, cam, left, right, false); }

// shift that moves the widescreen window of a layer (camera c) as close to
// `ideal` as possible while staying inside [left, right)
static int fitShift(int c, int e, int left, int right, int ideal) {
  int span = 256 + 2 * e;
  int want = c - e + ideal;
  int target;
  if(right - left >= span) target = want < left ? left : (want > right - span ? right - span : want);
  else target = left - (span - (right - left)) / 2; // narrow part: centre it
  return target - (c - e);
}

static int clampShift(int shift, int e) {
  return shift > e ? e : (shift < -e ? -e : shift);
}

// per-area starting guesses for the background scroll rates (refined live)
static void parallaxSeed(Bbl* g, int stage, int area) {
  g->parRatio[1] = 0.5f;   // most backgrounds move at half speed
  g->parRatio[2] = 0.0f;   // BG3 is usually the fixed HUD / starfield
  g->parIndep[1] = g->parIndep[2] = false;
  if((stage == 0 && (area == 2 || area == 3)) || (stage == 2 && area == 3) || (stage == 5 && area == 2)) g->parRatio[1] = 1.0f;
  if(stage == 2 && area == 0) g->parRatio[2] = 1.5f;              // mansion foreground
  if(stage == 1 && area >= 2 && area <= 4) g->parIndep[1] = true; // train: scrolling scenery
}

static void updateParallax(Bbl* g) {
  int key = (bbl_peek(g, 0x7E0068) << 8) | bbl_peek(g, 0x7E0094);
  // BG1/BG2 from their camera structs; BG3 (a split-screen foreground in some
  // stages) from the scroll register used near the bottom of the last frame
  int cam[3] = { bbl_peek16(g, 0x7E1902), bbl_peek16(g, 0x7E1942), g->snes->ppu->lineHScroll[199][2] };
  if(!g->parValid || key != g->parKey) {
    g->parKey = key;
    g->parValid = true;
    parallaxSeed(g, key >> 8, key & 0xff);
    g->parSum[0] = g->parSum[1] = g->parSum[2] = g->parSumReg1 = 0;
    g->parWarmup = 60;
    for(int i = 0; i < 3; i++) g->parLast[i] = cam[i];
    g->parLastReg1 = g->snes->ppu->lineHScroll[100][0];
    return;
  }
  // the cameras jump around while an area loads and during fades; only
  // measure once the picture is settled and visible
  Ppu* ppu = g->snes->ppu;
  int reg1 = ppu->lineHScroll[100][0];
  int dReg1 = ((reg1 - g->parLastReg1 + 512) & 1023) - 512;
  g->parLastReg1 = reg1;
  if(g->parWarmup > 0 || ppu->forcedBlank || ppu->brightness == 0) {
    if(g->parWarmup > 0) g->parWarmup--;
    for(int i = 0; i < 3; i++) g->parLast[i] = cam[i];
    return;
  }
  int d[3];
  for(int i = 0; i < 3; i++) {
    d[i] = (int16_t)(cam[i] - g->parLast[i]);
    g->parLast[i] = cam[i];
  }
  d[2] = ((d[2] + 512) & 1023) - 512; // 10-bit scroll register
  if(d[0] < -64 || d[0] > 64) { g->parSum[0] = g->parSum[1] = g->parSum[2] = g->parSumReg1 = 0; return; } // jump (new room)
  // a layer moving while BG1 stands still scrolls on its own (BG3, whose
  // record is a frame late, against the BG1 register of the same frame, or it
  // would look like that every time the camera stops)
  if(dReg1 == 0 && d[2] != 0 && d[2] > -64 && d[2] < 64) g->parIndep[2] = true;
  if(d[0] == 0) {
    if(d[1] != 0 && d[1] > -64 && d[1] < 64) g->parIndep[1] = true;
    return;
  }
  int sign = d[0] > 0 ? 1 : -1;
  g->parSum[0] += d[0] * sign;
  g->parSum[1] += d[1] * sign;
  // BG3 is compared with the BG1 register of the same (previous) frame
  if(dReg1 != 0) {
    int s3 = dReg1 > 0 ? 1 : -1;
    g->parSumReg1 += dReg1 * s3;
    g->parSum[2] += d[2] * s3;
  }
  if(g->parSum[0] >= 16) {
    float r = (float)g->parSum[1] / g->parSum[0];
    if(r >= 0.0f && r <= 2.0f) g->parRatio[1] = r;
    if(g->parSum[0] > 4096) { g->parSum[0] /= 2; g->parSum[1] /= 2; }
  }
  if(g->parSumReg1 >= 16) {
    float r = (float)g->parSum[2] / g->parSumReg1;
    if(r >= 0.0f && r <= 2.0f) g->parRatio[2] = r;
    if(g->parSumReg1 > 4096) { g->parSumReg1 /= 2; g->parSum[2] /= 2; }
  }
}

static void resetView(Bbl* g) {
  Ppu* ppu = g->snes->ppu;
  ppu->clipLeft = -PPU_MAX_EXTRA;
  ppu->clipRight = 256 + PPU_MAX_EXTRA;
  for(int i = 0; i < 2; i++) { ppu->layerClipLeft[i] = -PPU_MAX_EXTRA; ppu->layerClipRight[i] = 256 + PPU_MAX_EXTRA; }
  memset(ppu->viewShift, 0, sizeof(ppu->viewShift));
  ppu->spriteShift = 0;
  ppu->windowFixed = false;
  g->viewShift = 0;
  g->bandLive = false;
  g->objAutoRight = false;
  ppu->bg3Split = false;
  g->ghostOn = false;
}

// sweep distance for one layer: the camera cannot scroll left into a void
// column, so the sweep must not start more than one screen past the level's
// right edge (the game's own 392 px never does)
static int sweepDistance(Bbl* g, uint32_t cam) {
  int left, right;
  if(!layerExtentOf(g, cam, &left, &right, true)) return 392;
  int d = (right + 248 - bbl_peek16(g, cam + 0x02)) & ~7;
  return d > 640 ? 640 : (d < 392 ? 392 : d);
}

// Runs when a level load starts its camera sweep ($80:C77C, before the distance
// is read). The sweep takes several frames, so the column offsets are set for
// where the camera will end up and kept until the sweep is over.
static void onLoadSweep(Bbl* g) {
  g->objEdgeValid = false;
  ringReset(g);
  g->ringAdopt[0] = g->ringAdopt[1] = false;  // the sweep draws (and records) everything
  int e = g->extraCols;
  if(e <= 0) return;
  int c = (int16_t)bbl_peek16(g, 0x7E1902);
  int left, right, shift = 0;
  if(layerExtentOf(g, 0x7E1900, &left, &right, true)) shift = clampShift(fitShift(c, e, left, right, 0), e);
  // both cameras step back 8 px per step ($FE steps, already stored)
  int d1 = sweepDistance(g, 0x7E1900), d2 = sweepDistance(g, 0x7E1940);
  int d = d1 < d2 ? d1 : d2;
  patch16(g, 0x80C77D, (uint16_t)d);
  patch16(g, 0x80C787, (uint16_t)d);
  bbl_poke16(g, 0x7E0000 | (uint16_t)(g->snes->cpu->dp + 0xFE), (uint16_t)(d / 8));
  applyStreamOffsets(g, e, shift);
  g->sweepFreeze = true;
  g->sweepTarget = c;
  g->sweepFrames = 0;
}

// ---- tilemap / collision ring repair ---------------------------------------
//
// BG1 and BG2 are 512x512 px tilemap rings, and BG1's collision map at
// $7F:7000 mirrors its ring cell for cell. The game fills them with its column
// writer ($80:CAF4, per layer struct X, column at camera + Y) and row writer
// ($80:CC87), streaming just what the original 256 px view needs; some events
// (entering the lit café, for one) redraw only that range. In widescreen a
// column outside it would then keep whatever part of the level was in that
// ring cell before: stale graphics and, on BG1, stale collision.
//
// So every cell the writers fill is recorded, and before each frame the
// visible widescreen window is checked; stale columns are redrawn right away,
// the way the column writer draws them (layer struct: +$26 screen map in bank
// $81, +$28 screen blocks and +$2A metatile tiles in bank $7F, +$30 VRAM base;
// tile collision at $7F:CC00).

#define RING_COLUMN 0x80CAF4
#define RING_ROW 0x80CC87
#define RING_COUNT 0x80CB05   // the column writer's row count is in A (layer +$2E bit 15: $1B rows, else $31)

static const uint32_t ringLayers[2] = { 0x7E1900, 0x7E1940 };

// the game wrote a word of VRAM: note it if it is a cell of a ring
int bbl_vramLog;             // probe: print the ring cells the game writes each frame
static uint8_t vramLogCells[2][64 * 64];

static void ringVramWrite(void* ctx, uint16_t adr) {
  Bbl* g = ctx;
  for(int l = 0; l < 2; l++) {
    uint16_t off = (uint16_t)((adr - bbl_peek16(g, ringLayers[l] + 0x30)) & 0x7fff);
    if(off < 64 * 64) {
      g->ringGameWrote[l][off] = 1;
      if(bbl_vramLog) vramLogCells[l][off] = 1;
    }
  }
}

// probe: one line per layer and frame, the ring columns written (ring x: rows)
static void vramLogFlush(Bbl* g) {
  if(!bbl_vramLog) return;
  for(int l = 0; l < 2; l++) {
    int n = 0;
    char buf[2048]; int bl = 0;
    for(int rx = 0; rx < 64; rx++) {
      int y0 = -1, y1 = -1, cnt = 0;
      for(int ry = 0; ry < 64; ry++) {
        int off = ((ry >> 5) & 1) * 0x800 + ((rx >> 5) & 1) * 0x400 + (ry & 31) * 32 + (rx & 31);
        if(!vramLogCells[l][off]) continue;
        if(y0 < 0) y0 = ry;
        y1 = ry; cnt++;
      }
      if(!cnt) continue;
      n += cnt;
      if(bl < (int)sizeof(buf) - 32) bl += snprintf(buf + bl, sizeof(buf) - bl, " %d:%d-%d/%d", rx, y0, y1, cnt);
    }
    if(n) printf("  vw f=%u L%d n=%d cam=%d,%d%s\n", g->frameCount, l, n, bbl_peek16(g, ringLayers[l] + 0x02), bbl_peek16(g, ringLayers[l] + 0x12), buf);
  }
  memset(vramLogCells, 0, sizeof(vramLogCells));
}

static uint16_t romLive16(Bbl* g, uint32_t adr) {
  const uint8_t* rom = g->snes->cart->rom;
  uint32_t o = bbl_romOffset(adr) & (g->romSize - 1);
  return rom[o] | (rom[(o + 1) & (g->romSize - 1)] << 8);
}

static void ringInstall(Bbl* g) {
  cpu_setHook(g->snes->cpu, RING_COLUMN);
  cpu_setHook(g->snes->cpu, RING_ROW);
  cpu_setHook(g->snes->cpu, RING_COUNT);
  g->ringReady = true;
}

static void ringReset(Bbl* g) {
  memset(g->ringOwner, 0xFF, sizeof(g->ringOwner));
  memset(g->ringLineOff, 0, sizeof(g->ringLineOff));
  memset(g->ringKept, 0, sizeof(g->ringKept));
  g->colShort = false;
  g->ringSeen[0] = g->ringSeen[1] = false;
  g->ringAdopt[0] = g->ringAdopt[1] = true;   // learn the picture when first checked
}

static inline uint32_t ringKey(int cx, int cy) { return ((uint32_t)(cx & 0xFFFF) << 16) | (uint32_t)(cy & 0xFFFF); }
// ring cell -> index in the 64x64 tilemap (four 32x32 screens) and collision map
static inline int ringIndex(int cx, int cy) { return ((cy >> 5) & 1) * 0x800 + ((cx >> 5) & 1) * 0x400 + (cy & 31) * 32 + (cx & 31); }

static void ringMark(Bbl* g, int layer, int cx, int cy) {
  uint32_t cam = ringLayers[layer];
  if(cx < 0 || cy < 0 || !screenExists(g, cam, cx >> 5, cy >> 5)) return;
  g->ringOwner[layer][ringIndex(cx, cy)] = ringKey(cx, cy);
}

// the column / row writers are about to run: record what they will fill
static void ringOnWriter(Bbl* g, uint32_t pc) {
  Cpu* cpu = g->snes->cpu;
  uint16_t st = cpu->x;
  int layer = st == 0x1900 ? 0 : (st == 0x1940 ? 1 : -1);
  if(layer < 0) return;
  uint32_t s = 0x7E0000 | st;
  if(bbl_peek(g, s + 0x32) != 0) return;   // streaming off for this layer
  g->ringSeen[layer] = true;
  // Both layers share the column offsets and the row start patched into the
  // ROM, which follow BG1's view. BG2's view shift differs (parallax), so its
  // streaming is moved to its own window here.
  if(g->extraCols > 0) {
    if(pc == RING_COLUMN && layer == 1) {
      if((int16_t)cpu->y == g->streamRight) cpu->y = (uint16_t)g->stream2Right;
      else if((int16_t)cpu->y == -g->streamLeft) cpu->y = (uint16_t)(-g->stream2Left);
    }
    if(pc == RING_ROW) patch16(g, 0x80CC97, (uint16_t)(layer == 1 ? g->stream2Left : g->streamLeft));
  }
  int camX = (int16_t)bbl_peek16(g, s + 0x02), camY = (int16_t)bbl_peek16(g, s + 0x12);
  int off = (int16_t)cpu->y;
  if(pc == RING_COLUMN) {
    int x = off + camX;
    // 2-3's collapsing bridge: the rows below line 152 are the band, shown
    // scrolled by the bridge ($7EC084). For its last stretch the game writes
    // whole columns again ($2E bit 15 clear) so the mountain is in place when
    // the band stops, 264 px right of the camera, where the band shows none of
    // it on a 4:3 screen. The widescreen column goes 200 px further: in the
    // wider band its rows came round the 512 px ring into view, rock hanging
    // in the air left of the train. Such a column fills only the rows above
    // the band, as in the rest of the collapse (the repair fills in the level
    // once the band has stopped).
    g->colShort = false;
    if(layer == 0 && g->extraCols > 0 && g->bandLive && !(bbl_peek16(g, s + 0x2E) & 0x8000) && camY >= 0x40) {
      Ppu* ppu = g->snes->ppu;
      int w = 256 + 2 * g->extraCols;
      int bx = (x - (g->bandH + ppu->viewShift[0] - g->extraCols)) & 511;   // where the band shows it
      if(bx < w + 16 || bx >= 512 - 16) g->colShort = true;
    }
    if(x < 0) return;
    int count = (g->colShort || (bbl_peek16(g, s + 0x2E) & 0x8000)) ? 0x1B : 0x31;
    int y0 = camY - 0x40;
    if(y0 < 0) { count = 0x31 + (y0 >> 3); y0 = 0; }
    for(int i = 0; i < count; i++) ringMark(g, layer, x >> 3, (y0 >> 3) + i);
  } else {
    int y = off + camY;
    if(y < 0) return;
    int count = romLive16(g, 0x80CC8F);
    int x0 = camX - romLive16(g, 0x80CC97);
    if(x0 < 0) { count = (x0 >> 3) + romLive16(g, 0x80CCA0); x0 = 0; }
    for(int i = 0; i < count; i++) ringMark(g, layer, (x0 >> 3) + i, y >> 3);
  }
}

static int ringCellLevel(Bbl* g, int layer, int cx, int cy, int* idx, uint16_t* tile, uint8_t* coll);

// 2-5's tender (the last car) has no lower left in the level: the original
// camera only comes down to it with Buster on the footplate, past the cab, and
// by then the tender is off screen. The wider picture showed the sky through
// it. Drawn in here: under the number plate a plain panel framed like the
// plate, and left of both the tender's right side mirrored. Only the tiles;
// the collision stays the level's (empty, and out of reach anyway).
static const uint16_t tenderPanel[4][12] = {   // map columns 552-563, rows 112-115
  { 0x3138, 0x3136, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137 },
  { 0xB138, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137 },
  { 0xB135, 0x3136, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137, 0x3137 },
  { 0xB130, 0xB131, 0xB132, 0xF132, 0xB132, 0xF132, 0xB132, 0xF132, 0xB132, 0xF132, 0xB132, 0xF132 },
};

// the tile drawn into an empty BG1 cell of the tender, or 0
static uint16_t tenderFill(Bbl* g, int cx, int cy) {
  if(cx < 548 || cx > 563 || cy < 104 || cy > 115 || (cx >= 552 && cy < 112)) return 0;
  if(bbl_peek16(g, 0x7E0068) != 1 || bbl_peek16(g, 0x7E0094) != 4) return 0;
  int idx; uint16_t t; uint8_t c;
  if(ringCellLevel(g, 0, cx, cy, &idx, &t, &c) != 2 || t != 0) return 0;
  // (the tender as the level has it: the plate's corner, the panel's corner, the right side)
  static const struct { int16_t cx, cy; uint16_t t; } sig[] = { { 552, 111, 0xB130 }, { 565, 115, 0xF130 }, { 571, 105, 0x7134 } };
  for(int i = 0; i < 3; i++)
    if(ringCellLevel(g, 0, sig[i].cx, sig[i].cy, &idx, &t, &c) != 2 || t != sig[i].t) return 0;
  if(cx >= 552) return tenderPanel[cy - 112][cx - 552];
  if(ringCellLevel(g, 0, 571 - (cx - 548), cy, &idx, &t, &c) != 2) return 0;
  return t ^ 0x4000;
}

// computes one cell the way the column writer draws it (the tender filled in):
// returns 0 outside the map, 1 for a void screen (collision $FF, no tile), 2
// for a tile
static int ringCell(Bbl* g, int layer, int cx, int cy, int* idx, uint16_t* tile, uint8_t* coll) {
  int k = ringCellLevel(g, layer, cx, cy, idx, tile, coll);
  if(k == 2 && layer == 0 && *tile == 0) {
    uint16_t f = tenderFill(g, cx, cy);
    if(f) *tile = f;
  }
  return k;
}

static int ringCellLevel(Bbl* g, int layer, int cx, int cy, int* idx, uint16_t* tile, uint8_t* coll) {
  uint32_t s = ringLayers[layer];
  int w = bbl_peek(g, s + 0x25) + 1, h = bbl_peek(g, s + 0x24) + 1;
  int col = cx >> 5, row = cy >> 5;
  if(cx < 0 || cy < 0 || col >= w || row >= h) return 0;
  uint16_t mp = (uint16_t)(bbl_peek16(g, s + 0x26) + row * w + col);
  uint8_t scr = mp >= 0x8000 ? bbl_romRead(g, 0x810000 | mp) : bbl_peek(g, 0x7E0000 | (mp & 0x1fff));
  *idx = ringIndex(cx, cy);
  *coll = 0xFF;
  if(scr == 0xFF) return 1;
  int x = cx * 8, y = cy * 8;
  uint16_t block = (uint16_t)(bbl_peek16(g, s + 0x28) + scr * 64);
  uint8_t meta = bbl_peek(g, 0x7F0000 | (uint16_t)(block + ((y & 0xE0) >> 5) * 8 + ((x & 0xE0) >> 5)));
  uint16_t ta = (uint16_t)(bbl_peek16(g, s + 0x2A) + meta * 32 + (y & 0x18) + ((x & 0x18) >> 2));
  *tile = bbl_peek16(g, 0x7F0000 | ta);
  *coll = bbl_peek(g, 0x7FCC00 + (*tile & 0x3FF));
  return 2;
}

static bool trainVoid(Bbl* g, uint32_t cam);

static void ringDrawCell(Bbl* g, int layer, int cx, int cy) {
  int idx; uint16_t tile; uint8_t coll;
  int k = ringCell(g, layer, cx, cy, &idx, &tile, &coll);
  if(k == 0) return;
  if(layer == 0) bbl_poke(g, 0x7F7000 + idx, coll);
  if(k == 1 && !trainVoid(g, ringLayers[layer])) return;
  if(k == 1) tile = 0;   // (a void screen: empty, see trainVoid)
  g->snes->ppu->vram[(bbl_peek16(g, ringLayers[layer] + 0x30) + idx) & 0x7fff] = tile;
  g->ringOwner[layer][idx] = ringKey(cx, cy);
  g->ringKept[layer][idx] = 0;
}

// probe: compares the visible picture of a layer with the level data;
// returns the number of cells that differ (the game's own changes included)
int bbl_viewVerify(Bbl* g, int layer, int* checked, int verbose) {
  Ppu* ppu = g->snes->ppu;
  uint32_t s = ringLayers[layer];
  int e = ppu->extraLeft;
  int lines = ppu->frameOverscan ? 239 : 224;
  int camX = (int16_t)bbl_peek16(g, s + 0x02), camY = (int16_t)bbl_peek16(g, s + 0x12);
  int shift = ppu->viewShift[layer];
  int bad = 0;
  *checked = 0;
  for(int cx = (camX + shift - e) >> 3; cx <= (camX + shift + 256 + e - 1) >> 3; cx++) {
    int sx = cx * 8 - (camX + shift);                      // screen x of the cell
    if(sx + 8 <= ppu->clipLeft || sx >= ppu->clipRight) continue;   // hidden by the clip
    for(int cy = camY >> 3; cy <= (camY + lines - 1) >> 3; cy++) {
      int idx; uint16_t tile; uint8_t coll;
      if(ringCell(g, layer, cx, cy, &idx, &tile, &coll) != 2) continue;
      (*checked)++;
      uint16_t v = ppu->vram[(bbl_peek16(g, s + 0x30) + idx) & 0x7fff];
      if(v != tile) {
        if(verbose && bad < verbose) printf("  view L%d cell %d,%d (screen x %d) vram %04X want %04X\n", layer, cx, cy, sx, v, tile);
        bad++;
      }
    }
  }
  return bad;
}

// probe: writes what the level data says into the visible cells that differ
int bbl_viewFix(Bbl* g, int layer) {
  Ppu* ppu = g->snes->ppu;
  uint32_t s = ringLayers[layer];
  int e = ppu->extraLeft;
  int lines = ppu->frameOverscan ? 239 : 224;
  int camX = (int16_t)bbl_peek16(g, s + 0x02), camY = (int16_t)bbl_peek16(g, s + 0x12);
  int shift = ppu->viewShift[layer], n = 0;
  for(int cx = (camX + shift - e) >> 3; cx <= (camX + shift + 256 + e - 1) >> 3; cx++) {
    int sx = cx * 8 - (camX + shift);
    if(sx + 8 <= ppu->clipLeft || sx >= ppu->clipRight) continue;
    for(int cy = camY >> 3; cy <= (camY + lines - 1) >> 3; cy++) {
      int idx; uint16_t tile; uint8_t coll;
      if(ringCell(g, layer, cx, cy, &idx, &tile, &coll) != 2) continue;
      uint16_t* v = &ppu->vram[(bbl_peek16(g, s + 0x30) + idx) & 0x7fff];
      if(*v != tile) { *v = tile; n++; }
    }
  }
  return n;
}

// probe: one cell: what VRAM holds, what the level data says, who it belongs to
int bbl_ringCellInfo(Bbl* g, int layer, int cx, int cy, uint16_t* vram, uint16_t* model, uint32_t* owner) {
  int idx; uint8_t coll;
  int k = ringCell(g, layer, cx, cy, &idx, model, &coll);
  idx = ringIndex(cx, cy);
  *vram = g->snes->ppu->vram[(bbl_peek16(g, ringLayers[layer] + 0x30) + idx) & 0x7fff];
  *owner = g->ringOwner[layer][idx];
  return k;
}

// probe: compares every recorded cell with what ringCell computes
int bbl_ringVerify(Bbl* g, int layer, int* checked) {
  int bad = 0;
  *checked = 0;
  for(int i = 0; i < 64 * 64; i++) {
    uint32_t o = g->ringOwner[layer][i];
    if(o == 0xFFFFFFFFu) continue;
    int cx = (int16_t)(o >> 16), cy = (int16_t)(o & 0xFFFF);
    int idx; uint16_t tile; uint8_t coll;
    if(ringCell(g, layer, cx, cy, &idx, &tile, &coll) != 2) continue;
    (*checked)++;
    uint16_t v = g->snes->ppu->vram[(bbl_peek16(g, ringLayers[layer] + 0x30) + idx) & 0x7fff];
    bool ok = v == tile && (layer != 0 || bbl_peek(g, 0x7F7000 + idx) == coll);
    if(!ok && bad < 5) printf("  mismatch L%d cell %d,%d vram %04X want %04X coll %02X want %02X\n", layer, cx, cy, v, tile,
                              layer == 0 ? bbl_peek(g, 0x7F7000 + idx) : 0, coll);
    if(!ok) bad++;
  }
  return bad;
}

int bbl_ringDebug;   // probe: print why columns are redrawn
int bbl_noRing, bbl_noObj;   // probe: switch the ring repair / object range off
int bbl_objDebug;            // probe: log early spawns

// The camera jumped by whole rings (the train: past a stretch of track the game
// moves the camera 1024 px up into another part of the map without a redraw,
// the ring cells staying where they are on screen, and streams the new part in
// from the right). What the original view had streamed around the camera
// stays on screen as the game intends: those cells now stand for the matching
// cells of the new position. Cells only widescreen had are redrawn from there.
static void ringJump(Bbl* g, int layer, int prevX, int prevY, int jx, int jy) {
  int lines = g->snes->ppu->frameOverscan ? 239 : 224;
  int x0 = (prevX - 0x40) >> 3, x1 = (prevX + 0x140) >> 3;
  int y0 = (prevY - 0x40) >> 3, y1 = (prevY + lines + 0x40) >> 3;
  int kept = 0;
  for(int i = 0; i < 64 * 64; i++) {
    uint32_t o = g->ringOwner[layer][i];
    if(o == 0xFFFFFFFFu) continue;
    int ox = (int16_t)(o >> 16), oy = (int16_t)(o & 0xFFFF);
    if(ox < x0 || ox > x1 || oy < y0 || oy > y1) continue;
    g->ringOwner[layer][i] = ringKey(ox + (jx >> 3), oy + (jy >> 3));
    g->ringKept[layer][i] = 1;
    kept++;
  }
  if(bbl_ringDebug) printf("  ring L%d jump %d,%d kept %d cells\n", layer, jx, jy, kept);
}

// 2-3's collapse: the train there is one car in BG1's map. Its controller
// (type $8130, state at +$12, cars to go at +$3E) hands each car on when BG2's
// x reaches $11E: the car goes over to BG2 to fall, and BG1's camera goes back
// 256 px, so the same car of the map stands next in line on the right
// ($83:DC33; after the last, forward 256 px: no car). The widescreen picture
// showed the right of the train empty until then, the next car appearing all
// at once. While one is to come (states 10, 11 with two or more to go) the
// right shows what the jump will bring there, the map 256 px to the left,
// where the level has nothing: tiles and collision, it is the car Buster may
// step onto a moment later anyway. (Only above the bridge's band, line 153 on.)
// 2-3 before the collapse: from BG2's camera's jump onto the car it will drop
// (at its map's left edge; the train's hills are at y 1024 and on) until the
// cut down to the bridge. The game draws that car into BG2 a column at a
// time: at 4:3 its own place stays off the left of the screen, and the car
// comes in from the right through the ring's wrap. Left of the 4:3 screen the
// widescreen picture showed it under the train (the ring repair drew it all
// at once): there BG2 is not drawn, and not repaired at all (inside the 4:3
// screen too: the original shows only what the game has drawn).
static bool bg2CarWait(Bbl* g) {
  return bbl_peek16(g, 0x7E0068) == 1 && bbl_peek16(g, 0x7E0094) == 2 && bbl_peek16(g, 0x7E1912) >= 1024 &&
         bbl_peek16(g, 0x7E1952) < 1024 && bbl_peek16(g, 0x7E1942) < 512;
}

static uint16_t collapseCtrl(Bbl* g) {
  if(bbl_peek16(g, 0x7E0068) != 1 || bbl_peek16(g, 0x7E0094) != 2 || bbl_peek16(g, 0x7E1912) >= 256) return 0;
  for(uint16_t slot = 0x0880 + 0x60; slot + 0x60 <= 0x2000; slot += 0x60)
    if((bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x10)) & 0x7FFF) == 0x0130) return slot;
  return 0;
}

static bool collapseNextCar(Bbl* g) {
  uint16_t slot = collapseCtrl(g);
  if(!slot) return false;
  int state = bbl_peek(g, 0x7E0000 | (uint16_t)(slot + 0x12));
  return (state == 10 || state == 11) && bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x3E)) >= 2;
}

// ...and while BG1's car is one at all (after the last hand-off it is not)
static bool collapseCarScene(Bbl* g) {
  uint16_t slot = collapseCtrl(g);
  return slot && bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x3E)) >= 1;
}

// one cell of BG1 right of the 4:3 screen while a car is to come: true when
// it is the next car's (drawn if not yet)
static bool collapseCarCell(Bbl* g, int cx, int cy, int ri) {
  int idx; uint16_t tile, vtile; uint8_t coll, vcoll;
  if(ringCell(g, 0, cx, cy, &idx, &tile, &coll) != 2 || (tile & 0x3FF)) return false;       // the level's own
  if(ringCell(g, 0, cx - 32, cy, &idx, &vtile, &vcoll) != 2 || !(vtile & 0x3FF)) return false;
  uint32_t key = ringKey(cx - 32, cy);
  if(g->ringOwner[0][ri] == key) return true;
  g->snes->ppu->vram[(bbl_peek16(g, ringLayers[0] + 0x30) + ri) & 0x7fff] = vtile;
  bbl_poke(g, 0x7F7000 + ri, vcoll);
  g->ringOwner[0][ri] = key;
  g->ringKept[0][ri] = 0;
  g->ringRepaired++;
  return true;
}

// The map has only the car's first 24 columns: the rest would be right of the
// 4:3 screen until the car goes over to BG2, whose map holds the whole car (32
// columns from its left edge, one row lower; the car in BG1's map starts on a
// screen's edge). Right of the 4:3 screen a cell of the car's last 8 columns
// is drawn from BG2's (true when it is, drawn if not yet), where that row of
// the map matches BG2's; solid like its mirror image at the car's front. So
// the train does not end short of the next car, then get coupled to it all at
// once at the hand-off. (The next car's rear too while it is shown.)
static bool collapseCarRear(Bbl* g, int cx, int cy, int ri, bool nextCar) {
  int k = cx & 31, idx;
  uint16_t tile, t;
  uint8_t coll, c;
  if(k < 24) return false;
  if(ringCell(g, 0, cx, cy, &idx, &tile, &coll) != 2 || (tile & 0x3FF)) return false;   // the level's own
  uint32_t key = ringKey(cx, cy) | 0x40000000u;   // (no cell's)
  if(g->ringOwner[0][ri] == key) return true;
  Ppu* ppu = g->snes->ppu;
  if(ppu->bgLayer[0].tileAdr != ppu->bgLayer[1].tileAdr) return false;   // (the same tiles)
  uint16_t* vram = ppu->vram;
  uint16_t bg2 = bbl_peek16(g, ringLayers[1] + 0x30);
  uint16_t w = vram[(bg2 + ringIndex(k, cy + 1)) & 0x7fff];
  if(!(w & 0x3FF)) return false;
  for(int n = 0; n < (nextCar ? 2 : 1); n++) {
    int base = (cx & ~31) - n * 32;
    bool any = false;
    int j = 0;
    for(; j < 24; j++) {
      if(ringCell(g, 0, base + j, cy, &idx, &t, &c) != 2) break;
      uint16_t b = vram[(bg2 + ringIndex(j, cy + 1)) & 0x7fff];
      if(!(b & 0x3FF)) b = 0;
      if(!(t & 0x3FF)) t = 0;
      if(t != b) break;
      if(t) any = true;
    }
    if(j < 24 || !any) continue;
    if(ringCell(g, 0, base + 31 - k, cy, &idx, &t, &c) != 2) continue;
    vram[(bbl_peek16(g, ringLayers[0] + 0x30) + ri) & 0x7fff] = w;
    bbl_poke(g, 0x7F7000 + ri, c);
    g->ringOwner[0][ri] = key;
    g->ringKept[0][ri] = 0;
    g->ringRepaired++;
    return true;
  }
  return false;
}

// before a frame: redraws the cells of the streamed range and the visible
// picture that hold another part of the level. Cells the game drew itself are
// left alone (objects change some of them: platforms, animations), and a layer
// whose picture does not match the level data here is not touched at all.
static uint8_t ringWrote[64 * 64];

static void ringRepair(Bbl* g) {
  // (what the game itself wrote into a ring during the last frame is what it
  // wants there: the level's own columns, and tiles objects draw, like 2-3's
  // bridge built column by column after the camera jump, which the repair
  // took for leftovers; only kept for one frame, a cell's place changes)
  bool fresh = g->ringRepairFrame + 1 == g->frameCount;
  g->ringRepairFrame = g->frameCount;
  if(!g->ringReady || (bbl_noRing & 1)) { memset(g->ringGameWrote, 0, sizeof(g->ringGameWrote)); return; }
  // (right after a state load the per-line scroll record is from before it:
  // learning the picture waits a frame, or a band scrolled on its own counts
  // as level)
  if(g->ringLinesStale) { memset(g->ringGameWrote, 0, sizeof(g->ringGameWrote)); return; }
  Ppu* ppu = g->snes->ppu;
  int e = g->extraCols;
  int lines = ppu->frameOverscan ? 239 : 224;
  for(int layer = 0; layer < 2; layer++) {
    memcpy(ringWrote, g->ringGameWrote[layer], sizeof(ringWrote));
    memset(g->ringGameWrote[layer], 0, sizeof(ringWrote));
    if(!fresh || (bbl_noRing & 4)) memset(ringWrote, 0, sizeof(ringWrote));
    if(!g->ringSeen[layer]) continue;   // not streamed by the game (static background)
    uint32_t s = ringLayers[layer];
    if(bbl_peek(g, s + 0x32) != 0) continue;
    int camX = (int16_t)bbl_peek16(g, s + 0x02), camY = (int16_t)bbl_peek16(g, s + 0x12);
    // 2-3's collapse hands a car on by moving BG1's camera 256 px, and its
    // lines show that two frames later (the band's scroll goes out by HDMA
    // from a table built the frame before): for the frame between, what is
    // on screen is still the old view, the cells it shows (the next car shown
    // early, see collapseNextCar) left as they are
    bool handoffLag = layer == 0 && g->ringCamFrame[0] == g->frameCount && camY < 256 && abs(abs(camX - g->ringCamX[0]) - 256) <= 16 &&
                      bbl_peek16(g, 0x7E0068) == 1 && bbl_peek16(g, 0x7E0094) == 2;
    if(g->ringCamFrame[layer] == g->frameCount && !g->ringAdopt[layer] && !(bbl_noRing & 2)) {
      // since the last frame: a jump by whole rings (plus a little scrolling)?
      int jx = camX - g->ringCamX[layer], jy = camY - g->ringCamY[layer];
      int rx = (jx + (jx >= 0 ? 256 : -256)) / 512 * 512, ry = (jy + (jy >= 0 ? 256 : -256)) / 512 * 512;
      if((rx || ry) && abs(jx - rx) <= 16 && abs(jy - ry) <= 16) ringJump(g, layer, g->ringCamX[layer], g->ringCamY[layer], rx, ry);
    }
    g->ringCamX[layer] = camX;
    g->ringCamY[layer] = camY;
    g->ringCamFrame[layer] = g->frameCount + 1;
    if(layer == 1 && bg2CarWait(g)) continue;
    int shift = ppu->viewShift[layer];
    int vx0 = camX + shift - e, vx1 = camX + shift + 256 + e;      // visible picture
    int x0 = camX - (layer ? g->stream2Left : g->streamLeft);      // streamed range
    int x1 = camX + (layer ? g->stream2Right : g->streamRight);
    if(vx0 < x0) x0 = vx0;
    if(vx1 > x1) x1 = vx1;
    if(x1 - x0 > 504) {                                             // one ring, no aliasing
      int mid = (vx0 + vx1) / 2;
      x0 = mid - 252;
      x1 = mid + 252;
    }
    int cx0 = x0 >> 3, cx1 = (x1 - 1) >> 3;
    int cy0 = (camY - 0x40) >> 3, cy1 = (camY + lines + 0x40) >> 3;
    if(cy0 < 0) cy0 = 0;
    if(cx0 < 0) cx0 = 0;
    // Lines scrolled away from the camera in the middle of the frame show
    // something else than the level at camera positions: 2-3's collapsing
    // bridge is a band at the bottom of BG1 moved by the bridge's own position
    // ($7EC084), the game building track ahead and clearing it behind. Rows
    // shown on such lines are left alone (redrawing them from the level put
    // track back into the gap and moved the bridge's collision around).
    // Remembered for a second: the band's scroll passes the camera's now and
    // then. (Only a split frame counts: after a camera jump every line is off.)
    bool lineOn[PPU_BUF_H], anyOn = false;
    for(int l = 0; l < lines; l++) {
      int dh = ((ppu->lineHScroll[l][layer] - camX + 512) & 1023) - 512;
      int dv = ((ppu->lineVScroll[l][layer] - camY + 512) & 1023) - 512;
      lineOn[l] = dh >= -16 && dh <= 16 && dv >= -16 && dv <= 16;
      anyOn |= lineOn[l];
    }
    bool rowOff[64] = {false};
    for(int l = 0; l < lines; l++) {
      if(anyOn && !lineOn[l] && !(bbl_noRing & 8)) g->ringLineOff[layer][l] = g->frameCount + 1;
      uint32_t o = g->ringLineOff[layer][l];
      if(!o || g->frameCount + 1 - o >= 60) continue;
      int r = ((camY + l) >> 3) - cy0;
      if(r >= 0 && r < 64) rowOff[r] = true;
      // the margin rows beyond the picture go with the line next to them
      if(l == 0) for(r = 0; r < ((camY >> 3) - cy0) && r < 64; r++) rowOff[r] = true;
      if(l == lines - 1) for(r = ((camY + lines - 1) >> 3) - cy0 + 1; r <= cy1 - cy0 && r < 64; r++) rowOff[r] = true;
    }
    if(cy1 - cy0 >= 64) cy1 = cy0 + 63;
    // cells the game wrote hold what it wants there now (even on frames the
    // repair itself skips below)
    for(int cx = cx0; cx <= cx1; cx++) {
      for(int cy = cy0; cy <= cy1; cy++) {
        int ri = ringIndex(cx, cy);
        if(ringWrote[ri]) { g->ringOwner[layer][ri] = ringKey(cx, cy); g->ringKept[layer][ri] = 0; }
      }
    }
    if(handoffLag) continue;
    if(g->ringAdopt[layer]) {
      // after a state load nothing is known: tell from the picture which cells
      // hold what the level has there, which hold the part of the level that
      // shares their ring cell (stale), and which an object has changed
      g->ringAdopt[layer] = false;
      int total = 0, match = 0;
      uint16_t vbase = bbl_peek16(g, s + 0x30);
      for(int cx = cx0; cx <= cx1; cx++) {
        for(int cy = cy0; cy <= cy1; cy++) {
          int idx; uint16_t tile; uint8_t coll;
          if(ringCell(g, layer, cx, cy, &idx, &tile, &coll) != 2) continue;
          // (rows shown on lines scrolled on their own hold what the game put
          // there: taken as they are, not learnt from; 1-4's kitchen keeps its
          // conveyor chains in rows below the picture and shows them on bands,
          // left unclaimed they were redrawn from the level later)
          if(rowOff[cy - cy0]) { g->ringOwner[layer][idx] = ringKey(cx, cy); continue; }
          uint16_t v = ppu->vram[(vbase + idx) & 0x7fff];
          uint8_t c = layer == 0 ? bbl_peek(g, 0x7F7000 + idx) : coll;
          total++;
          uint32_t owner = ringKey(cx, cy);
          if(v == tile && c == coll) match++;
          else {
            // every place in the map that shares this ring cell
            int mw = (bbl_peek(g, s + 0x25) + 1) * 32, mh = (bbl_peek(g, s + 0x24) + 1) * 32;
            bool found = false;
            for(int ay = cy & 63; ay < mh && !found; ay += 64) {
              for(int ax = cx & 63; ax < mw && !found; ax += 64) {
                int ai; uint16_t at; uint8_t ac;
                if((ax == cx && ay == cy) || ringCell(g, layer, ax, ay, &ai, &at, &ac) != 2) continue;
                if(at == v && (layer != 0 || ac == c) && (at != tile || ac != coll)) {
                  owner = ringKey(ax, ay); found = true;
                  if(bbl_ringDebug > 1) printf("  adopt L%d cell %d,%d holds %d,%d\n", layer, cx, cy, ax, ay);
                }
              }
            }
            if(v == tile) match++;
            // (inside what the game itself streams, the picture is the game's:
            // after a camera jump without a redraw, like the train's, another
            // part of the level stays on screen on purpose; shown like after
            // a live jump, see ringJump)
            if(found && cx * 8 + 8 > camX - 0x40 && cx * 8 < camX + 0x140) {
              owner = ringKey(cx, cy);
              g->ringKept[layer][ringIndex(cx, cy)] = 1;
            }
          }
          g->ringOwner[layer][ringIndex(cx, cy)] = owner;
        }
      }
      if(total == 0 || match * 2 < total) { g->ringSeen[layer] = false; continue; }  // not level data here
    }
    // does the level data describe what the game drew on this layer? Each
    // sampled cell is compared with what it is recorded to hold, wherever that
    // is (after a jump, like the lights going on in the café, 512 px up onto
    // the same screens without a redraw, no cell holds its new position yet)
    int samples = 0, bad = 0;
    for(int cx = vx0 >> 3; cx <= (vx1 - 1) >> 3; cx += 5) {
      for(int cy = camY >> 3; cy <= (camY + lines - 1) >> 3; cy += 4) {
        if(cx < 0 || cy < 0 || (cy >= cy0 && cy <= cy1 && rowOff[cy - cy0])) continue;
        uint32_t o = g->ringOwner[layer][ringIndex(cx, cy)];
        if(o == 0xFFFFFFFFu) continue;
        int idx; uint16_t tile; uint8_t coll;
        if(ringCell(g, layer, (int16_t)(o >> 16), (int16_t)(o & 0xFFFF), &idx, &tile, &coll) != 2) continue;
        samples++;
        if(ppu->vram[(bbl_peek16(g, s + 0x30) + idx) & 0x7fff] != tile) bad++;
      }
    }
    if(samples < 8 || bad * 4 > samples) continue;
    int fixed = 0;
    int audit = cx0 + (int)(g->frameCount % (uint32_t)(cx1 - cx0 + 1));   // the column audited this frame
    bool nextCar = layer == 0 && collapseNextCar(g), carScene = layer == 0 && collapseCarScene(g);
    for(int cx = cx0; cx <= cx1; cx++) {
      for(int cy = cy0; cy <= cy1; cy++) {
        int ri = ringIndex(cx, cy);
        if(rowOff[cy - cy0]) continue;
        if(nextCar && cx * 8 - camX >= 264 && cx * 8 - camX < 256 + 2 * e + 16 && cy * 8 + 8 <= camY + 152 && collapseCarCell(g, cx, cy, ri)) continue;
        if(carScene && cx * 8 - camX >= 256 && cx * 8 - camX < 256 + 2 * e + 16 && cy * 8 + 8 <= camY + 152 && collapseCarRear(g, cx, cy, ri, nextCar)) continue;
        // (what a camera jump left on screen is shown where the game shows it,
        // inside its 4:3 view and to its right, where it streams on; left of
        // it the original never shows it: 2-3's train stood on in mid-air
        // after the bridge had fallen away under the real one)
        // (an audit, one column a frame: a cell recorded as holding its level
        // cell but showing the level from 512 px away is stale, whatever put
        // it out of step (2-5's locomotive boss showed the tender with a
        // column of the level ahead, the repair trusting the record); what
        // the game draws itself or leaves on purpose matches neither)
        if(cx == audit && !(bbl_noRing & 16) && g->ringOwner[layer][ri] == ringKey(cx, cy) && !g->ringKept[layer][ri]) {
          int idx, ai; uint16_t tile, at; uint8_t coll, ac;
          uint16_t v = ppu->vram[(bbl_peek16(g, s + 0x30) + ringIndex(cx, cy)) & 0x7fff];
          if(ringCell(g, layer, cx, cy, &idx, &tile, &coll) == 2 && v != tile) {
            for(int d = -64; d <= 64; d += 128) {
              if(cx + d < 0 || ringCell(g, layer, cx + d, cy, &ai, &at, &ac) != 2 || at != v) continue;
              if(bbl_ringDebug) printf("  audit L%d cell %d,%d holds %d,%d\n", layer, cx, cy, cx + d, cy);
              g->ringOwner[layer][ri] = ringKey(cx + d, cy);
              break;
            }
          }
        }
        // (the tender's filled-in cells, see tenderFill: the game streams them in empty)
        if(layer == 0 && g->ringOwner[0][ri] == ringKey(cx, cy) && !g->ringKept[0][ri] &&
           ppu->vram[(bbl_peek16(g, s + 0x30) + ri) & 0x7fff] == 0 && tenderFill(g, cx, cy)) g->ringOwner[0][ri] = 0xFFFFFFFFu;
        if(g->ringOwner[layer][ri] == ringKey(cx, cy) && !(g->ringKept[layer][ri] && cx * 8 + 8 <= camX)) continue;
        if(!screenExists(g, s, cx >> 5, cy >> 5) && !trainVoid(g, s)) continue;
        if(bbl_ringDebug > 1) {
          uint32_t o = g->ringOwner[layer][ri];
          printf("  redraw L%d cell %d,%d (screen x %d) owner %d,%d f=%u\n", layer, cx, cy, cx * 8 - (camX + shift), (int16_t)(o >> 16), (int16_t)(o & 0xFFFF), g->frameCount);
        }
        ringDrawCell(g, layer, cx, cy);
        fixed++;
      }
    }
    if(fixed) {
      g->ringRepaired += fixed;
      if(bbl_ringDebug) printf("  ring L%d f=%u cam=%d,%d shift=%d cells %d\n", layer, g->frameCount, camX, camY, shift, fixed);
    }
  }
}

// 2-5's handcar chase starts with BG1 switched to its own map and the camera's
// high bytes cleared ($83:E3C8: 5120,768 -> 0,256), on the same ring cells: the
// locomotive's last picture stays, and the chase map streams in as the camera
// moves on. The original's ring then holds the level up to 320 px right of the
// camera, where it streams (sky ahead of the locomotive). With the extra width
// shown behind Buster, the ring reached only 272 px ahead, the rest holding the
// level from 512 px back, and a piece of the boiler slid past in mid-air.
// Those columns are drawn here, before the map changes; the chase itself (4:3,
// see scene43) streams like the original.
static void chaseSwitch(Bbl* g) {
  if(g->extraCols <= 0 || !g->ringReady || bbl_peek16(g, 0x7E0068) != 1 || bbl_peek16(g, 0x7E0094) != 4) return;
  uint32_t s = ringLayers[0];
  int camX = (int16_t)bbl_peek16(g, s + 0x02), camY = (int16_t)bbl_peek16(g, s + 0x12);
  int cy0 = (camY - 0x40) >> 3;
  if(cy0 < 0) cy0 = 0;
  // (not rows the game draws itself: on screen they differ from the level,
  // like the rails at the bottom, in a palette of their own)
  uint16_t vbase = bbl_peek16(g, s + 0x30);
  for(int cy = cy0; cy < cy0 + 0x31; cy++) {
    int differ = 0;
    for(int k = 0; k < 8; k++) {
      int sx = (camX + 16 + k * 28) >> 3, idx; uint16_t tile; uint8_t coll;
      if(ringCell(g, 0, sx, cy, &idx, &tile, &coll) == 2 && g->snes->ppu->vram[(vbase + idx) & 0x7fff] != tile) differ++;
    }
    if(differ >= 4) continue;
    for(int cx = (camX + 256) >> 3; cx <= (camX + 0x140) >> 3; cx++)
      if(g->ringOwner[0][ringIndex(cx, cy)] != ringKey(cx, cy)) ringDrawCell(g, 0, cx, cy);
  }
}

// ---- widescreen object range -----------------------------------------------
//
// Enemies and items come from per-screen lists (bank $82, one table per area
// at $81:F704 + (stage * 8 + area) * 2; entries: x (bit 15 = skip, bit 14 =
// kept while $08B0 = 4), y, type, slot, parameter). The game spawns an entry
// when its spawn column or row (camera +$130 / -$30, $8F:F0A6) crosses it, into
// the fixed slot the entry names, and every object removes itself once it is
// more than 64 px outside the 256 px screen ($83:82C7 and inline copies). In
// widescreen objects would pop in and vanish inside the extra columns, so:
//  - the removal checks' x range follows the visible window (+64 px)
//  - entries entering the widened window are spawned early, into free slots
//    only, when the widened spawn columns (or the game's spawn row) cross them
//  - an object alive only thanks to the wider range gives its slot up when the
//    game's own spawner wants it, so whatever happens within the original range
//    happens exactly as it did (entries share slots with objects further away
//    than the original range, the Brick Corridor exit for one).

// ADC #$0040 / CMP #$0180 on an object's x (LDA $08,X before it)
static const uint32_t objCheckSites[] = { 0x82C911, 0x8382D5, 0x8382EC, 0x8396C0, 0x83B13B, 0x84CC8D };
// LDA $10,X in the spawners: is the entry's slot in use?
static const uint32_t objSlotSites[] = { 0x8FF3D9, 0x8FF406, 0x8FF444, 0x8FF474, 0x8FF4BA, 0x8FF4F2,
                                         0x8FF814, 0x8FF84A, 0x8FF888 };
#define OBJ_SPAWNER 0x8FF0A6
// the object loop: JSL $838000 runs the object at X ($82:C94B), LDX $F6 after it
#define OBJ_RUN 0x82C94B
#define OBJ_RAN 0x82C94F
// the fields the game's removal ($83:82F3) clears
static const uint8_t objFields[] = { 0x06, 0x08, 0x0A, 0x0C, 0x00, 0x02, 0x04, 0x2A, 0x10, 0x12, 0x14, 0x18, 0x1A,
  0x1C, 0x20, 0x22, 0x24, 0x26, 0x1E, 0x28, 0x2C, 0x32, 0x30, 0x2E, 0x16, 0x36, 0x3A, 0x38, 0x3C, 0x3E, 0x40,
  0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C, 0x5E };

// Objects kept alive (or spawned) outside the original range break an
// assumption the game makes: parent objects spawn children straight into
// fixed slots, relying on whatever was there having been removed by then
// (6-4 and 3-3 crashed: a child inherited another object's state). The slot
// hand-back below and the takeover watch after it keep those slots clean.
static const bool objRangeEnabled = true;
static bool objSupported = true;

static uint16_t rom16(Bbl* g, uint32_t adr) {
  return bbl_romRead(g, adr) | (bbl_romRead(g, adr + 1) << 8);
}

static void objWriteWatch(void* ctx, uint16_t adr, uint8_t val);

static void objInstallHooks(Bbl* g) {
  if(!objRangeEnabled) { objSupported = false; return; }
  // the removal checks must be the expected instructions
  for(int i = 0; i < (int)(sizeof(objCheckSites) / sizeof(objCheckSites[0])); i++) {
    uint32_t a = objCheckSites[i];
    if(bbl_romRead(g, a) != 0x69 || rom16(g, a + 1) != 0x0040 || bbl_romRead(g, a + 3) != 0xC9 ||
       rom16(g, a + 4) != 0x0180) objSupported = false;
  }
  for(int i = 0; i < (int)(sizeof(objSlotSites) / sizeof(objSlotSites[0])); i++) {
    // LDA $10,X / BNE +3 / JSR range / TYA
    if(rom16(g, objSlotSites[i]) != 0x10B5 || rom16(g, objSlotSites[i] + 2) != 0x03D0 ||
       bbl_romRead(g, objSlotSites[i] + 4) != 0x20 || bbl_romRead(g, objSlotSites[i] + 7) != 0x98) objSupported = false;
  }
  if(!objSupported) return;
  for(int i = 0; i < (int)(sizeof(objCheckSites) / sizeof(objCheckSites[0])); i++) cpu_setHook(g->snes->cpu, objCheckSites[i]);
  for(int i = 0; i < (int)(sizeof(objSlotSites) / sizeof(objSlotSites[0])); i++) {
    cpu_setHook(g->snes->cpu, objSlotSites[i]);
    cpu_setHook(g->snes->cpu, objSlotSites[i] + 7);
  }
  cpu_setHook(g->snes->cpu, OBJ_SPAWNER);
  if(bbl_romRead(g, OBJ_RUN) == 0x22 && rom16(g, OBJ_RUN + 1) == 0x8000 && bbl_romRead(g, OBJ_RAN) == 0xA6) {
    cpu_setHook(g->snes->cpu, OBJ_RUN);
    cpu_setHook(g->snes->cpu, OBJ_RAN);
  }
  for(int i = 0; i < (int)(sizeof(drawRangeSites) / sizeof(drawRangeSites[0])); i++)
    if(drawRangeObject[i]) cpu_setHook(g->snes->cpu, drawRangeSites[i]);
  for(int i = 0; i < (int)(sizeof(winSites) / sizeof(winSites[0])); i++)
    if(winSites[i].removal && winSiteOk(g, &winSites[i])) cpu_setHook(g->snes->cpu, winSites[i].site);
  g->snes->wramWatchFn = objWriteWatch;
  g->snes->wramWatchCtx = g;
  g->snes->wramWatchFrom = 0x0880;
}

// objects may live in screen x [lo, hi): the streamed range
static void objSetRange(Bbl* g, bool wide, int lo, int hi) {
  if(!objSupported) return;
  if(bbl_noObj & 1) wide = false;
  if(!wide) {
    if(g->objWide) {
      for(int i = 0; i < (int)(sizeof(objCheckSites) / sizeof(objCheckSites[0])); i++) {
        for(int k = 1; k <= 5; k++) if(k != 3) bbl_romRestore(g, objCheckSites[i] + k);
      }
      g->objDespawnA = g->objDespawnC = 0;
    }
    g->objWide = false;
    g->objEdgeValid = false;
    memset(g->objLineage, 0, sizeof(g->objLineage));
    memset(g->objTaint, 0, sizeof(g->objTaint));
    return;
  }
  if(lo > -64) lo = -64;
  if(hi < 320) hi = 320;
  g->objWide = true;
  g->objLo = lo;
  g->objHi = hi;
  int a = -lo, c = hi - lo;
  if(a == g->objDespawnA && c == g->objDespawnC) return;
  g->objDespawnA = a;
  g->objDespawnC = c;
  for(int i = 0; i < (int)(sizeof(objCheckSites) / sizeof(objCheckSites[0])); i++) {
    patch16(g, objCheckSites[i] + 1, (uint16_t)a);
    patch16(g, objCheckSites[i] + 4, (uint16_t)c);
  }
}

static bool objOutsideOriginal(Bbl* g, uint16_t slot) {
  int x = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x08));
  return x < -64 || x >= 320;
}

static void objClear(Bbl* g, uint16_t slot) {
  g->objTaint[slot >> 1] = 0;
  g->objGen[slot >> 1]++;
  g->objParent[slot >> 1] = 0;
  for(int i = 0; i < (int)sizeof(objFields); i++) bbl_poke16(g, 0x7E0000 | (uint16_t)(slot + objFields[i]), 0);
}

// Parent objects spawn children straight into fixed slots (84:BC08, 84:BC42
// in 3-3), writing only some fields, sometimes the type last: in the original
// that slot is empty by then. The wider range changes who is around: an
// object alive (or spawned) only thanks to it can spawn children the original
// never has. So such objects and their children count as extra, another
// object's code writing into an extra object's slot is remembered, and once
// it writes the slot's type the slot is cleared (as the original removal
// would have left it) and those writes are applied again.
static bool objExtFlag(Bbl* g, uint16_t slot) {
  uint32_t f = g->objExtFrame[slot >> 1];
  return f != 0 && f >= g->frameCount;   // (0: never; the frame count restarts at 0 on a load)
}

// (tainted: spawned early or kept beyond the original range at some point
// since it was spawned; its timing is not the original's, even once back
// inside the original range: 3-3's $84:BC42 spawned into a $0270 in state 3)
static bool objExtra(Bbl* g, uint16_t slot) {
  return objExtFlag(g, slot) || g->objLineage[slot >> 1] || g->objTaint[slot >> 1];
}

// an object slot address ($0880 + n * $60)
static bool objSlotAdr(uint16_t a) {
  return a >= 0x0880 && a < 0x2000 && (a - 0x0880) % 0x60 == 0;
}

static void objWriteWatch(void* ctx, uint16_t adr, uint8_t val) {
  Bbl* g = ctx;
  if(!g->objWide || adr < 0x0880) return;
  uint16_t slot = (uint16_t)(0x0880 + (adr - 0x0880) / 0x60 * 0x60);
  if(slot + 0x60 > 0x2000) return;
  int off = adr - slot;
  bool typeField = off == 0x10 || off == 0x11;
  // The writer is the object X points at (spawners setting up a child point
  // D at it, so D tells nothing; code with X not at a slot, like the graphics
  // decompressor working at $1C00 through D, is nobody's child). Except that
  // an object's code may point X at the slot it fills in ($83:ACE4 spawns
  // thirteen $8116s that way): then the writer is the object being run.
  uint16_t who = g->snes->cpu->x & 0x1FFF;
  if(who == slot && g->objRunning && g->objRunning != slot) who = g->objRunning;
  if(who == slot) {
    // the object itself, or the game's list spawner (after the object loop,
    // X at the slot): removing itself ends its marks, and so does a new
    // object in an empty slot (spawned when the original would)
    if(off == 0x10 && (val == 0 || (g->snes->ram[slot + 0x10] | g->snes->ram[slot + 0x11]) == 0)) {
      g->objLineage[slot >> 1] = g->objTaint[slot >> 1] = 0;
      if(val != 0) g->objExtFrame[slot >> 1] = 0;
      g->objGen[slot >> 1]++;
      g->objParent[slot >> 1] = 0;
    }
    return;
  }
  bool extra = objExtra(g, slot);
  if(typeField) {
    if(off == 0x11) return;   // (second byte of the same write)
    // the type it already has, written by the object that spawned what is
    // there: a parent refreshing its child (3-1's $83:F02A rewrites four of
    // them every frame), not a new object. (Another object of the same type
    // spawning there is new: 2-1's barrel piles all use the same slots, and a
    // leftover barrel of the last pile kept its "drawn" flag.)
    if(val != 0 && val == g->snes->ram[slot + 0x10] &&
       (!objSlotAdr(who) || (g->objParent[slot >> 1] == who && g->objParentGen[slot >> 1] == g->objGen[who >> 1]))) return;
    // an extra object spawning into a slot another object is still in: its
    // timing is not the original's, where that slot would be free (3-3's
    // $84:BC42 put a $814C into a $0270 in state 3)
    if(!extra && val != 0 && g->snes->ram[slot + 0x10] != 0 && objSlotAdr(who) && objExtra(g, who)) extra = true;
    if(extra) {
      if(bbl_objDebug) printf("  takeover f=%u slot=%04X old=%04X new=%02X?? who=%04X pc=%06X running=%04X\n", g->frameCount, slot,
        bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x10)), val, who, (g->snes->cpu->k << 16) | g->snes->cpu->pc, g->objRunning);
      objClear(g, slot);
      g->objExtFrame[slot >> 1] = 0;
      g->objTakeovers++;
      for(int i = 0; i < g->objPendN; i++) {
        ObjPend* e = &g->objPend[i];
        if(e->frame == g->frameCount && e->who == who && e->adr >= slot && e->adr < slot + 0x60 && e->adr != adr)
          bbl_poke(g, 0x7E0000 | e->adr, e->val);
      }
      g->objPendN = 0;
    }
    // an emptied slot carries nothing on; a child of an extra object is extra too
    if(val == 0) g->objLineage[slot >> 1] = g->objTaint[slot >> 1] = 0;
    else g->objLineage[slot >> 1] = objSlotAdr(who) && objExtra(g, who);
    g->objGen[slot >> 1]++;
    g->objParent[slot >> 1] = (val != 0 && objSlotAdr(who)) ? who : 0;
    g->objParentGen[slot >> 1] = objSlotAdr(who) ? g->objGen[who >> 1] : 0;
    return;
  }
  if(!extra) return;
  if(g->objPendN > 0 && g->objPend[0].frame != g->frameCount) g->objPendN = 0;
  if(g->objPendN < (int)(sizeof(g->objPend) / sizeof(g->objPend[0]))) {
    ObjPend* e = &g->objPend[g->objPendN++];
    e->adr = adr; e->val = val; e->who = who; e->frame = g->frameCount;
  }
}

// spawns the list entries of one screen that pass `test`
typedef struct { int kind, at, cam, camY, lo, hi; bool onlyFlagged; } ObjScan;

// List entries never spawned early: they set a scene going as soon as they
// run. 2-1's lasso bandit ($8104, $83:AC71) spawns the second bandit and the
// camera's auto-scroll controller ($806C, $19D4), which early started the
// duel's scroll 56 px before the original (and again once the game spawned
// him itself).
static bool objNoEarly(uint16_t type) {
  return (type & 0x7FFF) == 0x0104;
}

static void objScanScreen(Bbl* g, const ObjScan* sc, uint16_t table, int w, int h, int col, int row) {
  if(col < 0 || row < 0 || col >= w || row >= h) return;
  uint16_t y = rom16(g, 0x820000 | (uint16_t)(table + (row * w + col) * 2));
  for(int n = 0; n < 128; n++, y += 10) {
    uint16_t ex = rom16(g, 0x820000 | y);
    if(ex == 0xFFFF) break;
    if(ex & 0x8000) continue;
    if(sc->onlyFlagged && !(ex & 0x4000)) continue;
    int wx = ex & 0x3FFF, wy = rom16(g, 0x820000 | (uint16_t)(y + 2));
    if(sc->kind == 0 && (wx & 0x3FF0) != sc->at) continue;                   // column
    if(sc->kind == 1 && (wy & 0xFFF0) != sc->at) continue;                   // row
    int xr = wx - sc->cam, yr = wy - sc->camY;
    if(bbl_objDebug) printf("  objscan f=%u kind=%d entry %d,%d type=%04X rel %d,%d range %d..%d slot=%04X used=%d\n", g->frameCount, sc->kind, wx, wy,
      rom16(g, 0x820000 | (uint16_t)(y + 4)), xr, yr, sc->lo, sc->hi, rom16(g, 0x820000 | (uint16_t)(y + 6)),
      bbl_peek16(g, 0x7E0000 | (uint16_t)((rom16(g, 0x820000 | (uint16_t)(y + 6)) & 0x1FFF) + 0x10)) != 0);
    if(yr < -64 || yr >= 320) continue;
    if(xr >= -64 && xr < 320) continue;                                      // the game's own range
    if(xr < sc->lo || xr >= sc->hi) continue;
    uint16_t slot = rom16(g, 0x820000 | (uint16_t)(y + 6));
    if(slot >= 0x2000 || bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x10)) != 0) continue;
    if(objNoEarly(rom16(g, 0x820000 | (uint16_t)(y + 4)))) continue;
    bbl_poke16(g, 0x7E0000 | (uint16_t)(slot + 0x08), (uint16_t)xr);
    bbl_poke16(g, 0x7E0000 | (uint16_t)(slot + 0x0C), (uint16_t)yr);
    bbl_poke16(g, 0x7E0000 | (uint16_t)(slot + 0x10), rom16(g, 0x820000 | (uint16_t)(y + 4)));
    bbl_poke16(g, 0x7E0000 | (uint16_t)(slot + 0x5E), rom16(g, 0x820000 | (uint16_t)(y + 8)));
    g->objExtFrame[slot >> 1] = g->frameCount + 1;
    g->objTaint[slot >> 1] = 1;
    g->objEntry[slot >> 1] = y;
    g->objEarly++;
  }
}

// runs at the start of the game's spawner, after the camera has moved
static void objSpawnEarly(Bbl* g) {
  if(!g->objWide || g->sweepFreeze || (bbl_noObj & 2)) return;
  int stage = bbl_peek16(g, 0x7E0068), area = bbl_peek16(g, 0x7E0094);
  if(stage > 7 || area > 7) return;
  ObjScan sc;
  sc.cam = (int16_t)bbl_peek16(g, 0x7E1902);
  sc.camY = (int16_t)bbl_peek16(g, 0x7E1912);
  sc.lo = g->objLo;
  sc.hi = g->objHi;
  sc.onlyFlagged = bbl_peek16(g, 0x7E08B0) == 4;
  int w = bbl_peek(g, 0x7E1925) + 1, h = bbl_peek(g, 0x7E1924) + 1;
  uint16_t table = rom16(g, 0x810000 | (uint16_t)(0xF704 + (stage * 8 + area) * 2));
  if(table < 0x8000) return;
  // widened spawn columns, 16 px inside the streamed range
  int right = (sc.cam + sc.hi - 16) & ~15, left = (sc.cam + sc.lo + 16) & ~15;
  int origRight = (sc.cam & ~15) + 0x130, origLeft = (sc.cam & ~15) - 0x30;
  int row = sc.camY & ~15;
  int row0 = (sc.camY - 64) >> 8, row1 = (sc.camY + 319) >> 8;
  if(bbl_objDebug && (!g->objEdgeValid || abs(right - g->objEdgeR) > 64 || abs(left - g->objEdgeL) > 64 || abs(row - g->objRow) > 64))
    printf("  objscan restart f=%u cam=%d,%d range %d..%d\n", g->frameCount, sc.cam, sc.camY, sc.lo, sc.hi);
  if(!g->objEdgeValid || abs(right - g->objEdgeR) > 64 || abs(left - g->objEdgeL) > 64 || abs(row - g->objRow) > 64) {
    // (re)start: everything in the extra range once
    sc.kind = 2;
    int col0 = (sc.cam + sc.lo) >> 8, col1 = (sc.cam + sc.hi - 1) >> 8;
    for(int r = row0; r <= row1; r++) {
      for(int c = col0; c <= col1; c++) objScanScreen(g, &sc, table, w, h, c, r);
    }
  } else {
    sc.kind = 0;
    for(int c = g->objEdgeR + 16; c <= right; c += 16) {
      if(c <= origRight) continue;
      sc.at = c;
      for(int r = row0; r <= row1; r++) objScanScreen(g, &sc, table, w, h, c >> 8, r);
    }
    for(int c = g->objEdgeL - 16; c >= left; c -= 16) {
      if(c >= origLeft) continue;
      sc.at = c;
      for(int r = row0; r <= row1; r++) objScanScreen(g, &sc, table, w, h, c >> 8, r);
    }
    if(row != g->objRow) {
      // the row the game scans; it only spawns what lies within its own range
      sc.kind = 1;
      sc.at = row > g->objRow ? row + 0x130 : row - 0x30;
      int col0 = (sc.cam + sc.lo) >> 8, col1 = (sc.cam + sc.hi - 1) >> 8;
      for(int c = col0; c <= col1; c++) objScanScreen(g, &sc, table, w, h, c, sc.at >> 8);
    }
  }
  g->objEdgeR = right;
  g->objEdgeL = left;
  g->objRow = row;
  g->objEdgeValid = true;
}

static void onCpuHook(void* ctx, uint32_t pc) {
  Bbl* g = ctx;
  if(pc == 0x80C77C) { onLoadSweep(g); return; }
  if(pc == PLAY_MOVE) { playMove(g); return; }
  if(pc == PLAY_EDGE) { playEdge(g); return; }
  if(pc == playPixelSites[0] || pc == playPixelSites[1] || pc == playPixelSites[2]) { playPixel(g); return; }
  if(pc == CAM_HOLD) { camHold(g); return; }
  if(pc == GFX_RESTORE) { gfxRestoreCheck(g); return; }
  if(pc == GFX_LOAD) { gfxLoadCheck(g); return; }
  if(pc == PILE_CLEAR) { pileClear(g); return; }
  if(pc == PILE_START) { if(g->pileCount) g->snes->cpu->a += g->pileFirst; return; }
  if(pc == PILE_COUNT) { if(g->pileCount) g->snes->cpu->a = g->pileCount; return; }
  if(pc == CHASE_SWITCH) { chaseSwitch(g); return; }
  if(pc == CAM_SHAKE) {
    // (the camera before the shake: the view is fitted to that, see updateView)
    if(!g->shakeFrame || g->frameCount + 1 - g->shakeFrame > 8) g->shakeRest = (int16_t)bbl_peek16(g, 0x7E1902);
    g->shakeFrame = g->frameCount + 1;
    return;
  }
  if(pc == SPR_START) { g->sprExt = false; memset(g->oamFixedNext, 0, sizeof(g->oamFixedNext)); return; }
  if(pc == SPR_FULL) { sprFull(g); return; }
  if(pc == SPR_COPY) { sprCopy(g); return; }
  if(pc == SPR_RANGE) { sprRange(g); return; }
  if(pc == SPR_ATTR || pc == SPR_ATTR2) {
    // (a piece from the second sprite table, where the sets the graphics trigger swaps go)
    if(g->drawnPend && (g->snes->cpu->a & 1)) {
      g->objHiTab[(g->drawnPend >> 5) & 0xFF] = 1;
      g->objHiType[(g->drawnPend >> 5) & 0xFF] = bbl_peek16(g, 0x7E0000 | (uint16_t)(g->drawnPend + 0x10));
      gfxDrawn(g, g->drawnPend, g->drawnPendX);
      g->drawnPend = 0;
    }
    // (the piece goes out through the WRAM port: its attribute byte is the
    // last of its four in the sprite table at $01E0)
    uint32_t ra = g->snes->ramAdr;
    if(g->sprFixedSlot && !g->sprExt && ra >= 0x01E0 && ra < 0x01E0 + 512) g->oamFixedNext[(ra - 0x01E0) >> 2] = 1;
    if(pc == SPR_ATTR) sprAttr(g);
    return;
  }
  if(pc == SPR_ENTRY) {
    uint16_t d = g->snes->cpu->x;
    // (football draws its scoreboard marker and the pass box's ball dot from
    // the screen-position slots below Buster's, $0460 and $04C0)
    g->sprFixedSlot = d < 0x0880 && bbl_peek(g, 0x7E0068) == 3;
    g->drawnPend = d != 0x0880 ? d : 0;   // (not Buster; see SPR_ATTR)
    g->drawnPendX = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(d + 0x08));
    if(g->drawnPend && g->gfxWide && gfxHide(g, d)) {
      g->drawnPend = 0;
      g->snes->cpu->pc = 0x91F6;   // (CLC / RTL: the routine's exit for an object without pieces)
    }
    return;
  }
  if(pc == SPR_OBJ) {
    // (the object's x and y, copied for drawing: the net's?)
    uint16_t d = g->snes->cpu->x;
    g->sprNet = g->netSlot && bbl_peek16(g, 0x7E0000 | (uint16_t)(d + 0x08)) == bbl_peek16(g, 0x7E0000 | (uint16_t)(g->netSlot + 0x08)) &&
                bbl_peek16(g, 0x7E0000 | (uint16_t)(d + 0x0C)) == bbl_peek16(g, 0x7E0000 | (uint16_t)(g->netSlot + 0x0C));
    // (the net started again right of the screen: its pieces go to the ghost
    // of the one before (sprRange), at that one's height (SPR_Y); on the frame
    // of the restart itself netUpdate has not seen it yet)
    g->sprNetGhostY = false;
    if(g->sprNet && g->objAutoRight) {
      int nx = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(g->netSlot + 0x08));
      if(!g->netGhost && g->netPrevValid && g->netPrevX <= -96 && nx >= 384) { g->netGhost = true; g->netGhostX = nx - 512; g->netGhostY = g->netPrevY; }
      g->sprNetGhostY = g->netGhost && nx >= 384;
    }
    if(g->ghostSpawn && bbl_peek16(g, 0x7E0000 | (uint16_t)(g->ghostSlot + 0x10)) == 0x0122 &&
       bbl_peek16(g, 0x7E0000 | (uint16_t)(d + 0x08)) == bbl_peek16(g, 0x7E0000 | (uint16_t)(g->ghostSlot + 0x08)) &&
       bbl_peek16(g, 0x7E0000 | (uint16_t)(d + 0x0C)) == bbl_peek16(g, 0x7E0000 | (uint16_t)(g->ghostSlot + 0x0C))) g->ghostSeen = g->frameCount + 1;
    return;
  }
  if(pc == SPR_Y) { if(g->sprNetGhostY) g->snes->cpu->a = (uint16_t)g->netGhostY; return; }
  if(pc == OPT_TABLE) { optTable(g); return; }
  if(pc == RING_COLUMN || pc == RING_ROW) { if(g->ringReady) ringOnWriter(g, pc); return; }
  if(pc == RING_COUNT) {
    if(g->colShort && g->snes->cpu->x == 0x1900) g->snes->cpu->a = 0x001B;
    g->colShort = false;
    return;
  }
  if(pc == OBJ_SPAWNER) {
    if(g->objWide) objSpawnEarly(g);
    return;
  }
  if(pc == OBJ_RUN) { g->objRunning = g->snes->cpu->x & 0x1FFF; return; }
  if(pc == OBJ_RAN) { g->objRunning = 0; return; }
  if(!g->objWide) return;
  uint16_t slot = g->snes->cpu->x & 0x1FFF;
  for(int i = 0; i < (int)(sizeof(objCheckSites) / sizeof(objCheckSites[0])); i++) {
    if(pc != objCheckSites[i]) continue;
    // an object checking whether it is still in range (x in A)
    // Where the camera scrolls right by itself, one past the original range on
    // the right and still moving on right has been removed in the original, by
    // this check: kept, 2-3's bandit walked off the end of the carriage roof
    // and on through the air (the game hides that just off the 4:3 screen).
    // Removed by the game's own check then (A made far outside).
    int x = (int16_t)g->snes->cpu->a;
    bool movingRight = g->objCheckFrame[slot >> 1] == g->frameCount && x > g->objCheckX[slot >> 1];
    g->objCheckX[slot >> 1] = (int16_t)x;
    g->objCheckFrame[slot >> 1] = g->frameCount + 1;
    if(g->objAutoRight && slot != 0x0880 && x >= 320 && movingRight) {
      g->snes->cpu->a = 0x7000;
      return;
    }
    g->objExtFrame[slot >> 1] = objOutsideOriginal(g, slot) ? g->frameCount + 1 : 0;
    if(g->objExtFrame[slot >> 1]) g->objTaint[slot >> 1] = 1;
    return;
  }
  for(int i = 0; i < (int)(sizeof(winSites) / sizeof(winSites[0])); i++) {
    if(pc != winSites[i].site || !winSites[i].removal) continue;
    // an object checking itself against the original window (x in A)
    int x = (int16_t)g->snes->cpu->a;
    bool outside = x >= winSites[i].hi || x < winSites[i].lo - 0x10000;
    g->objExtFrame[slot >> 1] = outside ? g->frameCount + 1 : 0;
    if(outside) g->objTaint[slot >> 1] = 1;
    return;
  }
  for(int i = 0; i < (int)(sizeof(drawRangeSites) / sizeof(drawRangeSites[0])); i++) {
    if(pc != drawRangeSites[i] || !drawRangeObject[i]) continue;
    // an object checking itself against a screen range of its own: beyond
    // the original range it is extra
    int add = rom16(g, pc + 1), cmp = rom16(g, pc + 4);
    int x = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x08));
    if(x < -add || x >= cmp - add) { g->objExtFrame[slot >> 1] = g->frameCount + 1; g->objTaint[slot >> 1] = 1; }
    return;
  }
  for(int i = 0; i < (int)(sizeof(objSlotSites) / sizeof(objSlotSites[0])); i++) {
    if(pc == objSlotSites[i] + 7) {
      // after the entry's range check: if the game spawned into the slot the
      // hidden object is gone (the original would have removed it by then),
      // otherwise it comes back
      if(g->objHoldSlot) {
        uint16_t s = g->objHoldSlot;
        g->objHoldSlot = 0;
        uint16_t type = bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x10));
        if(type != 0) {
          uint16_t x = bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x08)), y = bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x0C));
          uint16_t par = bbl_peek16(g, 0x7E0000 | (uint16_t)(s + 0x5E));
          objClear(g, s);
          bbl_poke16(g, 0x7E0000 | (uint16_t)(s + 0x08), x);
          bbl_poke16(g, 0x7E0000 | (uint16_t)(s + 0x0C), y);
          bbl_poke16(g, 0x7E0000 | (uint16_t)(s + 0x10), type);
          bbl_poke16(g, 0x7E0000 | (uint16_t)(s + 0x5E), par);
          g->objExtFrame[s >> 1] = 0;
          g->objLineage[s >> 1] = 0;
          g->objEvicted++;
        }
        else bbl_poke16(g, 0x7E0000 | (uint16_t)(s + 0x10), g->objHoldType);
      }
      return;
    }
    if(pc != objSlotSites[i]) continue;
    // The game looks at an entry's slot before checking whether the entry is
    // in range. If the slot only holds an object the wider range kept alive
    // (or spawned early), hide it for the check: should the game spawn the
    // entry, that object goes (see above), as it would have in the original.
    // (Not when it is this very entry, spawned early: its slot is in use.)
    g->objHoldSlot = 0;
    uint16_t type = bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x10));
    if(!(bbl_noObj & 4) && type != 0 && objExtFlag(g, slot) && objOutsideOriginal(g, slot) &&
       !(g->snes->cpu->y == g->objEntry[slot >> 1] && type == rom16(g, 0x820000 | (uint16_t)(g->snes->cpu->y + 4)))) {
      g->objHoldSlot = slot;
      g->objHoldType = type;
      bbl_poke16(g, 0x7E0000 | (uint16_t)(slot + 0x10), 0);
    }
    return;
  }
}

// The extent check needs the view's rows to exist; while the camera crosses a
// row with void screens it can fail for a moment, and falling back would make
// the picture jump (the train's desert backdrop juddered). So each layer keeps
// the last extent it had in this area.
static bool layerExtentKept(Bbl* g, int layer, uint32_t cam, int* left, int* right) {
  if(layerExtent(g, cam, left, right)) {
    g->extLeft[layer] = *left;
    g->extRight[layer] = *right;
    g->extValid[layer] = true;
    return true;
  }
  if(g->extValid[layer]) {
    *left = g->extLeft[layer];
    *right = g->extRight[layer];
    return true;
  }
  // nothing known yet (a stage start drops Buster in through a void row):
  // the whole map, which is what the extent usually turns out to be
  if(bbl_peek16(g, cam + 0x26) < 0x8000) return false;
  *left = 0;
  *right = (bbl_peek(g, cam + 0x25) + 1) * 256;
  return true;
}

// Moves a layer's view shift towards its target by at most 4 px a frame, so a
// change in the level's layout (a row of void screens coming into view, the
// train stage's drop-in) pans instead of jumping. Snaps after a camera jump.
static int easeShift(Bbl* g, int layer, int target, bool snap) {
  if(snap || !g->shownValid) g->shownShift[layer] = target;
  else {
    int d = target - g->shownShift[layer];
    g->shownShift[layer] += d > 4 ? 4 : (d < -4 ? -4 : d);
  }
  return g->shownShift[layer];
}

// ---- Buster's screen bounds on auto-scrolling stages ------------------------
//
// Buster's x ($0887-$0889: fraction, pixel, page) is relative to the camera.
// Where the camera moves by itself ($19D4 bit 0: the train, the asteroid belt)
// or stays put while the game scrolls BG1 (the airship), the movement code
// ($80:E6A2) keeps him between 18 and 238, and $80:CFA2 ends his life once he
// is 16 px outside the screen (pushed off the left edge). In widescreen both
// follow the widescreen picture, within the level's own extent. Elsewhere the
// camera follows Buster, and the level edges already match the picture.
// (hooks: PLAY_MOVE, PLAY_EDGE at the top)

static bool playAirship(Bbl* g) {
  return bbl_peek16(g, 0x7E0068) == 4 && bbl_peek16(g, 0x7E0094) == 3;
}

// wide: widescreen gameplay on such a stage; lo/hi: picture edges in Buster's
// coordinates
int bbl_noPlay;   // probe: keep Buster's original screen bounds
int bbl_playFull; // probe: no original right limit where the camera scrolls right

static void playUpdate(Bbl* g, bool wide, int lo, int hi) {
  if(bbl_noPlay) wide = false;
  if(lo > 0) lo = 0;
  if(hi < 256) hi = 256;
  wide = wide && (lo < 0 || hi > 256);
  if(!wide && (g->playWide || g->playLoaded) && hud_inGameplay(g)) {
    // back to the original range (widescreen switched off, or a state from
    // widescreen loaded): bring Buster onto the 4:3 screen instead of letting
    // the edge check end his life
    int x = (int16_t)bbl_peek16(g, 0x7E0888);
    if(x < 0 || x >= 256) {
      bbl_poke(g, 0x7E0887, 0);
      bbl_poke16(g, 0x7E0888, x < 0 ? 0x12 : 0xEE);
    }
  }
  g->playLoaded = false;
  g->playWide = wide;
  g->playLo = lo;
  g->playHi = hi;
}

static void playMove(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  uint8_t* ram = g->snes->ram;
  uint16_t delta = ram[0x1A] | ram[0x1B] << 8;
  if(!g->playWide || cpu->x != 0x0880 || cpu->mf || delta >= 0x8000) return;
  // carry: direction (from LSR $C2); same rules as the original, wider limits
  int32_t pos = ram[0x887] | ram[0x888] << 8 | (int8_t)ram[0x889] << 16;
  int32_t np;
  if(cpu->c) {
    int32_t lim = (g->playHi - 18) * 256;
    np = pos + delta;
    if(np >= lim) np = (ram[0x19D4] & 2) ? pos : lim;
  } else {
    int32_t lim = (g->playLo + 18) * 256;
    np = pos - delta;
    if(np < lim) np = (ram[0x19D4] & 1) ? pos : lim;
  }
  ram[0x887] = np & 0xff;
  ram[0x888] = (np >> 8) & 0xff;
  ram[0x889] = (np >> 16) & 0xff;
  cpu->a = np & 0xffff;
  cpu->pc = 0xE6D4; // SEP #$20 / RTS
}

static void playEdge(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  if(!g->playWide || cpu->x != 0x0880 || cpu->mf) return;
  int x = (int16_t)(g->snes->ram[0x888] | g->snes->ram[0x889] << 8);
  // (nothing pushes Buster off the right; the limit there only moves inwards
  // when the picture shifts, which must not cost a life)
  bool inside = x >= g->playLo - 16 && x < 256 + 2 * g->extraCols + 16;
  cpu->pc = inside ? 0xCFBA : 0xCFAC;
}

// About to store Buster's new screen x pixel (A, 8-bit) without its page:
// the bounce off a wall ($80:DA4C, -8/+8) and the snaps to the tile edge
// ($80:E41F, $80:E5E6, -0..7), fine within the original bounds 18-238. Where
// he may leave the 4:3 screen (left of it on auto-scrolling stages) the pixel
// crossed a page with the page left as it was: found with the right limit
// lifted, bounced back from x 263 to "255" he stood at 511 and lost a life
// (2-3's boxcar, dashing into its side); across x 0 likewise. The page
// follows the pixel.
static void playPixel(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  uint8_t* ram = g->snes->ram;
  if(cpu->x != 0x0880 || !cpu->mf) return;
  int old = ram[0x888] | ram[0x889] << 8;
  int nx = old + (int8_t)(uint8_t)((cpu->a & 0xff) - ram[0x888]);
  ram[0x889] = (uint8_t)(nx >> 8);
}

// 2-3's car controller holds BG1's camera at $0800 while BG2's is below $60
// ($83:DC2C), waiting for the next car. The auto-scroll moves it on half a
// pixel a frame, and the objects have been moved back with the camera of the
// layer Buster stands on ($08BD bit 7: BG2; $80:C559, x on screen = x in the
// level - camera) before the controller sets it back, which moves nothing on
// again: every other frame they are 1 px further back. At 4:3 Buster stands on
// BG2's car then; right of the 4:3 screen he can stand on BG1's next car, and
// was pushed backwards. There he is moved on again (the enemies riding that
// car drift as in the original).
static void camHold(Bbl* g) {
  uint8_t* ram = g->snes->ram;
  int d = bbl_peek16(g, 0x7E1902) - (g->snes->cpu->a & 0xFFFF);
  if(d < 1 || d > 2 || g->extraCols == 0 || (ram[0x8BD] & 0x80)) return;
  int x = (int16_t)(ram[0x888] | ram[0x889] << 8);
  if(x <= 238) return;
  x += d;
  ram[0x888] = (uint8_t)x;
  ram[0x889] = (uint8_t)(x >> 8);
}

// 2-1's barrel pile (six $4108 parts, $1240-$1420, three of them drawing a
// 32 x 32 barrel into BG1 once in range, $83:B2D2): once the last part has
// gone out of range, $83:B290 clears the pile's picture, 11 x 4 cells at a
// fixed VRAM address ($81:C8A5 by the part's +$5E), and removes the parts. At
// 4:3 those ring cells still hold the pile then, off screen. Widescreen
// streams further ahead and removes objects at the streamed range, so by then
// the columns at one end of the pile can hold the level 512 px on (arriving
// in 2-1 from stage 1, the Town Hall's porch lost a 32 x 32 block; walking
// back past the pile, the scenery 512 px back would), and those at the other
// end can still be on screen (a barrel vanished at the picture's edge). Only
// the columns that still hold the pile, off screen, are cleared: the routine's
// rows are narrowed to them (its start address and count), or it is skipped.
static void pileClear(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  g->pileCount = 0;
  if(g->extraCols == 0 || !g->ringReady) return;
  uint16_t slot = cpu->x;
  int param = bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x5E));
  uint32_t tab = ((uint32_t)cpu->db << 16) | (uint16_t)(0xC8A5 + ((param >> 8) << 1));
  uint16_t vram = (uint16_t)(bbl_romRead(g, tab) | bbl_romRead(g, tab + 1) << 8);
  uint16_t base = bbl_peek16(g, ringLayers[0] + 0x30);
  int camX = (int16_t)bbl_peek16(g, ringLayers[0] + 0x02);
  int pileCx = (camX + (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x08))) >> 3;
  int vx0 = camX + g->snes->ppu->viewShift[0] - g->extraCols - 16, vx1 = vx0 + 256 + 2 * g->extraCols + 32;
  bool clear[11];
  for(int k = 0; k < 11; k++) {
    int idx = (uint16_t)(vram + k - base) & 0xFFF;
    // the pile's map column in this ring column (the part stands in the pile)
    int rc = (idx & 31) + ((idx & 0x400) ? 32 : 0);
    int cx = pileCx + ((rc - pileCx) % 64 + 96) % 64 - 32;
    clear[k] = cx * 8 + 8 <= vx0 || cx * 8 >= vx1;
    for(int r = 0; r < 4 && clear[k]; r++) {
      uint32_t o = g->ringOwner[0][(idx + r * 32) & 0xFFF];
      if(o != 0xFFFFFFFFu && (int16_t)(o >> 16) != cx) clear[k] = false;
    }
  }
  // the longest run of them (one run, bar a camera jump)
  int first = 0, count = 0;
  for(int k = 0; k < 11; k++) {
    int n = 0;
    while(k + n < 11 && clear[k + n]) n++;
    if(n > count) { first = k; count = n; }
    k += n;
  }
  if(bbl_ringDebug) printf("  pile clear f=%u cam=%d pile col %d: %d columns from %d of 11\n", g->frameCount, camX, pileCx, count, first);
  if(count == 11) return;                        // all of it, as at 4:3
  if(count == 0) { cpu->pc = 0xB2CE; return; }   // none (its RTS)
  g->pileFirst = (uint8_t)first;
  g->pileCount = (uint8_t)count;
}

// ---- more sprites in widescreen --------------------------------------------
//
// The game builds its sprite table at $01E0 each frame (WRAM port set at
// $80:90C1: 128 entries, then their high bits at $03E0 through the pointer at
// $02) and stops once that pointer wraps ($80:92AA INC $02 / BEQ). The NMI
// copies the table to OAM ($80:81BD). In widescreen the extra columns' sprites
// can fill it before the ones in the middle get a slot (the mansion: tops of
// the pillars missing). So when it fills, the finished table is set aside and
// the game carries on into a second one; at the copy the second one goes to
// the PPU's extension (drawn after the 128) and the first back into place.
static void sprFull(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  uint8_t* ram = g->snes->ram;
  if(!cpu->z || !cpu->mf || g->sprExt || g->snes->ppu->extraLeft == 0) return;
  memcpy(g->sprBase, ram + 0x01E0, sizeof(g->sprBase));
  for(int i = 0; i < 128; i++) ram[0x01E0 + i * 4 + 1] = 0xF0;   // unused: off screen
  memset(ram + 0x03E0, 0, 32);                                     // (and small)
  g->snes->ramAdr = 0x01E0;
  ram[0x02] = 0xE0;
  cpu->z = false;   // not full: go on
  g->sprExt = true;
}

// Offset-per-tile columns (mode 2: 2-4's rising and falling logs, the
// carriage): $81:FF30 fills a table of 32 vertical offsets, one per 8 px
// screen column, from a ring of 64 column heights at $1D80 (starting at
// byte Y, flags in $00); it reaches the picture through VRAM a frame later.
// The widescreen columns get their values from the same ring, but only the
// columns the game itself shows with offsets at some point: the ring repeats
// every 512 px, and the wider picture could otherwise show a log's height on
// what lies 512 px away (the carriage after the logs, whose columns the game
// only shows once the log animator has cleared the table at camera x $1970,
// $83:E036). So from the left, columns left of what the game has shown since
// the offsets came on get none, and on the right, columns past what it shows
// before the animator stops.
int bbl_optDelay = 1;   // probe: frames until the table reaches the picture
int bbl_optForce = -1;  // probe: force the widescreen columns' value

static void optTable(Bbl* g) {
  Ppu* ppu = g->snes->ppu;
  const uint8_t* ram = g->snes->ram;
  uint16_t start = g->snes->cpu->y & 0x7E, flags = ram[0] | (ram[1] << 8);
  int cam = bbl_peek16(g, 0x7E0000 | (uint16_t)((g->snes->cpu->x & 0x1FFF) + 2));
  uint32_t area = ringAreaKey(g);
  if(area != g->optArea || !g->optCamValid) { g->optArea = area; g->optMinCam = g->optMaxCam = cam; g->optCamValid = true; }
  if(cam < g->optMinCam) g->optMinCam = cam;
  if(cam > g->optMaxCam) g->optMaxCam = cam;
  int limit = g->optMaxCam + 1;
  if(bbl_peek(g, 0x7E0068) == 1 && bbl_peek(g, 0x7E0094) == 3 && bbl_romRead(g, 0x83E036) == 0xC9) limit = rom16(g, 0x83E037);
  int base = cam >> 3, minCol = (g->optMinCam >> 3) + 1, maxCol = ((limit - 1) >> 3) + 32;
  if(bbl_optDelay == 0) g->optPendValid = false;
  if(g->optPendValid && g->optFrame + 1 >= g->frameCount) {
    memcpy(ppu->optExtV, g->optPend, sizeof(ppu->optExtV));
    ppu->optExtFirst = -24;
    ppu->optExtCount = 80;
    ppu->optExtValid = true;
  }
  // (those get the ring's flat value, its most common one: the game's own
  // flat columns use it, and no offset at all would put them 8 px lower)
  uint16_t flat = 0; int best = 0;
  for(int a = 0; a < 64; a++) {
    uint16_t va = ram[0x1D80 + a * 2] | (ram[0x1D81 + a * 2] << 8);
    int n = 0;
    for(int b = 0; b < 64; b++) n += (ram[0x1D80 + b * 2] | (ram[0x1D81 + b * 2] << 8)) == va;
    if(n > best) { best = n; flat = va; }
  }
  for(int i = 0; i < 80; i++) {
    int c = i - 24, o = (start + c * 2) & 0x7E;
    uint16_t v = ram[0x1D80 + o] | (ram[0x1D81 + o] << 8);
    if(base + c < minCol || base + c > maxCol) v = flat;
    g->optPend[i] = (uint16_t)(((v - 1) & 0x3FF) | flags);
    if(bbl_optForce >= 0) g->optPend[i] = (uint16_t)bbl_optForce;
  }
  g->optPendValid = true;
  g->optFrame = g->frameCount;
  if(bbl_optDelay == 0) {
    memcpy(ppu->optExtV, g->optPend, sizeof(ppu->optExtV));
    ppu->optExtFirst = -24;
    ppu->optExtCount = 80;
    ppu->optExtValid = true;
  }
}

// 2-3's mail nets go round every 512 px: x = 400 - counter ($7EC092, +4 a
// frame, from 0 again at 512, held at 0 a while before some nets), and their
// poles are one endless BG3 strip scrolled by the same counter (h = counter -
// 4). The game ends a net just off the left of the 4:3 screen (x -112), starts
// it again at 400 and holds the strip, and at the first net the strip's copy
// stands at -112: all of that in view with the picture moved left
// (autoRight). There the left of it is drawn apart: after a net has gone round
// its pole and net go on to the left at the net's speed (a ghost of them, the
// net's pieces written there, see sprRange), otherwise nothing is shown there
// until the net comes back. (Left of the 4:3 screen the net passes the
// passenger car, which the original never shows it in front of: it is drawn
// in front, see sprAttr.)
#define NET_SPLIT (-96)   // game x left of which the strip's copy stands
static void netUpdate(Bbl* g) {
  Ppu* ppu = g->snes->ppu;
  if(!g->netSlot) { g->netGhost = g->netPrevValid = false; return; }
  int x = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(g->netSlot + 0x08));
  // (at the height of the net it goes on for: nets hang at different heights
  // on the same poles; already started while the new one's pieces were drawn
  // on the frame of the restart, see SPR_OBJ)
  if(!g->netGhost && g->netPrevValid && g->netPrevX <= -96 && x >= 384) { g->netGhost = true; g->netGhostX = x - 512; g->netGhostY = g->netPrevY; }
  else if(g->netGhost) g->netGhostX -= 4;
  if(g->netGhost && g->netGhostX < ppu->viewShift[0] - g->extraCols - 96) g->netGhost = false;
  g->netPrevX = x;
  g->netPrevY = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(g->netSlot + 0x0C));
  g->netPrevValid = true;
  if(!g->netGhost && x < 256) return;   // the net is on its way across: all as the game draws it
  ppu->bg3Split = true;
  ppu->bg3SplitTop = 49;                // (below the HUD)
  ppu->bg3SplitX = NET_SPLIT;
  ppu->bg3SplitHide = !g->netGhost;
  ppu->bg3SplitH = 396 - g->netGhostX;
}

// A sprite piece right of the picture (X in A at the range helper, $80:FF80;
// whose pieces they are noted at $80:91FA, X its slot, where its x and y are
// about to be copied to $08 and $0A for its pieces): the net's,
// while it waits or starts again right of the screen, go to its ghost (the
// 9-bit X put left of the screen).
static void sprRange(Bbl* g) {
  Cpu* cpu = g->snes->cpu;
  if(!g->sprNet || !g->objAutoRight || !g->netGhost) return;
  int gx = g->netGhostX + ((int16_t)cpu->a - (int16_t)bbl_peek16(g, 0x7E0008));
  if(gx < -256 || gx >= 0) return;
  cpu->a = (uint16_t)gx;
  cpu->pc = 0xFF8D;
}

// the net's pieces' attributes about to be written (A, 8-bit): in front of
// the backgrounds
int bbl_netPrio = 1;   // probe: 0 keeps the net's own priority
static void sprAttr(Bbl* g) {
  if(g->sprNet && g->objAutoRight && bbl_netPrio) g->snes->cpu->a |= 0x30;
}

static void banditGhostDraw(Bbl* g);
static void banditGhostUpdate(Bbl* g);

// Football's scoreboard marker (on the BALL ON bar) and the pass box's ball
// dot are sprites placed on the 4:3 screen, like the scoreboard and the box
// (BG3 and windows, see windowFixed): while a pass moves the view they stay
// with them, the dot left the box before. Their sprite table entries are noted
// as the sprite pass writes them (SPR_ATTR) and taken over when the table goes
// to the PPU.
static int sprFixedX(void* ctx, int index, int x, int y) {
  Bbl* g = ctx;
  (void)y;
  return index < 128 && g->oamFixedShown[index] ? x + g->snes->ppu->spriteShift : x;
}

static void sprCopy(Bbl* g) {
  Ppu* ppu = g->snes->ppu;
  uint8_t* ram = g->snes->ram;
  memcpy(g->oamFixedShown, g->oamFixedNext, sizeof(g->oamFixedShown));
  if(!g->sprExt) { ppu->extOamCount = 0; banditGhostDraw(g); return; }
  for(int i = 0; i < 128; i++) {
    const uint8_t* e = ram + 0x01E0 + i * 4;
    ppu->extOam[i * 2] = e[0] | (e[1] << 8);
    ppu->extOam[i * 2 + 1] = e[2] | (e[3] << 8);
  }
  memcpy(ppu->extHighOam, ram + 0x03E0, 32);
  memcpy(ram + 0x01E0, g->sprBase, sizeof(g->sprBase));
  ppu->extOamCount = 128;
  g->sprExt = false;
}

// 2-3's collapse: the enemy riding the car (list entry type $0122, at the
// car's front) is spawned when the spawn column, 304 px right of the camera,
// gets to it: on the car coming into the widescreen picture 48 px after the
// 4:3 screen's edge, and on the next car shown early (collapseNextCar) only
// once the hand-off has taken the camera back. It appeared on the car all at
// once. A stand-in (its sprite pieces as the game draws them walking, graphics
// already loaded) stands where it will be until the game's own is there
// (spawned, it is first drawn two frames later): only drawn, in the sprite
// table's extension, behind the game's sprites. Not while its slot is taken:
// it would not come.
static const struct { int8_t dx, dy; uint16_t tile; uint8_t big; } banditPieces[] = {
  { -9, 2, 0x1A2, 0 }, { -1, 1, 0x1B2, 0 }, { 7, 1, 0x1B3, 0 }, { 6, -7, 0x1A3, 0 }, { 11, -2, 0x1BF, 0 },
  { -5, 8, 0x1B7, 1 }, { -5, 0, 0x1A7, 1 }, { -5, -8, 0x186, 1 }, { 6, -40, 0x1CA, 0 }, { -10, -40, 0x1C9, 0 },
  { 6, -32, 0x1A6, 0 }, { -10, -16, 0x1C4, 0 }, { -2, -24, 0x1B5, 1 }, { -10, -32, 0x1A4, 1 },
};

static void banditGhostUpdate(Bbl* g) {
  g->ghostOn = false;
  uint16_t ctrl = collapseCtrl(g);
  if(!ctrl || bbl_peek16(g, 0x7E0000 | (uint16_t)(ctrl + 0x3E)) < 1) { g->ghostPending = false; g->ghostSpawn = 0; return; }
  // the entry: the collapse's last of that type (row 0 of the map)
  uint16_t table = rom16(g, 0x810000 | (uint16_t)(0xF704 + (1 * 8 + 2) * 2));
  int w = bbl_peek(g, 0x7E1925) + 1;
  int ex = -1, ey = 0;
  uint16_t eslot = 0;
  for(int col = 0; col < w; col++) {
    uint16_t y = rom16(g, 0x820000 | (uint16_t)(table + col * 2));
    for(int n = 0; n < 128; n++, y += 10) {
      uint16_t x = rom16(g, 0x820000 | y);
      if(x == 0xFFFF) break;
      if((x & 0x8000) || rom16(g, 0x820000 | (uint16_t)(y + 4)) != 0x0122) continue;
      if((x & 0x3FFF) > ex) { ex = x & 0x3FFF; ey = rom16(g, 0x820000 | (uint16_t)(y + 2)); eslot = rom16(g, 0x820000 | (uint16_t)(y + 6)); }
    }
  }
  if(ex < 0 || eslot >= 0x2000) { g->ghostPending = false; g->ghostSpawn = 0; return; }
  g->ghostSlot = eslot;
  uint16_t type = bbl_peek16(g, 0x7E0000 | (uint16_t)(eslot + 0x10));
  int camX = bbl_peek16(g, 0x7E1902), camY = bbl_peek16(g, 0x7E1912);
  int x, y;
  if(type == 0x0122) {
    // spawned where the stand-in stood, and first drawn two frames later
    if(g->ghostPending) { g->ghostPending = false; g->ghostSpawn = g->frameCount + 1; }
    if(!g->ghostSpawn || g->ghostSeen >= g->ghostSpawn) { g->ghostSpawn = 0; return; }
    x = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(eslot + 0x08));
    y = (int16_t)bbl_peek16(g, 0x7E0000 | (uint16_t)(eslot + 0x0C));
  } else {
    g->ghostSpawn = 0;
    g->ghostPending = false;
    if(type != 0) return;   // (its slot taken: it would not come)
    // spawned once the spawn column (camera + $130) gets to it, once for each
    // time the camera passes it (the hand-off takes the camera back); until
    // then, or on the next car shown early
    if(camX + 0x130 < ex) { x = ex - camX; g->ghostPending = true; }
    else if(collapseNextCar(g)) x = ex + 256 - camX;
    else return;
    y = ey - camY;
  }
  g->ghostX = x;
  g->ghostY = y;
  g->ghostOn = x > 256 - 16 && x < 256 + 2 * g->extraCols + 32;
}

static void banditGhostDraw(Bbl* g) {
  Ppu* ppu = g->snes->ppu;
  int n = (int)(sizeof(banditPieces) / sizeof(banditPieces[0]));
  if(!g->ghostOn || ppu->extOamCount + n > 128) return;
  for(int i = 0; i < n; i++) {
    int k = ppu->extOamCount + i;
    int x = g->ghostX + banditPieces[i].dx, y = g->ghostY + banditPieces[i].dy;
    uint16_t t = banditPieces[i].tile;
    ppu->extOam[k * 2] = (uint16_t)((x & 0xFF) | ((y & 0xFF) << 8));
    ppu->extOam[k * 2 + 1] = (uint16_t)((t & 0xFF) | ((0x28 | (t >> 8)) << 8));   // priority 2, palette 4
    int sh = (k & 3) * 2;
    ppu->extHighOam[k >> 2] = (uint8_t)((ppu->extHighOam[k >> 2] & ~(3 << sh)) | ((((x >> 8) & 1) | (banditPieces[i].big << 1)) << sh));
  }
  ppu->extOamCount += n;
}

int bbl_objAll;   // probe: widen the object range in every area

// objects live and spawn over the whole widescreen picture (every area; the
// probe's objall is kept for testing)
static bool objAreaWide(Bbl* g) {
  (void)g;
  return true;
}

// Football (4-1): on a pass the camera follows the ball and the right edge of
// the 256 px screen holds Buster back until it reaches him (the movement
// limit at x 238, part of how passes work). In widescreen that edge would be
// an invisible wall in the picture, so while Buster is held there the
// picture moves left until it ends there too, showing the play behind him.
static int footballIdeal(Bbl* g, int e) {
  // Buster ($0880) running out for a pass is type $0001; the picture starts
  // moving once he is past x 140, so it has by the time he reaches 238 (a run
  // play has him $0001 for a moment at the start, the kick-off far outside
  // the screen)
  int x = (int16_t)bbl_peek16(g, 0x7E0888);
  g->fbHeld = bbl_peek(g, 0x7E0068) == 3 && bbl_peek16(g, 0x7E0890) == 0x0001 && x >= 140 && x <= 256;
  return g->fbHeld ? -e : 0;
}

// Stages whose camera scrolls to the right by itself ($19D4 bit 0: the train,
// the asteroid belt) are timed for Buster staying within 238 px of it: he
// waits at the right edge while the train brings things to him. Allowed on to
// the widescreen picture's edge he got to them before the game had: in 2-3 he
// walked off the car roof into the gap before the tunnel's mountain had slid
// over it. So there his right limit stays the original one (autoRight). The
// picture stays centred: shifted to put the extra width behind him, what the
// game ends just off the left of the 4:3 screen came into view (2-3's mail
// nets restart at x -112 while their BG3 poles, one endless strip, go on).
// The collapsing bridge shows its extra width ahead instead (see
// collapseClip): nothing behind the train is laid out for being seen there.
// (Left of the 4:3 screen his x goes below 0: see playPixel.)
static bool collapseActive(Bbl* g) {
  return g->collapseSeen && g->frameCount + 1 - g->collapseSeen < 60;
}

// the collapsing bridge's stretch of 2-3 (after the camera's jump 1024 px up,
// until it stops scrolling by itself at the jump onto the mountain): known by
// the camera's height right from the jump, so the view changes with the cut
static bool collapseStretch(Bbl* g) {
  if(collapseActive(g)) return true;
  return bbl_peek16(g, 0x7E0068) == 1 && bbl_peek16(g, 0x7E0094) == 2 && bbl_peek16(g, 0x7E1912) < 256 && (bbl_peek(g, 0x7E19D4) & 1);
}

// Enemies keep the original range while the bridge collapses, from the
// camera's cut down to it (the band is seen only a few frames later: in
// between 2-3's bandit was spawned early, just off the picture's right edge)
static bool collapseRange(Bbl* g) {
  return collapseActive(g) || (collapseStretch(g) && g->camJumpFrame && g->frameCount + 1 - g->camJumpFrame < 60);
}

static bool autoRight(Bbl* g) {
  return (bbl_peek(g, 0x7E19D4) & 1) && bbl_peek(g, 0x7E0068) != 3 && !playAirship(g) && !collapseStretch(g);
}

// With his original right limit, Buster's wall would stand in the middle of
// the widescreen picture's right part: the picture moves left by the extra
// columns instead, so it ends where the 4:3 screen does, as does he (the
// extra width shows the train behind). The collapse shows it ahead.
static int scrollIdeal(Bbl* g, int e) {
  if(bbl_peek(g, 0x7E0068) == 3) return footballIdeal(g, e);
  if(bbl_peek(g, 0x7E19D4) & 1) {
    if(collapseStretch(g)) return e;
    if(autoRight(g)) return -e;
  }
  return 0;
}

static bool scene43(Bbl* g);

static void updateView(Bbl* g, bool active) {
  Ppu* ppu = g->snes->ppu;
  resetView(g);
  ppu->bandRefVScroll = -1;
  {
    // records belong to one area and map
    uint32_t key = ringAreaKey(g);
    if(key != g->ringArea) { ringReset(g); g->ringArea = key; g->extValid[0] = g->extValid[1] = false; g->shownValid = false; }
    if(key != g->playArea) { g->playArea = key; g->playAuto = false; }
    // (once the camera has scrolled by itself in an area, it counts as an
    // auto-scrolling one to its end; not football, see footballIdeal)
    if(hud_inGameplay(g) && (bbl_peek(g, 0x7E19D4) & 3) && bbl_peek(g, 0x7E0068) != 3) g->playAuto = true;
  }
  if(g->sweepFreeze) {
    // done once the camera is back where the sweep started from (or after a while)
    g->shownValid = false;
    if(bbl_peek16(g, 0x7E1902) == g->sweepTarget) g->sweepFreeze = false;
    else if(++g->sweepFrames > 600) { g->sweepFreeze = false; g->sweepTimeouts++; }
    else { applySpriteRange(g, g->extraCols, 0); return; }
  }
  if(!active || g->extraCols == 0) {
    g->parValid = false;
    g->shownValid = false;
    applySpriteRange(g, g->extraCols, 0);
    // (2-5's handcar chase is gameplay shown 4:3, with nothing to repair the
    // ring: it streams as the original does, see chaseSwitch)
    if(g->extraCols > 0 && scene43(g)) applyStreamOriginal(g);
    else if(g->extraCols > 0) applyStreamOffsets(g, g->extraCols, 0);
    objSetRange(g, false, 0, 256);
    applyDrawRanges(g, false);
    applyGfxTrigger(g, false, 0, 0);
    applyWindowSites(g, false, 0);
    applyBridgeBuilder(g, false, 0);
    playUpdate(g, false, 0, 256);
    return;
  }
  updateParallax(g);
  int e = g->extraCols;
  // (signed: 5-6's door shakes the camera to -1, which read as 65535 put the
  // playfield clip 65000 px away and blacked the picture out)
  int c = (int16_t)bbl_peek16(g, 0x7E1902);
  int left, right, shift = 0;
  // some stages (the airship) scroll BG1 themselves instead of through the
  // camera; the camera tells nothing about the picture there
  // (per-line effects like the boss kitchen's conveyor belts scroll a few
  // lines on their own: decide by the majority of lines)
  int lines = ppu->frameOverscan ? 239 : 224, onCamera = 0;
  for(int l = 0; l < lines; l++) {
    int diff = ((ppu->lineHScroll[l][0] - c + 512) & 1023) - 512;
    if(diff >= -16 && diff <= 16) onCamera++;
  }
  // (right after a state load the per-line record is from before it)
  if(g->loadedFrames > 0) { g->loadedFrames--; onCamera = lines; }
  // (and on a camera jump: 2-3's collapse hands each car on by jumping the
  // camera 256 px, and the picture follows a frame or two later (two after
  // lag frames); until then it is where the camera was and fell back to
  // 4:3's place)
  if(g->shownValid && (abs(c - g->shownCam) > 32 || abs(bbl_peek16(g, 0x7E1912) - g->shownCamY) > 32)) { onCamera = lines; g->jumpFromCam = g->shownCam; }
  else if(g->camJumpFrame && g->frameCount + 1 - g->camJumpFrame < 4 && onCamera * 2 < lines) {
    for(int l = 0; l < lines; l++) {
      int diff = ((ppu->lineHScroll[l][0] - g->jumpFromCam + 512) & 1023) - 512;
      if(diff >= -16 && diff <= 16) onCamera++;
    }
  }
  // (the airship's map is one screen, and whenever the game scrolls it back
  // near the camera it would pass for a one-screen room and get side bars)
  if(onCamera * 2 < lines || playAirship(g)) {
    g->shownValid = false;
    applySpriteRange(g, e, 0);
    applyStreamOffsets(g, e, 0);
    objSetRange(g, objAreaWide(g) && !collapseRange(g), -g->streamLeft, g->streamRight);
    applyDrawRanges(g, true);
    applyGfxTrigger(g, true, e, 0);
    applyWindowSites(g, true, 0);
    applyBridgeBuilder(g, true, e);
    playUpdate(g, g->playAuto || playAirship(g), playAirship(g) ? 0 : -e, 256 + e);
    return;
  }
  // (a camera jump is a cut: the view changes with it, also when what it
  // should be is only known a few frames later)
  int cy = (int16_t)bbl_peek16(g, 0x7E1912);
  if(abs(c - g->shownCam) > 32 || abs(cy - g->shownCamY) > 32) { g->camJumpFrame = g->frameCount + 1; g->jumpFromCam = g->shownCam; }
  bool snap = !g->shownValid || (g->camJumpFrame && g->frameCount + 1 - g->camJumpFrame < 4);
  g->shownCam = c;
  g->shownCamY = cy;
  int playLo, playHi;
  // (a screen shake moves the camera a pixel or two back and forth: fitted to
  // the camera from before it, the picture shakes as in 4:3, where fitted to
  // each position a one-screen room stood still)
  int cv = g->shakeFrame && g->frameCount + 1 - g->shakeFrame <= 6 ? g->shakeRest : c;
  if(layerExtentKept(g, 0, 0x7E1900, &left, &right)) {
    shift = easeShift(g, 0, clampShift(fitShift(cv, e, left, right, scrollIdeal(g, e)), e), snap);
    ppu->clipLeft = left - (c + shift);
    ppu->clipRight = right - (c + shift);
    playLo = -e + shift > left - c ? -e + shift : left - c;
    playHi = 256 + e + shift < right - c ? 256 + e + shift : right - c;
    if(autoRight(g) && playHi > 256 && !bbl_playFull) playHi = 256;
  } else {
    shift = easeShift(g, 0, 0, snap);
    // unknown layout: hide what lies beyond the map width
    int width = (bbl_peek(g, 0x7E1925) + 1) * 256;
    ppu->clipLeft = -c;
    ppu->clipRight = width - c;
    if(ppu->clipLeft > 0) ppu->clipLeft = 0;
    if(ppu->clipRight < 256) ppu->clipRight = 256;
    playLo = -e + shift > -c ? -e + shift : -c;
    playHi = 256 + e + shift < width - c ? 256 + e + shift : width - c;
    if(autoRight(g) && playHi > 256 && !bbl_playFull) playHi = 256;
  }
  playUpdate(g, g->playAuto || playAirship(g), playLo, playHi);
  g->objAutoRight = autoRight(g);
  // (the mail net, see sprRange)
  g->netSlot = 0;
  if(g->objAutoRight && bbl_peek16(g, 0x7E0068) == 1) {
    for(uint16_t slot = 0x0880 + 0x60; slot + 0x60 <= 0x2000; slot += 0x60)
      if((bbl_peek16(g, 0x7E0000 | (uint16_t)(slot + 0x10)) & 0x7FFF) == 0x4138) { g->netSlot = slot; break; }   // (bit 15 while right of the screen)
  }
  g->viewShift = shift;
  ppu->viewShift[0] = shift;
  ppu->spriteShift = shift;
  // (football's windows are the scoreboard's dark backing: it stays with the
  // scoreboard when a pass moves the view)
  ppu->windowFixed = bbl_peek(g, 0x7E0068) == 3;
  // background layers: shift by their scroll rate (so parallax stays
  // consistent) and keep them inside their own map as well
  for(int i = 1; i < 3; i++) {
    // (a BG2 that scrolls on its own pans with the sprites: on the train's
    // hills Buster runs on it; a BG3 like that is a foreground: left alone)
    // A distant background pans at its scroll rate, so the scene holds still
    // while the view pans. Layers the game lines things up with pan with BG1,
    // like the 4:3 picture moved sideways: a BG2 that scrolls on its own (the
    // train's hills, which Buster runs on) and a BG3 moving with the camera
    // (the mansion's tallest candle is a BG3 foreground candle with its top
    // drawn into BG1). A BG3 that scrolls on its own is left alone.
    // A BG2 that moves 1:1 with BG1 is lined up with the level as well: 6-3's
    // giant ball is drawn into a one-screen BG2 map and moved by its scroll.
    bool rigid2 = g->parIndep[1] || (g->parRatio[1] > 0.95f && g->parRatio[1] < 1.05f);
    int ideal;
    if(i == 1) ideal = rigid2 ? shift : (int)(g->parRatio[1] * shift + (shift >= 0 ? 0.5f : -0.5f));
    // (2-3's mail net poles are a BG3 strip scrolled by the nets' counter,
    // lined up with the net sprites: with the view moved, it moves too)
    // (football's BG3 is the scoreboard and the RUN/PASS box, fixed to the 4:3
    // screen: moved with a pass's view, the box's leftovers in its map came
    // into the stands, see footballIdeal)
    else ideal = (g->parIndep[2] && !autoRight(g)) || bbl_peek(g, 0x7E0068) == 3 ? 0 : shift;
    int l2, r2;
    // (such a layer, or one that scrolls on its own like the train's desert,
    // is not fitted to its map)
    if(i == 1 && !rigid2 && layerExtentKept(g, 1, 0x7E1940, &l2, &r2)) {
      ideal = clampShift(fitShift(bbl_peek16(g, 0x7E1942), e, l2, r2, ideal), e);
    }
    // (BG2 streams around its shifted window: never more than the margin)
    if(i == 1) ideal = easeShift(g, 1, clampShift(ideal, e), snap);
    ppu->viewShift[i] = ideal;
  }
  g->shownValid = true;
  applySpriteRange(g, e, shift);
  applyStreamOffsets(g, e, shift);
  streamOffsetsFor(e, ppu->viewShift[1], &g->stream2Right, &g->stream2Left);
  applyDrawRanges(g, true);
  applyGfxTrigger(g, true, e, shift);
  // (not on 2-3's collapsing bridge: an enemy come early, the flying lizard,
  // took the sprite graphics the falling timbers use, and they fell as grey
  // scraps; everything there is timed for the original range)
  objSetRange(g, objAreaWide(g) && !collapseRange(g), -g->streamLeft, g->streamRight);
  applyWindowSites(g, true, shift);
  applyBridgeBuilder(g, true, e + shift);
  if(g->objAutoRight) netUpdate(g);
  else g->netGhost = g->netPrevValid = false;
  banditGhostUpdate(g);
  collapseClip(g, shift);
  if(bg2CarWait(g)) ppu->layerClipLeft[1] = -shift;
  ringRepair(g);
}

void bbl_setButtons(Bbl* g, int player, uint16_t mask) {
  if(player == 1) g->snes->input1->currentState = mask;
  else g->snes->input2->currentState = mask;
}

// Gameplay drawn for exactly 256 pixels, shown pillarboxed like cutscenes: the
// handcar chase at the end of 2-5 (the game draws only as much of the runaway
// locomotive as the 4:3 screen shows, so it would end in mid-air)
// 2-3's collapsing bridge, after the camera jump (BG1's bottom is the bridge's
// band, see ringRepair): the game moves each car from BG1 onto BG2 by jumping
// both cameras 256 px at once, BG1's map having no car there and BG2's the same
// car, which then falls as BG2 scrolls down. Only the 4:3 picture is laid out
// for that: wider, BG1 showed the car it had just given up on the left (all of
// the left is behind the break here) and BG2 its next car on the right, falling
// with the whole layer and back up at the next jump. So BG1 is not drawn left
// of the 4:3 view, and BG2 beside it only as far as the car(s) reaching out of
// the 4:3 view go.
static bool bg2ColumnUsed(Ppu* ppu, int tx, int ty0, int ty1) {
  BgLayer* bg = &ppu->bgLayer[1];
  for(int ty = ty0; ty <= ty1; ty++) {
    int adr = bg->tilemapAdr + (ty & 31) * 32 + (tx & 31);
    if(bg->tilemapWider && (tx & 32)) adr += 0x400;
    if(bg->tilemapHigher && (ty & 32)) adr += bg->tilemapWider ? 0x800 : 0x400;
    if(ppu->vram[adr & 0x7fff] & 0x3ff) return true;
  }
  return false;
}

static void collapseClip(Bbl* g, int shift) {
  Ppu* ppu = g->snes->ppu;
  if(bbl_peek16(g, 0x7E0068) != 1 || bbl_peek16(g, 0x7E0094) != 2 || bbl_peek16(g, 0x7E1912) >= 256) return;
  // the band: the bottom lines of BG1 scrolled away from the camera while the
  // top follows it (remembered for a while: its scroll passes the camera's)
  int lines = ppu->frameOverscan ? 239 : 224, c = (int16_t)bbl_peek16(g, 0x7E1902);
  int dTop = ((ppu->lineHScroll[0][0] - c + 512) & 1023) - 512;
  int dBottom = ((ppu->lineHScroll[lines - 1][0] - c + 512) & 1023) - 512;
  if(dTop >= -16 && dTop <= 16 && (dBottom < -16 || dBottom > 16)) {
    g->collapseSeen = g->frameCount + 1;
    g->bandLive = true;
    g->bandH = ppu->lineHScroll[lines - 1][0];
  }
  if(!g->collapseSeen || g->frameCount + 1 - g->collapseSeen >= 60) return;
  ppu->layerClipLeft[0] = -shift;
  BgLayer* bg = &ppu->bgLayer[1];
  int hs = bg->hScroll + ppu->viewShift[1], vs = bg->vScroll;
  int ty0 = (vs + 1) >> 3, ty1 = (vs + lines) >> 3;
  // (as far as the picture reaches: the view is shifted there, showing the
  // extra width ahead)
  int maxCols = (g->extraCols + abs(shift) + 16) / 8 + 2;
  int left = -shift, right = 256 - shift;
  int tx = (left + hs) >> 3;
  if(bg2ColumnUsed(ppu, tx, ty0, ty1)) {
    for(int n = 0; n < maxCols && bg2ColumnUsed(ppu, tx - 1, ty0, ty1); n++) tx--;
    left = tx * 8 - hs;
  }
  tx = (right - 1 + hs) >> 3;
  if(bg2ColumnUsed(ppu, tx, ty0, ty1)) {
    for(int n = 0; n < maxCols && bg2ColumnUsed(ppu, tx + 1, ty0, ty1); n++) tx++;
    right = (tx + 1) * 8 - hs;
  }
  ppu->layerClipLeft[1] = left;
  ppu->layerClipRight[1] = right;
}

static bool scene43(Bbl* g) {
  return bbl_peek16(g, 0x7E0068) == 1 && bbl_peek16(g, 0x7E0094) == 4 && bbl_peek16(g, 0x7E1912) < 512;
}

// The game over / continue screen runs inside the stage's mode ($80 = 7, the
// camera moved to 0,0; the last fall before it is $80 = 7 too, with the
// camera still in the level). Drawn for 4:3: the level's view shift and
// clip moved it sideways or hid it.
static bool gameOverScreen(Bbl* g) {
  return bbl_peek16(g, 0x7E0080) == 7 && bbl_peek16(g, 0x7E1902) == 0 && bbl_peek16(g, 0x7E1912) == 0;
}

void bbl_runFrame(Bbl* g) {
  Ppu* ppu = g->snes->ppu;
  ppu->noSpriteLimit = g->noSpriteLimit;
  if(g->frameCount > g->optFrame + 1) { ppu->optExtValid = false; g->optPendValid = false; }
  // widescreen is used during gameplay; menus and cutscenes are designed for
  // exactly 256 pixels and get pillarboxed
  bool active = g->extraCols > 0 && hud_inGameplay(g) && !scene43(g) && !gameOverScreen(g);
  ppu->extraLeft = ppu->extraRight = active ? g->extraCols : 0;
  // drawing the wider picture (more sprites, wider streaming) costs the game
  // CPU time; a little more speed keeps busy scenes from slowing down more
  // than they do in 4:3
  g->snes->cpuCycleScale = (active && g->cpuBoost > 0) ? (uint16_t)(25600 / (100 + g->cpuBoost)) : 256;
  updateView(g, active);
  hud_update(g);
  snes_runFrame(g->snes);
  g->ringLinesStale = false;
  vramLogFlush(g);
  if(!active && g->extraCols > 0) {
    for(int y = 0; y < PPU_BUF_H; y++) {
      uint32_t* line = &ppu->pixelBuffer[y * PPU_BUF_W];
      memset(line + PPU_MAX_EXTRA - g->extraCols, 0, g->extraCols * 4);
      memset(line + PPU_MAX_EXTRA + 256, 0, g->extraCols * 4);
    }
  }
  g->frameCount++;
}

const uint32_t* bbl_pixels(Bbl* g) {
  return g->snes->ppu->pixelBuffer;
}

int bbl_width(Bbl* g) { return 256 + 2 * g->extraCols; }
int bbl_height(Bbl* g) { return g->snes->ppu->frameOverscan ? 239 : 224; }

void bbl_getSamples(Bbl* g, int16_t* out, int samples) {
  snes_setSamples(g->snes, out, samples);
}

uint32_t bbl_romOffset(uint32_t adr) {
  // LoROM: banks $00-$7D/$80-$FF, upper half of each bank
  return (((adr >> 16) & 0x7f) << 15) | (adr & 0x7fff);
}

void bbl_romPatch(Bbl* g, uint32_t snesAdr, uint8_t value) {
  uint32_t off = bbl_romOffset(snesAdr) & (g->romSize - 1);
  g->snes->cart->rom[off] = value;
}

void bbl_romRestore(Bbl* g, uint32_t snesAdr) {
  uint32_t off = bbl_romOffset(snesAdr) & (g->romSize - 1);
  g->snes->cart->rom[off] = g->rom[off];
}

uint8_t bbl_romRead(Bbl* g, uint32_t snesAdr) {
  return g->rom[bbl_romOffset(snesAdr) & (g->romSize - 1)];
}

bool bbl_callRoutine(Bbl* g, uint32_t addr, uint16_t a) {
  Cpu* cpu = g->snes->cpu;
  // the idle loop is: 80C1 JSL $809AA9 / 80C5 LDA #0 / TCD / ... / BRA 80C1
  for(int i = 0; i < 2000000; i++) {
    if(cpu->k == 0x80 && cpu->pc == 0x80C1 && !cpu->nmiWanted && !cpu->irqWanted && !cpu->intWanted) {
      // push return address for RTL (points at the last byte of a JSL)
      uint16_t ret = 0x80C1 - 1;
      g->snes->ram[cpu->sp] = 0x80; cpu->sp--;
      g->snes->ram[cpu->sp] = ret >> 8; cpu->sp--;
      g->snes->ram[cpu->sp] = ret & 0xff; cpu->sp--;
      uint16_t sp = cpu->sp;
      cpu->k = addr >> 16;
      cpu->pc = addr & 0xffff;
      cpu->a = a;
      // run the routine to completion (it returns to the idle loop)
      for(int k = 0; k < 4000000; k++) {
        cpu_runOpcode(cpu);
        if(cpu->k == 0x80 && cpu->pc == 0x80C1 && cpu->sp == sp + 3) return true;
      }
      return false;
    }
    cpu_runOpcode(cpu);
  }
  return false;
}

// What the object range knows about each slot (extra now, a child of an
// extra object, tainted, the list entry it was spawned early from, the object
// that spawned it and whether that one is still there) goes into
// save states after the emulator's own data, or a load would forget which
// objects the original game would not have (the soak's crash in 3-3 came
// right after one). States without it still load.
#define OBJ_SLOTS ((0x2000 - 0x0880) / 0x60)
#define OBJ_STATE_MAGIC 0x4F4C4242u   // 'BBLO'

int bbl_saveState(Bbl* g, uint8_t** out) {
  int size = snes_saveState(g->snes, NULL);
  int extra = 6 + OBJ_SLOTS * 8;
  *out = malloc(size + extra);
  snes_saveState(g->snes, *out);
  uint8_t* t = *out + size;
  t[0] = OBJ_STATE_MAGIC & 0xff; t[1] = (OBJ_STATE_MAGIC >> 8) & 0xff; t[2] = (OBJ_STATE_MAGIC >> 16) & 0xff; t[3] = OBJ_STATE_MAGIC >> 24;
  t[4] = 2; t[5] = OBJ_SLOTS;
  for(int k = 0; k < OBJ_SLOTS; k++) {
    uint16_t slot = (uint16_t)(0x0880 + k * 0x60);
    uint8_t* e = t + 6 + k * 8;
    uint16_t par = g->objParent[slot >> 1];
    e[0] = g->objTaint[slot >> 1];
    e[1] = g->objLineage[slot >> 1];
    e[2] = objExtFlag(g, slot);
    e[3] = g->objEntry[slot >> 1] & 0xff;
    e[4] = g->objEntry[slot >> 1] >> 8;
    e[5] = par & 0xff;
    e[6] = par >> 8;
    e[7] = objSlotAdr(par) && g->objParentGen[slot >> 1] == g->objGen[par >> 1];
  }
  return size + extra;
}

static uint32_t ringAreaKey(Bbl* g) {
  return ((uint32_t)(bbl_peek(g, 0x7E0068) * 8 + bbl_peek(g, 0x7E0094)) << 16) | bbl_peek16(g, 0x7E1926);
}

bool bbl_loadState(Bbl* g, const uint8_t* data, int size) {
  g->objEdgeValid = false;
  memset(g->oamFixedNext, 0, sizeof(g->oamFixedNext));
  memset(g->oamFixedShown, 0, sizeof(g->oamFixedShown));
  memset(g->objExtFrame, 0, sizeof(g->objExtFrame));
  memset(g->objLineage, 0, sizeof(g->objLineage));
  memset(g->objTaint, 0, sizeof(g->objTaint));
  memset(g->objGen, 0, sizeof(g->objGen));
  memset(g->objParent, 0, sizeof(g->objParent));
  memset(g->objParentGen, 0, sizeof(g->objParentGen));
  memset(g->objCheckFrame, 0, sizeof(g->objCheckFrame));
  g->objPendN = 0;
  g->objHoldSlot = 0;
  g->objRunning = 0;
  memset(g->objHiTab, 0, sizeof(g->objHiTab));
  g->gfxTrigFrame = 0;
  g->optPendValid = false;
  g->optCamValid = false;
  g->snes->ppu->optExtValid = false;
  // the emulator's part says how long it is (bytes 8-11)
  int core = size;
  if(size >= 12) {
    int len = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    if(len > 0 && len < size) core = len;
  }
  bool ok = snes_loadState(g->snes, (uint8_t*)data, core);
  const uint8_t* t = data + core;
  int ver = size - core >= 6 && (uint32_t)(t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24) == OBJ_STATE_MAGIC ? t[4] : 0;
  int esz = ver == 1 ? 5 : ver == 2 ? 8 : 0;   // (version 1: without the parents)
  if(ok && esz && t[5] == OBJ_SLOTS && size - core >= 6 + OBJ_SLOTS * esz) {
    for(int k = 0; k < OBJ_SLOTS; k++) {
      uint16_t slot = (uint16_t)(0x0880 + k * 0x60);
      const uint8_t* e = t + 6 + k * esz;
      g->objTaint[slot >> 1] = e[0];
      g->objLineage[slot >> 1] = e[1];
      g->objExtFrame[slot >> 1] = e[2] ? g->frameCount + 1 : 0;
      g->objEntry[slot >> 1] = (uint16_t)(e[3] | e[4] << 8);
      if(esz == 8) {
        g->objParent[slot >> 1] = (uint16_t)(e[5] | e[6] << 8);
        g->objParentGen[slot >> 1] = e[7] ? 0 : 0xFFFF;   // (all generations restart at 0)
      }
    }
  }
  // (a load in the middle of a level-load sweep: the view starts over)
  g->sweepFreeze = false;
  g->sprExt = false;
  g->snes->ppu->extOamCount = 0;
  g->sweepFrames = 0;
  // nothing is known about the loaded picture: check all of BG1 right away
  ringReset(g);
  g->ringArea = ringAreaKey(g);
  g->ringSeen[0] = true;
  g->ringLinesStale = true;
  g->loadedFrames = 1;
  g->shownValid = false;
  g->playLoaded = true;
  return ok;
}
