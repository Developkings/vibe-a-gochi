// The drawing API the reference species files (src/buddies/*.cpp) were written against.
// pet.cpp implements it on a 135x82 1x canvas (6x8 px per character) that is then scaled
// into whichever screen shows the pet.
#pragma once
#include <stdint.h>
#include <string.h>

typedef void (*StateFn)(uint32_t t);

struct Species {
  const char *name;
  uint16_t bodyColor;   // RGB565
  StateFn states[7];    // sleep, idle, busy, attention, celebrate, dizzy, heart
};

extern const int BUDDY_X_CENTER;
extern const int BUDDY_CANVAS_W;
extern const int BUDDY_Y_BASE;
extern const int BUDDY_Y_OVERLAY;
extern const int BUDDY_CHAR_W;
extern const int BUDDY_CHAR_H;

extern const uint16_t BUDDY_BG;
extern const uint16_t BUDDY_HEART;
extern const uint16_t BUDDY_DIM;
extern const uint16_t BUDDY_YEL;
extern const uint16_t BUDDY_WHITE;
extern const uint16_t BUDDY_CYAN;
extern const uint16_t BUDDY_GREEN;
extern const uint16_t BUDDY_PURPLE;
extern const uint16_t BUDDY_RED;
extern const uint16_t BUDDY_BLUE;

void buddyPrintLine(const char *line, int yPx, uint16_t color, int xOff = 0);
void buddyPrintSprite(const char *const *lines, uint8_t nLines, int yOffset, uint16_t color, int xOff = 0);
void buddySetCursor(int x, int y);
void buddySetColor(uint16_t fg);
void buddyPrint(const char *s);
