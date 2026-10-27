// Daily summary (Today screen): the day's numbers from the PC companion (companion/daily.py reads
// the Claude Code logs), an optional recap by Claude Haiku, and the board's own approvals.
// It also shows itself: at 18:00 for the day, and on the first morning wake for yesterday.
#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

struct DaySummary {
  char date[11];
  uint16_t active, prompts, replies, files, chats, streak;   // active = minutes
  uint32_t tokens;
  char projects[112], tools[112];   // ready-made lines: "name 1h 20m  ·  …", "Edit 40  ·  …"
  uint16_t week[7];                 // active minutes, the shown day last
  uint8_t week_start;               // weekday of week[0], 0 = Monday
  char recap[420];
};

enum class SummaryWait : uint8_t { NONE, NUMBERS, RECAP };

void summary_request(int day, bool auto_show);   // 0 today, 1 yesterday
void summary_refresh();                          // the screen opened: ask again unless just asked
void summary_on_event(JsonDocument &doc);        // "summary" / "summary_recap" from the companion
void summary_tick();                             // main loop: the evening / morning showings
bool summary_have();                             // numbers for the shown day arrived
const DaySummary &summary_data();
int summary_day();
SummaryWait summary_wait();
uint16_t summary_gen();                          // bumps on every change (the screen repaints)
