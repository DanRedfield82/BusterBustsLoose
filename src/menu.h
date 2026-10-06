#pragma once
#include <stdbool.h>

void menu_open(void);
void menu_close(void);
bool menu_isOpen(void);
// true while the music player page is showing (the frontend then runs the
// music instance instead of silence)
bool menu_musicActive(void);
void menu_update(void);
void menu_draw(int winW, int winH);
// touch: a tap at a screen position selects the row there and activates it
// (the left / right half of a value steps it back / forward)
void menu_tap(int x, int y);
int menu_rowHeight(void);   // of the last drawn menu, in screen pixels
