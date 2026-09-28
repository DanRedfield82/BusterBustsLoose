// Cheats. Most are small ROM patches (the classic Pro Action Replay codes for
// the USA version), the rest poke RAM every frame.
//
// RAM notes: $8E lives (BCD), $9C most hearts, $9E hearts, $B6 dash meter, $6A stars,
// $B8 Buster's hit signal (see invincible), $08A8 Buster's jump/vertical state,
// $1D7E debug stage select flag.
#include <string.h>
#include "cheats.h"

const CheatInfo cheatInfo[CHEAT_COUNT] = {
  { "Infinite health",      "Hearts are never lost and stay full" },
  { "Infinite lives",       "The lives counter never goes down" },
  { "Infinite dash",        "The dash meter never runs out" },
  { "Invincibility",        "Enemies pass straight through Buster" },
  { "Moon jump",            "Hold jump to float upwards" },
  { "Start with 99 lives",  "New games and continues start with 99 lives" },
  { "Five hearts",          "Start with five hearts (Children/Normal)" },
  { "Star bonus",           "Small stars are worth 10" },
  { "Debug stage select",   "Starting a new game opens the hidden stage select" },
  { "Passwords always work","Passwords are accepted on every difficulty" },
};

typedef struct RomPatch {
  uint32_t adr;
  uint8_t val;
} RomPatch;

typedef struct CheatPatches {
  int count;
  RomPatch p[3];
} CheatPatches;

static const CheatPatches patches[CHEAT_COUNT] = {
  [CHEAT_INF_HEALTH]  = { 1, { { 0x8FF05A, 0xEA } } },                      // DEC A -> NOP
  [CHEAT_INF_LIVES]   = { 2, { { 0x80EF2C, 0x00 }, { 0x80AF45, 0x00 } } },  // SBC #1 -> SBC #0
  [CHEAT_INF_DASH]    = { 1, { { 0x80D03F, 0xA5 } } },                      // DEC $B6 -> LDA $B6
  [CHEAT_99_LIVES]    = { 2, { { 0x808518, 0x99 }, { 0x80A647, 0x99 } } },
  [CHEAT_5_HEARTS]    = { 1, { { 0x80A407, 0x05 } } },
  [CHEAT_STAR_BONUS]  = { 1, { { 0x8FEEFA, 0x10 } } },
  [CHEAT_PASSWORDS]   = { 1, { { 0x91EA81, 0x34 } } },
};

static int enabled[CHEAT_COUNT];

void cheats_init(Bbl* g) {
  memset(enabled, 0, sizeof(enabled));
  cheats_apply(g);
}

void cheats_apply(Bbl* g) {
  for(int id = 0; id < CHEAT_COUNT; id++) {
    for(int i = 0; i < patches[id].count; i++) {
      if(enabled[id]) bbl_romPatch(g, patches[id].p[i].adr, patches[id].p[i].val);
      else bbl_romRestore(g, patches[id].p[i].adr);
    }
  }
}

void cheats_set(Bbl* g, int id, int value) {
  if(id < 0 || id >= CHEAT_COUNT) return;
  bool was = enabled[id];
  enabled[id] = value ? 1 : 0;
  cheats_apply(g);
  if(was && !enabled[id]) {
    if(id == CHEAT_STAGE_SELECT) bbl_poke(g, 0x7E1D7E, 0);
  }
}

int cheats_get(Bbl* g, int id) {
  (void)g;
  return (id >= 0 && id < CHEAT_COUNT) ? enabled[id] : 0;
}

// Invincibility. $B8 is Buster's hit signal: an enemy that touches him while
// it, $08B8 and $C8 are 0 sets it to $FFFF ($8F:F976), his own code sees it
// negative ($80:D01B) and takes the hit, and it is cleared once dealt with.
// Holding it at 1 (the classic RAM code) kept hits out, but whatever waits
// for Buster to be clear of a hit waited forever: football's plays ended at
// once, 2-1's jump rope ($4126, $83:AEF8) never started. Instead the three
// places that set it are turned off: the contact test always takes its
// "already hit" branch (the enemy passes through, as before), and the two
// hazards' DEC $B8 ($84:FEF3 in 6-2, $84:D841 for $C8's requests) do nothing.
// Not in football (stage 4), where being tackled is part of a play.
static const RomPatch invinciblePatches[] = {
  { 0x8FF974, 0x80 },                        // BNE $F97F -> BRA
  { 0x84FEF3, 0xEA }, { 0x84FEF4, 0xEA },    // DEC $B8 -> NOP NOP
  { 0x84D841, 0xEA }, { 0x84D842, 0xEA },
};

static void invincible(Bbl* g, bool on) {
  for(int i = 0; i < (int)(sizeof(invinciblePatches) / sizeof(invinciblePatches[0])); i++) {
    if(on) bbl_romPatch(g, invinciblePatches[i].adr, invinciblePatches[i].val);
    else bbl_romRestore(g, invinciblePatches[i].adr);
  }
  // (the game only ever leaves $B8 at 0 or negative: a positive value is the
  // old cheat's 1 in a save state, which would still hold those events up)
  uint16_t b8 = bbl_peek(g, 0x7E00B8) | bbl_peek(g, 0x7E00B9) << 8;
  if(b8 != 0 && b8 < 0x8000) { bbl_poke(g, 0x7E00B8, 0); bbl_poke(g, 0x7E00B9, 0); }
}

void cheats_preFrame(Bbl* g) {
  if(enabled[CHEAT_INF_HEALTH]) {
    // the patch only stops hearts going down; also keep them full ($9C = the
    // most hearts Buster has, $9E = hearts now), so every section is the same
    uint8_t most = bbl_peek(g, 0x7E009C);
    if(most >= 1 && most <= 8 && bbl_peek(g, 0x7E009E) < most) bbl_poke(g, 0x7E009E, most);
  }
  invincible(g, enabled[CHEAT_INVINCIBLE] && bbl_peek(g, 0x7E0068) != 3);
  // (a 1-up at 99 lives makes them $0100, shown as 00: the game adds lives in
  // BCD without a limit, $8F:EF01, and with these cheats 99 comes quickly)
  if(enabled[CHEAT_INF_LIVES] || enabled[CHEAT_99_LIVES] || enabled[CHEAT_STAR_BONUS]) {
    uint16_t lives = bbl_peek16(g, 0x7E008E);
    if(lives > 0x0099 && lives < 0x0200) { bbl_poke(g, 0x7E008E, 0x99); bbl_poke(g, 0x7E008F, 0x00); }
  }
  // Moon jump: while Buster rises from a jump (his type $0890 = 2) $08A8 counts
  // the rise down from 16; held at 7 he keeps rising. Only then: in other
  // states (dash jumps, swings, rides, text boxes, football's players) the
  // field means something else or nothing, and writing it there was a risk.
  if(enabled[CHEAT_MOON_JUMP] && (g->snes->input1->currentState & (1 << BTN_B)) &&
     bbl_peek(g, 0x7E0068) != 3 && bbl_peek16(g, 0x7E0890) == 2) {
    bbl_poke(g, 0x7E08A8, 7);
  }
  if(enabled[CHEAT_STAGE_SELECT]) bbl_poke(g, 0x7E1D7E, 1);
}

void cheats_postFrame(Bbl* g) {
  (void)g;
}
