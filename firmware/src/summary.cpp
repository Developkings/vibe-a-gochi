#include "summary.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "buddy.h"
#include "companion.h"
#include "link.h"
#include "pet.h"
#include "power.h"
#include "settings.h"
#include "sound.h"
#include "ui_data.h"
#include "voice.h"

namespace {
DaySummary d;
bool have = false;
int shown_day = 0;
SummaryWait wait = SummaryWait::NONE;
uint16_t gen = 0;
bool auto_pending = false;   // an evening / morning showing waits for its numbers
uint32_t asked_ms = 0;
Preferences prefs;

constexpr int EVENING_HOUR = 18, MORNING_FROM = 5, MORNING_TO = 12;
constexpr uint16_t BIG_DAY_MINUTES = 180;   // a celebration for the pet

bool local_now(tm &lt) {
  if (!link_rtc_valid()) return false;
  const time_t now = time(nullptr);
  gmtime_r(&now, &lt);   // the system clock holds local time (link.cpp)
  return true;
}

// Not while something else needs the screen.
bool busy() {
  return buddy_in_prompt() || voice_state() != VoiceState::OFF || ui_ask_active() || xfer_active();
}

void show_now() {
  power_wake();
  sound_play(Sound::DONE);
  ui_go(SCREEN_ID_TODAY, LV_SCR_LOAD_ANIM_OVER_TOP);
  if (d.active >= BIG_DAY_MINUTES) buddy_one_shot(P_CELEBRATE, 4000);
}
}  // namespace

void summary_request(int day, bool auto_show) {
  if (day != shown_day) have = false;   // the other day: its numbers aren't here yet
  shown_day = day ? 1 : 0;
  auto_pending = auto_show;
  JsonDocument q;
  q["cmd"] = "summary";
  q["day"] = shown_day;
  q["recap"] = settings().recap;
  q["speak"] = auto_show && settings().read_aloud;   // a showing by itself reads its recap aloud
  char buf[96];
  serializeJson(q, buf, sizeof buf);
  wait = companion_send(buf) ? SummaryWait::NUMBERS : SummaryWait::NONE;
  asked_ms = millis();
  gen++;
}

void summary_refresh() {
  if (wait != SummaryWait::NONE || (have && millis() - asked_ms < 60000)) return;   // e.g. shown by itself
  summary_request(shown_day, false);
}

void summary_on_event(JsonDocument &doc) {
  const char *evt = doc["evt"] | "";
  if (!strcmp(evt, "summary_recap")) {
    if (strcmp(doc["date"] | "", d.date)) return;   // for a day no longer shown
    strlcpy(d.recap, doc["recap"] | "", sizeof d.recap);
    wait = SummaryWait::NONE;
    gen++;
    return;
  }
  strlcpy(d.date, doc["date"] | "", sizeof d.date);
  d.active = doc["active"] | 0;
  d.prompts = doc["prompts"] | 0;
  d.replies = doc["replies"] | 0;
  d.files = doc["files"] | 0;
  d.chats = doc["chats"] | 0;
  d.streak = doc["streak"] | 0;
  d.tokens = doc["tokens"] | 0;
  strlcpy(d.projects, doc["projects"] | "", sizeof d.projects);
  strlcpy(d.tools, doc["tools"] | "", sizeof d.tools);
  int i = 0;
  for (JsonVariant v : doc["week"].as<JsonArray>())
    if (i < 7) d.week[i++] = v | 0;
  while (i < 7) d.week[i++] = 0;
  d.week_start = (doc["week_start"] | 0) % 7;
  strlcpy(d.recap, doc["recap"] | "", sizeof d.recap);
  have = true;
  wait = settings().recap && d.prompts && !d.recap[0] ? SummaryWait::RECAP : SummaryWait::NONE;
  gen++;
  if (auto_pending) {
    auto_pending = false;
    if (d.prompts && !busy()) show_now();   // a day without Claude Code isn't worth a pop-up
  }
}

void summary_tick() {
  static uint32_t last = 0;
  if (millis() - last < 5000) return;
  last = millis();
  if (auto_pending && millis() - asked_ms > 30000) auto_pending = false;   // the companion didn't answer
  tm lt;
  if (auto_pending || !companion_connected() || busy() || !local_now(lt)) return;
  const uint32_t key = uint32_t(lt.tm_year) * 400 + lt.tm_yday;
  prefs.begin("summary", false);
  if (lt.tm_hour >= EVENING_HOUR && prefs.getUInt("eve", 0) != key) {
    prefs.putUInt("eve", key);
    summary_request(0, true);
  } else if (lt.tm_hour >= MORNING_FROM && lt.tm_hour < MORNING_TO && prefs.getUInt("morn", 0) != key &&
             !power_screen_off() && !power_napping()) {   // the first time someone is at the board
    prefs.putUInt("morn", key);
    summary_request(1, true);
  }
  prefs.end();
}

bool summary_have() { return have; }
const DaySummary &summary_data() { return d; }
int summary_day() { return shown_day; }
SummaryWait summary_wait() {
  if (wait != SummaryWait::NONE && millis() - asked_ms > 120000) wait = SummaryWait::NONE;   // gave up
  return wait;
}
uint16_t summary_gen() { return gen; }
