#pragma once
#include <stdbool.h>
#include <SDL.h>

typedef struct UiColor { uint8_t r, g, b, a; } UiColor;

#define UI_WHITE  ((UiColor){ 255, 255, 255, 255 })
#define UI_GREY   ((UiColor){ 150, 160, 190, 255 })
#define UI_DIM    ((UiColor){ 100, 110, 140, 255 })
#define UI_YELLOW ((UiColor){ 255, 220, 60, 255 })
#define UI_CYAN   ((UiColor){ 90, 220, 255, 255 })
#define UI_PINK   ((UiColor){ 255, 120, 180, 255 })
#define UI_GREEN  ((UiColor){ 110, 230, 120, 255 })
#define UI_RED    ((UiColor){ 255, 90, 90, 255 })

// text is UTF-8: ASCII, and an em dash drawn with this glyph code
#define UI_GLYPH_EMDASH 1

void ui_init(SDL_Renderer* r);
void ui_shutdown(void);
// scale is the pixel size of one font pixel
void ui_text(int x, int y, const char* s, UiColor c, int scale);
void ui_textShadow(int x, int y, const char* s, UiColor c, int scale);
int ui_textWidth(const char* s, int scale);
void ui_rect(int x, int y, int w, int h, UiColor c);
void ui_frame(int x, int y, int w, int h, UiColor fill, UiColor border, int thickness);
