#include <math.h>
#include <string.h>
#include "touch.h"
#include "config.h"
#include "finput.h"
#include "ui.h"

// Layout, in units of the screen's short side (u), measured from the bottom
// corners so it suits any phone shape: the D-pad bottom left, the face buttons
// bottom right in the SNES diamond, L and R above them, Start and Menu at the
// bottom centre, fast forward at the top centre.

enum { OWN_NONE = 0, OWN_DPAD, OWN_FACE, OWN_PILL };
enum { FACE_B = 0, FACE_Y, FACE_A, FACE_X, FACE_COUNT };
enum { PILL_L = 0, PILL_R, PILL_START, PILL_MENU, PILL_FF, PILL_BACK, PILL_COUNT };

#define MAX_FINGERS 10

typedef struct Finger {
  SDL_FingerID id;
  bool down;
  float x, y;       // pixels
  int owner;        // what it pressed first
  int which;        // face button / pill index
  bool upPending;   // lifted, kept down until it has been seen for one update
  uint32_t upLoop;
  float startX, startY, lastY;   // a touch on the menu (not on a control)
  bool moved;
} Finger;

static Finger fingers[MAX_FINGERS];
static int scrW = 1280, scrH = 720;    // last rendered output size
static bool used;                      // touched since the last key / controller input
static bool menuOpen;
static bool backPending;
static uint32_t loopNo;
// the menu: a tap where no control is, and drag scrolling
static bool tapPending;
static int tapX, tapY;
static float scrollAcc;

static float unit(void) { return (float)(scrW < scrH ? scrW : scrH); }

// geometry
static void dpadGeom(float* cx, float* cy, float* r) {
  float u = unit();
  *r = 0.16f * u;
  *cx = 0.23f * u;
  *cy = scrH - 0.25f * u;
}

static void faceGeom(int i, float* x, float* y, float* r) {
  float u = unit(), bx = scrW - 0.24f * u, by = scrH - 0.26f * u, s = 0.12f * u;
  static const float off[FACE_COUNT][2] = { { 0, 1 }, { -1, 0 }, { 1, 0 }, { 0, -1 } };   // B Y A X
  *x = bx + off[i][0] * s;
  *y = by + off[i][1] * s;
  *r = 0.068f * u;
}

static void pillGeom(int i, SDL_FRect* rc) {
  float u = unit(), w = 0.20f * u, h = 0.085f * u;
  float dx, dy, fx, fy, fr;
  dpadGeom(&dx, &dy, &fr);
  faceGeom(FACE_X, &fx, &fy, &fr);
  switch(i) {
    case PILL_L: rc->x = dx - w / 2; rc->y = dy - 0.36f * u; break;
    case PILL_R: rc->x = fx - w / 2; rc->y = dy - 0.36f * u; break;
    case PILL_START: case PILL_BACK: rc->x = scrW / 2.0f + 0.03f * u; rc->y = scrH - h - 0.03f * u; break;
    case PILL_MENU: rc->x = scrW / 2.0f - w - 0.03f * u; rc->y = scrH - h - 0.03f * u; break;
    default: rc->x = scrW / 2.0f - w / 2; rc->y = 0.03f * u; break;
  }
  rc->w = w;
  rc->h = h;
}

static bool inPill(int i, float x, float y, float slack) {
  SDL_FRect rc;
  pillGeom(i, &rc);
  return x >= rc.x - slack && x < rc.x + rc.w + slack && y >= rc.y - slack && y < rc.y + rc.h + slack;
}

// the face button nearest to a point, if close enough
static int faceAt(float x, float y) {
  int best = -1;
  float bestD = 0;
  for(int i = 0; i < FACE_COUNT; i++) {
    float fx, fy, fr;
    faceGeom(i, &fx, &fy, &fr);
    float d = hypotf(x - fx, y - fy);
    if(d < fr * 1.45f && (best < 0 || d < bestD)) { best = i; bestD = d; }
  }
  return best;
}

// the pills shown: in the menu only MENU and BACK (its rows are tapped directly)
static bool pillShown(int i) {
  return menuOpen ? (i == PILL_MENU || i == PILL_BACK) : i != PILL_BACK;
}

// what a new touch lands on
static void claim(Finger* f) {
  f->owner = OWN_NONE;
  for(int i = 0; i < PILL_COUNT; i++) {
    if(pillShown(i) && inPill(i, f->x, f->y, 0.02f * unit())) { f->owner = OWN_PILL; f->which = i; return; }
  }
  if(menuOpen) return;
  float cx, cy, r;
  dpadGeom(&cx, &cy, &r);
  if(hypotf(f->x - cx, f->y - cy) < r * 1.5f) { f->owner = OWN_DPAD; return; }
  int fb = faceAt(f->x, f->y);
  if(fb >= 0) { f->owner = OWN_FACE; f->which = fb; return; }
}

static Finger* findFinger(SDL_FingerID id) {
  for(int i = 0; i < MAX_FINGERS; i++) if(fingers[i].down && !fingers[i].upPending && fingers[i].id == id) return &fingers[i];
  return NULL;
}

void touch_handleEvent(const SDL_Event* e) {
  if(e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP) return;
  float x = e->tfinger.x * scrW, y = e->tfinger.y * scrH;
  Finger* f = findFinger(e->tfinger.fingerId);
  if(e->type == SDL_FINGERDOWN) {
    used = true;
    if(!f) for(int i = 0; i < MAX_FINGERS && !f; i++) if(!fingers[i].down) f = &fingers[i];
    if(!f) return;
    f->id = e->tfinger.fingerId;
    f->down = true;
    f->upPending = false;
    f->x = x;
    f->y = y;
    claim(f);
    f->startX = x;
    f->startY = y;
    f->lastY = y;
    f->moved = false;
    if(f->owner == OWN_PILL && f->which == PILL_BACK) backPending = true;
  } else if(f) {
    f->x = x;
    f->y = y;
    if(menuOpen && f->owner == OWN_NONE) {
      scrollAcc += f->lastY - y;   // (dragging up moves down the list)
      f->lastY = y;
      if(hypotf(x - f->startX, y - f->startY) > 0.03f * unit()) f->moved = true;
      if(e->type == SDL_FINGERUP && !f->moved) { tapPending = true; tapX = (int)x; tapY = (int)y; }
    }
    // (a thumb rolls from one face button to the next)
    if(f->owner == OWN_FACE) {
      int fb = faceAt(x, y);
      if(fb >= 0) f->which = fb;
    }
    // (a quick tap is down and up between two updates: it still counts once)
    if(e->type == SDL_FINGERUP) { f->upPending = true; f->upLoop = loopNo; }
  }
}

void touch_frame(void) {
  loopNo++;
  for(int i = 0; i < MAX_FINGERS; i++)
    if(fingers[i].down && fingers[i].upPending && fingers[i].upLoop < loopNo - 1) fingers[i].down = false;
}

void touch_noteOtherInput(void) { used = false; }

bool touch_visible(void) {
  if(cfg.touchControls == TOUCH_OFF) return false;
  if(cfg.touchControls == TOUCH_ALWAYS) return true;
#ifdef __ANDROID__
  // (a phone shows them from the start, unless a controller is connected)
  static bool started;
  if(!started) { started = true; used = in_padCount() == 0; }
#endif
  return used;
}

// the D-pad's directions for one finger (8-way, with a small dead zone)
static uint16_t dpadMask(const Finger* f) {
  float cx, cy, r;
  dpadGeom(&cx, &cy, &r);
  float dx = f->x - cx, dy = f->y - cy, d = hypotf(dx, dy);
  if(d < r * 0.22f) return 0;
  uint16_t m = 0;
  if(dx < -0.38f * d) m |= 1 << BTN_LEFT;
  if(dx > 0.38f * d) m |= 1 << BTN_RIGHT;
  if(dy < -0.38f * d) m |= 1 << BTN_UP;
  if(dy > 0.38f * d) m |= 1 << BTN_DOWN;
  return m;
}

static uint16_t heldMask(void) {
  static const int faceBtn[FACE_COUNT] = { BTN_B, BTN_Y, BTN_A, BTN_X };
  static const int pillBtn[PILL_COUNT] = { BTN_L, BTN_R, BTN_START, -1, -1, -1 };
  uint16_t m = 0;
  for(int i = 0; i < MAX_FINGERS; i++) {
    const Finger* f = &fingers[i];
    if(!f->down) continue;
    if(f->owner == OWN_DPAD) m |= dpadMask(f);
    else if(f->owner == OWN_FACE) m |= 1 << faceBtn[f->which];
    else if(f->owner == OWN_PILL && pillBtn[f->which] >= 0 && inPill(f->which, f->x, f->y, 0.06f * unit())) m |= 1 << pillBtn[f->which];
  }
  return m;
}

uint16_t touch_snesMask(void) {
  return touch_visible() && !menuOpen ? heldMask() : 0;
}

static bool pillHeld(int pill) {
  for(int i = 0; i < MAX_FINGERS; i++) {
    const Finger* f = &fingers[i];
    if(f->down && f->owner == OWN_PILL && f->which == pill && inPill(pill, f->x, f->y, 0.06f * unit())) return true;
  }
  return false;
}

bool touch_menuHeld(void) { return touch_visible() && pillHeld(PILL_MENU); }
bool touch_fastForwardHeld(void) { return touch_visible() && !menuOpen && pillHeld(PILL_FF); }

int touch_navDir(void) {
  if(!touch_visible() || menuOpen) return 0;
  uint16_t m = heldMask();
  if(m & (1 << BTN_UP)) return NAV_UP;
  if(m & (1 << BTN_DOWN)) return NAV_DOWN;
  if(m & (1 << BTN_LEFT)) return NAV_LEFT;
  if(m & (1 << BTN_RIGHT)) return NAV_RIGHT;
  return 0;
}

bool touch_takeBack(void) { bool r = backPending; backPending = false; return r; }
void touch_setMenuOpen(bool open) {
  if(!open) { tapPending = false; scrollAcc = 0; }
  menuOpen = open;
}

bool touch_takeTap(int* x, int* y) {
  if(!tapPending) return false;
  tapPending = false;
  if(x) *x = tapX;
  if(y) *y = tapY;
  return true;
}

int touch_takeScroll(int rowH) {
  if(rowH < 1) rowH = 1;
  int n = (int)(scrollAcc / rowH);
  scrollAcc -= (float)n * rowH;
  return n;
}

// ---------------------------------------------------------------- drawing

static void fillCircle(SDL_Renderer* r, float cx, float cy, float rad, SDL_Color c) {
  enum { SEG = 40 };
  SDL_Vertex v[SEG + 1];
  int idx[SEG * 3];
  v[0].position = (SDL_FPoint){ cx, cy };
  v[0].color = c;
  for(int i = 0; i < SEG; i++) {
    float a = (float)i / SEG * 6.2831853f;
    v[i + 1].position = (SDL_FPoint){ cx + cosf(a) * rad, cy + sinf(a) * rad };
    v[i + 1].color = c;
    idx[i * 3] = 0;
    idx[i * 3 + 1] = i + 1;
    idx[i * 3 + 2] = (i + 1) % SEG + 1;
  }
  SDL_RenderGeometry(r, NULL, v, SEG + 1, idx, SEG * 3);
}

static void ring(SDL_Renderer* r, float cx, float cy, float rad, float th, SDL_Color c) {
  enum { SEG = 48 };
  SDL_Vertex v[SEG * 2];
  int idx[SEG * 6];
  for(int i = 0; i < SEG; i++) {
    float a = (float)i / SEG * 6.2831853f, ca = cosf(a), sa = sinf(a);
    v[i * 2].position = (SDL_FPoint){ cx + ca * rad, cy + sa * rad };
    v[i * 2 + 1].position = (SDL_FPoint){ cx + ca * (rad - th), cy + sa * (rad - th) };
    v[i * 2].color = v[i * 2 + 1].color = c;
    int n = (i + 1) % SEG;
    int* q = &idx[i * 6];
    q[0] = i * 2; q[1] = i * 2 + 1; q[2] = n * 2;
    q[3] = n * 2; q[4] = i * 2 + 1; q[5] = n * 2 + 1;
  }
  SDL_RenderGeometry(r, NULL, v, SEG * 2, idx, SEG * 6);
}

static void label(float cx, float cy, const char* s, int scale, UiColor c) {
  ui_text((int)(cx - ui_textWidth(s, scale) / 2.0f), (int)(cy - 3.5f * scale), s, c, scale);
}

static SDL_Color fillCol(bool pressed) { return pressed ? (SDL_Color){ 255, 255, 255, 110 } : (SDL_Color){ 20, 30, 60, 90 }; }
static const SDL_Color edgeCol = { 255, 255, 255, 150 };

void touch_render(SDL_Renderer* r, int w, int h) {
  scrW = w;
  scrH = h;
  if(!touch_visible()) return;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  float u = unit();
  int ts = (int)(u / 150.0f);
  if(ts < 1) ts = 1;
  uint16_t held = heldMask();
  UiColor txt = { 255, 255, 255, 210 };

  if(!menuOpen) {
  // D-pad: a disc with the four arrows, lit where it is pressed
  float cx, cy, rad;
  dpadGeom(&cx, &cy, &rad);
  fillCircle(r, cx, cy, rad, fillCol(false));
  ring(r, cx, cy, rad, 0.012f * u, edgeCol);
  static const struct { int btn; float dx, dy; const char* s; } arrows[4] = {
    { BTN_UP, 0, -1, "^" }, { BTN_DOWN, 0, 1, "v" }, { BTN_LEFT, -1, 0, "<" }, { BTN_RIGHT, 1, 0, ">" } };
  for(int i = 0; i < 4; i++) {
    float ax = cx + arrows[i].dx * rad * 0.62f, ay = cy + arrows[i].dy * rad * 0.62f;
    bool p = (held & (1 << arrows[i].btn)) != 0;
    fillCircle(r, ax, ay, rad * 0.27f, fillCol(p));
    label(ax, ay, arrows[i].s, ts * 2, txt);
  }

  // face buttons, labelled for the menu while it is open
  static const char* faceName[FACE_COUNT] = { "B", "Y", "A", "X" };
  for(int i = 0; i < FACE_COUNT; i++) {
    float fx, fy, fr;
    faceGeom(i, &fx, &fy, &fr);
    bool p = false;
    for(int k = 0; k < MAX_FINGERS; k++) if(fingers[k].down && fingers[k].owner == OWN_FACE && fingers[k].which == i) p = true;
    fillCircle(r, fx, fy, fr, fillCol(p));
    ring(r, fx, fy, fr, 0.008f * u, edgeCol);
    label(fx, fy, faceName[i], ts * 2, txt);
  }
  }

  // pills
  static const char* pillName[PILL_COUNT] = { "L", "R", "START", "MENU", "FAST", "BACK" };
  for(int i = 0; i < PILL_COUNT; i++) {
    if(!pillShown(i)) continue;
    SDL_FRect rc;
    pillGeom(i, &rc);
    bool p = pillHeld(i);
    SDL_Color c = fillCol(p);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRectF(r, &rc);
    SDL_SetRenderDrawColor(r, edgeCol.r, edgeCol.g, edgeCol.b, edgeCol.a);
    SDL_RenderDrawRectF(r, &rc);
    label(rc.x + rc.w / 2, rc.y + rc.h / 2, pillName[i], ts, txt);
  }
}
