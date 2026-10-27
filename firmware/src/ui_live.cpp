// Live data on the EEZ screens: labels the firmware fills (identifiers in gen_ui.py), the lists
// it builds at runtime (chat, projects, chats, characters, WiFi), and automatic navigation
// (approval requests, pairing codes, the clock face when idle on USB).
#include <Arduino.h>
#include <LittleFS.h>
#include <nvs_flash.h>
#include <time.h>
#include "ble_link.h"
#include "buddy.h"
#include "chat_log.h"
#include "companion.h"
#include "i18n.h"
#include "i18n_strings.h"
#include "link.h"
#include "pet.h"
#include "power.h"
#include "settings.h"
#include "sound.h"
#include "stats.h"
#include "summary.h"
#include "theme.h"
#include "ui/fonts.h"
#include "ui/ui.h"
#include "ui_data.h"
#include "voice.h"
#include "wifi_link.h"

#define FW_VERSION "0.2"

namespace {

const char *S(TrString s) { return i18n_str(s); }

void set_label(lv_obj_t *o, const char *t) {
  if (o && strcmp(lv_label_get_text(o), t)) lv_label_set_text(o, t);
}

void show(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Global variables only change when the text does (each set re-renders the bound labels).
char var_cache[FLOW_GLOBAL_VARIABLE_SCREEN_ORIENTATION_NAME + 1][96];   // the last variable (gen_ui.py)

// Bumped when the screen turns: the lists and buttons sized here are rebuilt for the new shape.
uint8_t layout_gen = 0;
void set_var(FlowGlobalVariables v, const char *t) {
  if (!strncmp(var_cache[v], t, sizeof var_cache[v] - 1)) return;
  strlcpy(var_cache[v], t, sizeof var_cache[v]);
  ui_set_text(v, t);
}

bool on_screen(lv_obj_t *scr) { return lv_screen_active() == scr; }

void fmt_count(char *buf, size_t n, uint32_t v) {
  if (v >= 1000000) snprintf(buf, n, "%lu.%luM", (unsigned long)(v / 1000000), (unsigned long)(v / 100000 % 10));
  else if (v >= 1000) snprintf(buf, n, "%luK", (unsigned long)(v / 1000));
  else snprintf(buf, n, "%lu", (unsigned long)v);
}

void fmt_duration(char *buf, size_t n, uint32_t s) {
  if (s >= 3600) snprintf(buf, n, "%luh %lum", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
  else snprintf(buf, n, "%lum", (unsigned long)(s / 60));
}

// "charging" → "Charging" (ASCII first letters only; the UTF-8 ones stay as they are).
void capitalize(char *s) {
  if (*s >= 'a' && *s <= 'z') *s -= 32;
}

lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  return o;
}

lv_obj_t *text(lv_obj_t *parent, const char *t, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

lv_obj_t *card(lv_obj_t *parent, int w, int h, lv_color_t bg, int radius) {
  lv_obj_t *b = plain(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, radius, 0);
  lv_obj_set_style_bg_color(b, THEME_RAISED, LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

// A list row like gen_ui.list_row(): icon tile, title, subtitle, optional right text, chevron.
lv_obj_t *list_row(lv_obj_t *list, int y, const char *icon, bool accent, const char *title, const char *sub,
                   const char *right) {
  lv_obj_t *r = card(list, lv_obj_get_width(list), 46, THEME_SURFACE, 12);
  lv_obj_set_pos(r, 0, y);
  lv_obj_t *ib = plain(r);
  lv_obj_set_size(ib, 32, 32);
  lv_obj_align(ib, LV_ALIGN_LEFT_MID, 8, 0);
  lv_obj_set_style_bg_color(ib, accent ? THEME_ACCENT_SOFT : THEME_RAISED, 0);
  lv_obj_set_style_bg_opa(ib, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ib, 10, 0);
  lv_obj_center(text(ib, icon, &ui_font_i20, accent ? THEME_ACCENT : THEME_DIM));
  lv_obj_t *t = text(r, title, &ui_font_t14, THEME_TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_size(t, lv_obj_get_width(list) - (right ? 130 : 84), 18);
  lv_obj_set_pos(t, 50, sub && *sub ? 5 : 14);
  if (sub && *sub) {
    lv_obj_t *s = text(r, sub, &ui_font_t11, THEME_FAINT);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s, lv_obj_get_width(list) - 84, 15);
    lv_obj_set_pos(s, 50, 25);
  }
  if (right) lv_obj_align(text(r, right, &ui_font_t11, THEME_FAINT), LV_ALIGN_RIGHT_MID, -30, 0);
  lv_obj_align(text(r, ICON_CHEVRON_RIGHT, &ui_font_i20, THEME_FAINT), LV_ALIGN_RIGHT_MID, -6, 0);
  return r;
}

void clear_list(lv_obj_t *list) {
  if (list) lv_obj_clean(list);
}

void info_row(lv_obj_t *list, const char *msg) {
  clear_list(list);
  lv_obj_t *l = text(list, msg, &ui_font_t14, THEME_DIM);
  lv_obj_set_width(l, lv_obj_get_width(list) - 20);
  lv_obj_set_pos(l, 10, 10);
}

// ---------------------------------------------------------------- status bar

// The status bar is an EEZ user widget, one copy per screen, with no named objects: on the shown
// screen, find the copy (a screen-wide 24 px box of 7 labels: dot, link, clock, bt, wifi, battery, usb) and
// colour its icons. wifi: 0 off (dim), 1 connected (text colour), 2 Claude Code's hooks talk over it (accent).
void status_icon_colors(bool bt_on, int wifi_on, bool usb_on) {
  lv_obj_t *scr = lv_screen_active();
  static lv_obj_t *was_scr = nullptr;
  static int8_t was_bt = -1, was_wifi = -1, was_usb = -1;
  static uint8_t was_layout = 0;
  if (scr == was_scr && bt_on == was_bt && wifi_on == was_wifi && usb_on == was_usb && was_layout == layout_gen)
    return;
  lv_obj_update_layout(scr);   // a screen just loaded has no sizes yet
  bool found = false;
  // The bar is the screen's child, or (as an EEZ user widget) that child's only child.
  for (uint32_t i = 0; i < lv_obj_get_child_count(scr); i++) {
    lv_obj_t *o = lv_obj_get_child(scr, i);
    if (lv_obj_get_child_count(o) == 1) o = lv_obj_get_child(o, 0);
    if (lv_obj_get_child_count(o) == 7 && lv_obj_get_width(o) == ui_width() && lv_obj_get_height(o) == 24 &&
        lv_obj_check_type(lv_obj_get_child(o, 3), &lv_label_class)) {
      lv_obj_set_style_text_color(lv_obj_get_child(o, 3), bt_on ? THEME_ACCENT : THEME_DIM, 0);
      lv_obj_set_style_text_color(lv_obj_get_child(o, 4),
                                  wifi_on == 2 ? THEME_ACCENT : wifi_on ? THEME_TEXT : THEME_DIM, 0);
      lv_obj_set_style_text_color(lv_obj_get_child(o, 6), usb_on ? THEME_ACCENT : THEME_DIM, 0);
      found = true;
    }
  }
  if (!found) return;   // no bar on this screen (yet): try again next time
  was_scr = scr;
  was_layout = layout_gen;
  was_bt = bt_on;
  was_wifi = wifi_on;
  was_usb = usb_on;
  char note[64];
  snprintf(note, sizeof note, "[ui] status icons bt=%d wifi=%d usb=%d", bt_on, wifi_on, usb_on);
  link_debug_note(note);
}

void status_bar() {
  const Tama &t = tama();
  const bool demo = link_demo();
  set_var(FLOW_GLOBAL_VARIABLE_SB_LINK_ICON, demo ? ICON_PLAY_ARROW : t.connected ? ICON_LINK : ICON_LINK_OFF);
  set_var(FLOW_GLOBAL_VARIABLE_SB_LINK_TEXT, demo ? S(TrString::DEMO) : t.connected ? S(TrString::CLAUDE) : S(TrString::OFFLINE));
  set_var(FLOW_GLOBAL_VARIABLE_SB_BT_ICON, ble_passkey()     ? ICON_BLUETOOTH_SEARCHING
                                           : ble_connected() ? ICON_BLUETOOTH_CONNECTED
                                                             : ICON_BLUETOOTH);
  const char *wifi = ICON_WIFI_OFF;
  if (wifi_status() == WifiStatus::CONNECTED) {
    const int r = wifi_rssi();
    wifi = r > -55 ? ICON_SIGNAL_WIFI_4_BAR : r > -65 ? ICON_NETWORK_WIFI_3_BAR : r > -75 ? ICON_NETWORK_WIFI_2_BAR : ICON_NETWORK_WIFI_1_BAR;
  } else if (wifi_status() == WifiStatus::CONNECTING) {
    wifi = ICON_WIFI_FIND;
  }
  set_var(FLOW_GLOBAL_VARIABLE_SB_WIFI_ICON, wifi);
  // USB: hidden without a cable, grey when plugged in, orange while the Claude app talks over it.
  set_var(FLOW_GLOBAL_VARIABLE_SB_USB_ICON, power_on_usb() || link_usb_active() ? ICON_USB : "");
  const bool wifi_up = wifi_status() == WifiStatus::CONNECTED;
  status_icon_colors(ble_secure(), wifi_up ? (link_hooks_active() ? 2 : 1) : 0, link_usb_active());
  static const char *const BARS[] = {ICON_BATTERY_0_BAR, ICON_BATTERY_1_BAR, ICON_BATTERY_2_BAR, ICON_BATTERY_3_BAR,
                                     ICON_BATTERY_4_BAR, ICON_BATTERY_5_BAR, ICON_BATTERY_6_BAR};
  const int pct = power_battery_pct();
  const char *bat = pct < 0                         ? ""
                    : power_on_usb() && pct < 98    ? ICON_BATTERY_CHARGING_FULL
                    : pct >= 95                     ? ICON_BATTERY_FULL
                    : pct < 8                       ? ICON_BATTERY_ALERT
                                                    : BARS[pct * 7 / 100];
  set_var(FLOW_GLOBAL_VARIABLE_SB_BAT_ICON, bat);
  char clock[8] = "";
  if (link_rtc_valid()) {
    const time_t now = time(nullptr);
    tm lt;
    gmtime_r(&now, &lt);
    snprintf(clock, sizeof clock, "%02d:%02d", lt.tm_hour, lt.tm_min);
  }
  set_var(FLOW_GLOBAL_VARIABLE_CLOCK_TEXT, clock);

  set_var(FLOW_GLOBAL_VARIABLE_BT_STATUS_TEXT,
          ble_connected() ? (ble_secure() ? S(TrString::CONNECTED) : S(TrString::NOT_ENCRYPTED)) : ble_name());
  const char *w = S(TrString::NOT_CONNECTED);
  switch (wifi_status()) {
    case WifiStatus::CONNECTED: w = wifi_ssid(); break;
    case WifiStatus::CONNECTING: w = S(TrString::WIFI_CONNECTING); break;
    case WifiStatus::FAILED: w = S(TrString::WIFI_FAILED); break;
    default: break;
  }
  set_var(FLOW_GLOBAL_VARIABLE_WIFI_STATUS_TEXT, w);
}

// ---------------------------------------------------------------- screens

void home() {
  const Tama &t = tama();
  char b[96];
  show(objects.home_wait, t.waiting > 0 || buddy_in_prompt());
  snprintf(b, sizeof b, S(TrString::N_WAITING), max<int>(t.waiting, 1));
  set_label(objects.home_wait_text, b);
  snprintf(b, sizeof b, "Lv %u  %s", stats().level, pet_name());
  set_label(objects.home_level, b);
  if (!t.connected) {
    set_label(objects.home_who, S(TrString::NO_CLAUDE));
  } else {
    char what[48];
    if (t.running) snprintf(what, sizeof what, S(TrString::N_RUNNING), t.running);
    else strlcpy(what, S(TrString::NOTHING_RUNNING), sizeof what);
    snprintf(b, sizeof b, "%s  ·  %s", link_demo() ? S(TrString::DEMO) : S(TrString::CLAUDE), what);
    set_label(objects.home_who, b);
  }
  static char last_msg[96] = "\x01";
  if (strcmp(last_msg, t.msg)) {
    strlcpy(last_msg, t.msg, sizeof last_msg);
    set_var(FLOW_GLOBAL_VARIABLE_LAST_MESSAGE, t.msg);
  }
  show(objects.home_bubble, settings().hud);
}

void approval() {
  const Tama &t = tama();
  const bool p = buddy_in_prompt();
  const char *tool = p ? t.prompt_tool : "";
  set_label(objects.approval_tool, p ? tool : S(TrString::NOTHING_TO_APPROVE));
  const bool question = strstr(tool, "Question");
  const bool edit = strstr(tool, "Edit") || strstr(tool, "Write");
  const bool run = !strcmp(tool, "Bash");
  set_label(objects.approval_icon, question ? ICON_QUESTION_MARK : edit ? ICON_EDIT : run ? ICON_TERMINAL : ICON_SMART_TOY);
  const char *choices = link_question_choices();
  const bool show_choices = p && question && *choices;
  const char *sub = !p ? "" : show_choices ? choices : question ? S(TrString::ASKS_QUESTION)
                                      : edit ? S(TrString::WANTS_TO_EDIT)
                                      : run ? S(TrString::WANTS_TO_RUN) : S(TrString::WANTS_TO_USE);
  if (strcmp(lv_label_get_text(objects.approval_sub), sub)) {
    char note[140];
    snprintf(note, sizeof note, "[ui] approval sub '%s'", sub);
    link_debug_note(note);
  }
  set_label(objects.approval_sub, sub);
  // The question's choices stand out: bigger and in the accent colour (the board can't pick one).
  lv_obj_set_style_text_font(objects.approval_sub, show_choices ? &ui_font_t14 : &ui_font_t12, 0);
  lv_obj_set_style_text_color(objects.approval_sub, show_choices ? THEME_ACCENT : THEME_DIM, 0);
  set_var(FLOW_GLOBAL_VARIABLE_PROMPT_HINT, p ? t.prompt_hint : "");
  const uint32_t age = p ? buddy_prompt_age_s() : 0;
  char b[16];
  snprintf(b, sizeof b, "%lu:%02lu", (unsigned long)(age / 60), (unsigned long)(age % 60));
  set_label(objects.approval_timer, b);
  lv_obj_set_style_text_color(objects.approval_timer, age >= 10 ? THEME_BAD : THEME_WARN, 0);   // reference: red after 10 s

  // "Always" (allow and add the rule) only works through the hooks; for the app: two wide buttons.
  const bool always = p && link_prompt_from_hook() && !question;
  static uint8_t sized_layout = 0xFF;
  if (always == lv_obj_has_flag(objects.approval_always, LV_OBJ_FLAG_HIDDEN) || sized_layout != layout_gen) {
    sized_layout = layout_gen;
    show(objects.approval_always, always);
    if (ui_portrait()) {   // Approve on its own row (gen_ui.py); Deny takes the second row alone
      lv_obj_set_width(objects.approval_deny, always ? (ui_width() - 26) / 2 : ui_width() - 20);
    } else {
      lv_obj_set_width(objects.approval_deny, always ? 96 : 146);
      lv_obj_set_width(objects.approval_allow, always ? 96 : 146);
      lv_obj_set_x(objects.approval_allow, always ? 214 : 164);
    }
  }
}

void pet_screen() {
  char b[64], species[24];
  strlcpy(species, pet_species_name(), sizeof species);
  // The character on top (DevelopKing: "DK" and a crown); under it whose pet it is: "Alex's Rex"
  // (the order follows the language).
  const bool king = !strncasecmp(species, "developking", 11);
  set_label(objects.pet_name, king ? "DK" : species);
  show(objects.pet_crown, king);
  if (king) {
    const lv_font_t *f = lv_obj_get_style_text_font(objects.pet_name, 0);
    const int32_t w = lv_text_get_width("DK", 2, f, lv_obj_get_style_text_letter_space(objects.pet_name, 0));
    lv_obj_set_pos(objects.pet_crown, lv_obj_get_x(objects.pet_name) + w + 6, lv_obj_get_y(objects.pet_name) + 3);
  }
  if (owner_name()[0]) {
    const char *f = S(TrString::OWNER_PET);   // "{o}" = the owner, "{p}" = the pet
    size_t n = 0;
    for (; *f && n < sizeof b - 1; f++) {
      const char *put = nullptr;
      if (!strncmp(f, "{o}", 3)) put = owner_name();
      else if (!strncmp(f, "{p}", 3)) put = pet_name();
      if (!put) {
        b[n++] = *f;
        continue;
      }
      n += strlcpy(b + n, put, sizeof b - n);
      n = min(n, sizeof b - 1);
      f += 2;
    }
    b[n] = 0;
  } else {
    strlcpy(b, pet_name(), sizeof b);
  }
  set_label(objects.pet_owner, b);
  snprintf(b, sizeof b, "Lv %u", stats().level);
  set_label(objects.pet_level, b);
  lv_bar_set_value(objects.pet_mood, stats_mood_tier() * 25, LV_ANIM_ON);
  lv_bar_set_value(objects.pet_fed, (stats_fed_progress() + 1) * 10, LV_ANIM_ON);
  lv_bar_set_value(objects.pet_energy, stats_energy_tier() * 20, LV_ANIM_ON);
  snprintf(b, sizeof b, "%u", stats().approvals);
  set_label(objects.pet_stat_0, b);
  snprintf(b, sizeof b, "%u", stats().denials);
  set_label(objects.pet_stat_1, b);
  // Tokens: total (levels), today, this chat, last hour, 3 s each; the last two only from the hooks.
  const Tama &t = tama();
  const uint32_t values[] = {stats().tokens, t.tokens_today, t.tokens_session, t.tokens_hour};
  static const TrString names[] = {TrString::TOKENS_TOTAL, TrString::TOKENS_TODAY, TrString::TOKENS_SESSION,
                                   TrString::TOKENS_HOUR};
  int shown = (millis() / 3000) % 4;
  while (shown && !values[shown]) shown--;   // unknown: show an earlier one
  fmt_count(b, sizeof b, values[shown]);
  set_label(objects.pet_stat_2, b);
  set_label(objects.pet_stat_2_name, S(names[shown]));
  // Napped, and every other 4 s the time left until the usage limit resets (an estimate: "~").
  const int32_t left = companion_usage_left();
  if (left > 0 && (millis() / 4000) % 2) {
    char d[24];
    fmt_duration(d, sizeof d, left);
    snprintf(b, sizeof b, "~%s", d);
    set_label(objects.pet_stat_3_name, S(TrString::USAGE_RESETS));
  } else {
    fmt_duration(b, sizeof b, stats().nap_seconds);
    set_label(objects.pet_stat_3_name, S(TrString::NAPPED));
  }
  set_label(objects.pet_stat_3, b);
}

void info() {
  char b[80];
  snprintf(b, sizeof b, "%u KB free  ·  PSRAM %u KB free", unsigned(ESP.getFreeHeap() / 1024), unsigned(ESP.getFreePsram() / 1024));
  set_label(objects.info_value_1, b);
  snprintf(b, sizeof b, "%s  ·  %s", ble_name(),
           ble_connected() ? (ble_secure() ? S(TrString::ENCRYPTED) : S(TrString::NOT_ENCRYPTED)) : S(TrString::NOT_CONNECTED));
  set_label(objects.info_value_2, b);
  if (wifi_status() == WifiStatus::CONNECTED) snprintf(b, sizeof b, "%s  ·  %s", wifi_ssid(), wifi_ip());
  else strlcpy(b, S(TrString::NOT_CONNECTED), sizeof b);
  set_label(objects.info_value_3, b);
  const int pct = power_battery_pct();
  if (pct < 0) snprintf(b, sizeof b, "USB");
  else snprintf(b, sizeof b, "%d%%  ·  %d.%02d V  ·  %s", pct, power_battery_mv() / 1000, power_battery_mv() % 1000 / 10,
                power_on_usb() ? S(TrString::CHARGING) : S(TrString::ON_BATTERY));
  set_label(objects.info_value_4, b);
  fmt_duration(b, sizeof b, millis() / 1000);
  set_label(objects.info_value_5, b);
  snprintf(b, sizeof b, "Vibe-a-Gochi " FW_VERSION "  ·  %u/%u KB", unsigned(LittleFS.usedBytes() / 1024),
           unsigned(LittleFS.totalBytes() / 1024));
  set_label(objects.info_value_6, b);
}

void clock_screen() {
  char b[64];
  if (link_rtc_valid()) {
    const time_t now = time(nullptr);
    tm lt;
    gmtime_r(&now, &lt);
    const char *day = S(TrString(int(TrString::DAY_0) + lt.tm_wday));
    const char *mon = S(TrString(int(TrString::MON_0) + lt.tm_mon));
    switch (i18n_ui()) {
      case 0: snprintf(b, sizeof b, "%s, %s %d", day, mon, lt.tm_mday); break;      // Sunday, October 25
      case 1: snprintf(b, sizeof b, "%s, %d. %s", day, lt.tm_mday, mon); break;     // Sonntag, 25. Oktober
      case 4: snprintf(b, sizeof b, "%s, %d de %s", day, lt.tm_mday, mon); break;   // domingo, 25 de outubro
      default: snprintf(b, sizeof b, "%s %d %s", day, lt.tm_mday, mon); break;      // dimanche 25 octobre
    }
    capitalize(b);
  } else {
    b[0] = 0;
  }
  set_label(objects.clock_date, b);
  char what[40];
  if (tama().running) snprintf(what, sizeof what, S(TrString::N_RUNNING), tama().running);
  else strlcpy(what, S(TrString::NOTHING_RUNNING), sizeof what);
  snprintf(b, sizeof b, "%s  ·  %s", power_on_usb() ? S(TrString::CHARGING) : S(TrString::ON_BATTERY), what);
  capitalize(b);
  set_label(objects.clock_note, b);
}

void voice_screen() {
  const VoiceState v = voice_state();
  const char *status = !companion_connected()          ? S(TrString::COMPANION_OFF)
                       : voice_muted()                 ? S(TrString::MUTED)
                       : v == VoiceState::LISTENING    ? S(TrString::LISTENING)
                       : v == VoiceState::THINKING     ? S(TrString::THINKING)
                       : v == VoiceState::SPEAKING     ? S(TrString::SPEAKING)
                                                       : S(TrString::TAP_TO_TALK);
  set_label(objects.voice_status, status);
  lv_arc_set_value(objects.voice_level, voice_level());
  show(objects.voice_headset, companion_connected());
  set_label(objects.voice_mute_icon, voice_muted() ? ICON_MIC_OFF : ICON_MIC);
  lv_obj_set_style_text_color(objects.voice_mute_icon, voice_muted() ? THEME_ACCENT : THEME_DIM, 0);
}

void headset_screen() {
  const bool on = companion_connected();
  set_label(objects.headset_state, on ? S(TrString::CONNECTED) : S(TrString::COMPANION_OFF));
  lv_obj_set_style_text_color(objects.headset_state, on ? THEME_OK : THEME_DIM, 0);
  char b[80];
  if (on) snprintf(b, sizeof b, "%s  ·  WiFi  ·  %s", companion_peer(), wifi_ip());
  else snprintf(b, sizeof b, "%s.local", wifi_host());
  set_label(objects.headset_pc, b);
  lv_bar_set_value(objects.headset_mic_level, sound_mic_level(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(objects.headset_mute, voice_muted() ? THEME_ACCENT_SOFT : THEME_SURFACE, 0);
}

void install_screen() {
  static bool was_active = false;
  if (was_active && !xfer_active()) {   // finished
    char b[64];
    snprintf(b, sizeof b, S(TrString::INSTALLED), xfer_name());
    set_label(objects.install_status, b);
    ui_toast(b);
    sound_play(Sound::DONE);
  }
  was_active = xfer_active();
  if (!xfer_active()) return;
  char b[80];
  const uint32_t total = xfer_total();
  snprintf(b, sizeof b, S(TrString::INSTALLING), xfer_name(), total ? int(uint64_t(xfer_progress()) * 100 / total) : 0);
  set_label(objects.install_status, b);
}

void commands_screen() {
  set_label(objects.custom_command_text, custom_command()[0] ? custom_command() : S(TrString::NEW_COMMAND));
}

// ---------------------------------------------------------------- runtime lists

void chat_list() {
  static uint16_t built = 0xFFFF, built_lines = 0xFFFF;
  static uint8_t built_layout = 0;
  if (built == chat_gen() && built_lines == tama().line_gen && built_layout == layout_gen) return;
  built = chat_gen();
  built_lines = tama().line_gen;
  built_layout = layout_gen;
  lv_obj_t *list = objects.chat_list;
  clear_list(list);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_style_pad_hor(list, 10, 0);
  lv_obj_set_style_pad_ver(list, 6, 0);
  auto bubble = [&](ChatKind kind, const char *msg) {
    lv_obj_t *row = plain(list);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, kind == ChatKind::MINE ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_t *b = plain(row);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    if (kind == ChatKind::TOOL) {
      lv_obj_set_style_bg_color(b, THEME_RAISED, 0);
      lv_obj_set_style_radius(b, 11, 0);
      lv_obj_set_style_pad_hor(b, 8, 0);
      lv_obj_set_style_pad_ver(b, 3, 0);
      lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_column(b, 4, 0);
      text(b, ICON_CHECK, &ui_font_i16, THEME_OK);
      lv_obj_t *l = text(b, msg, &ui_font_m12, THEME_DIM);
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
      lv_obj_set_style_max_width(l, ui_width() - 100, 0);
      return;
    }
    const bool mine = kind == ChatKind::MINE;
    lv_obj_set_style_bg_color(b, mine ? THEME_ACCENT : THEME_SURFACE, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 6, 0);
    lv_obj_t *l = text(b, msg, &ui_font_t14, mine ? THEME_ON_ACCENT : THEME_TEXT);
    const int max_w = mine ? 180 : min(218, ui_width() - 40);   // portrait: Claude's bubbles nearly full width
    lv_point_t sz;
    lv_text_get_size(&sz, msg, &ui_font_t14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(l, min<int>(sz.x + 1, max_w));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  };
  if (chat_count()) {
    for (int i = 0; i < chat_count(); i++) bubble(chat_at(i).kind, chat_at(i).text);
  } else if (companion_chat_pinned()) {
    // A chat opened or started here: empty means empty (not the activity of other sessions).
  } else if (tama().n_lines) {   // Claude app session: the transcript lines from the heartbeat
    for (int i = 0; i < tama().n_lines; i++) bubble(ChatKind::CLAUDE, tama().lines[i]);
  } else {
    bubble(ChatKind::CLAUDE, companion_connected() ? "…" : S(TrString::NO_CLAUDE));
  }
  lv_obj_update_layout(list);
  lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
}

void project_clicked(lv_event_t *e) {
  companion_open_project(int(intptr_t(lv_event_get_user_data(e))));
  ui_go(SCREEN_ID_CHATS, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

void chat_clicked(lv_event_t *e) {
  companion_open_chat(int(intptr_t(lv_event_get_user_data(e))));
  ui_go(SCREEN_ID_CHAT, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

// Assistant chats: a long press on an old one, or the last row (all old ones), asks first.
int delete_pick = -1;
void delete_answered(int choice) {
  if (choice != 0) return;
  companion_delete_chat(delete_pick);
  ui_toast(S(TrString::DELETED));
}
void delete_ask(int i) {
  delete_pick = i;
  const char *choices[] = {S(TrString::DELETE), S(TrString::CANCEL)};
  char q[120];
  if (i < 0) strlcpy(q, S(TrString::DELETE_OLD_Q), sizeof q);
  else snprintf(q, sizeof q, "%s\n\n%s", S(TrString::DELETE_CHAT_Q), companion_chat(i).title);
  ui_ask(q, choices, 2, delete_answered, "Assistant");
}
void chat_long_pressed(lv_event_t *e) {
  lv_indev_wait_release(lv_indev_active());   // no tap when the finger lifts
  delete_ask(int(intptr_t(lv_event_get_user_data(e))));
}
void delete_old_clicked(lv_event_t *) { delete_ask(-1); }

void project_lists() {
  static uint16_t built = 0xFFFF;
  static bool built_online = false;
  static uint8_t built_layout = 0;
  if (built == companion_lists_gen() && built_online == companion_connected() && built_layout == layout_gen) return;
  built_layout = layout_gen;
  built = companion_lists_gen();
  built_online = companion_connected();
  if (!built_online) {
    info_row(objects.projects_list, S(TrString::COMPANION_OFF));
    info_row(objects.chats_list, S(TrString::COMPANION_OFF));
    return;
  }
  clear_list(objects.projects_list);
  for (int i = 0; i < companion_project_count(); i++) {
    const Project &p = companion_project(i);
    char n[16];
    snprintf(n, sizeof n, "%u", p.chats);
    lv_obj_t *r = list_row(objects.projects_list, i * 52, p.assistant ? ICON_SMART_TOY : ICON_FOLDER, p.assistant,
                           p.name, p.assistant ? S(TrString::ASSISTANT_SUB) : p.path, n);
    lv_obj_add_event_cb(r, project_clicked, LV_EVENT_CLICKED, (void *)intptr_t(i));
  }
  clear_list(objects.chats_list);
  int old = 0;
  for (int i = 0; i < companion_chat_count(); i++) {
    const ChatInfo &c = companion_chat(i);
    const bool top = companion_chats_deletable() && !c.old;   // the Assistant's current chat
    lv_obj_t *r = list_row(objects.chats_list, i * 52, top ? ICON_SMART_TOY : ICON_CHAT, c.running || top, c.title,
                           c.when, nullptr);
    lv_obj_add_event_cb(r, chat_clicked, LV_EVENT_SHORT_CLICKED, (void *)intptr_t(i));
    if (c.old) {
      lv_obj_add_event_cb(r, chat_long_pressed, LV_EVENT_LONG_PRESSED, (void *)intptr_t(i));
      old++;
    }
  }
  if (old) {   // the Assistant: delete all its old chats
    lv_obj_t *r = list_row(objects.chats_list, companion_chat_count() * 52, ICON_DELETE_FOREVER, false,
                           S(TrString::DELETE_OLD), S(TrString::DELETE_OLD_HINT), nullptr);
    lv_obj_add_event_cb(r, delete_old_clicked, LV_EVENT_CLICKED, nullptr);
  }
}

char wifi_pick[33];
void wifi_password(const char *pw) { wifi_connect(wifi_pick, pw); }

void wifi_clicked(lv_event_t *e) {
  const WifiNet &n = wifi_scan_at(int(intptr_t(lv_event_get_user_data(e))));
  strlcpy(wifi_pick, n.ssid, sizeof wifi_pick);
  if (!n.secure) return wifi_connect(wifi_pick, "");
  char title[64];
  snprintf(title, sizeof title, S(TrString::WIFI_PASSWORD), n.ssid);
  ui_keyboard(title, "", true, wifi_password);
}

void wifi_screen() {
  char b[96];
  switch (wifi_status()) {
    case WifiStatus::CONNECTED: snprintf(b, sizeof b, "%s  ·  %s  ·  %s.local", wifi_ssid(), wifi_ip(), wifi_host()); break;
    case WifiStatus::CONNECTING: snprintf(b, sizeof b, "%s  ·  %s", wifi_ssid(), S(TrString::WIFI_CONNECTING)); break;
    case WifiStatus::FAILED: snprintf(b, sizeof b, "%s  ·  %s", wifi_ssid(), S(TrString::WIFI_FAILED)); break;
    default: strlcpy(b, wifi_scanning() ? S(TrString::WIFI_SCANNING) : S(TrString::NOT_CONNECTED), sizeof b);
  }
  set_label(objects.wifi_status, b);
  static uint16_t built = 0xFFFF;
  static WifiStatus built_status = WifiStatus::OFF;
  static uint8_t built_layout = 0;
  if (built == wifi_scan_gen() && built_status == wifi_status() && built_layout == layout_gen) return;
  built_layout = layout_gen;
  built = wifi_scan_gen();
  built_status = wifi_status();
  if (!wifi_scan_count()) {
    info_row(objects.wifi_list, wifi_scanning() || !built ? S(TrString::WIFI_SCANNING) : S(TrString::WIFI_NONE));
    return;
  }
  clear_list(objects.wifi_list);
  for (int i = 0; i < wifi_scan_count(); i++) {
    const WifiNet &n = wifi_scan_at(i);
    const char *ic = n.rssi > -55 ? ICON_SIGNAL_WIFI_4_BAR : n.rssi > -65 ? ICON_NETWORK_WIFI_3_BAR
                     : n.rssi > -75 ? ICON_NETWORK_WIFI_2_BAR : ICON_NETWORK_WIFI_1_BAR;
    const bool current = wifi_status() == WifiStatus::CONNECTED && !strcmp(n.ssid, wifi_ssid());
    lv_obj_t *r = list_row(objects.wifi_list, i * 52, ic, current, n.ssid, current ? S(TrString::CONNECTED) : "",
                           n.secure ? ICON_LOCK : nullptr);
    if (n.secure) lv_obj_set_style_text_font(lv_obj_get_child(r, -2), &ui_font_i16, 0);   // the lock is an icon
    lv_obj_add_event_cb(r, wifi_clicked, LV_EVENT_CLICKED, (void *)intptr_t(i));
  }
}

// ---------------------------------------------------------------- characters

void character_clicked(lv_event_t *e) {
  pet_select(int(intptr_t(lv_event_get_user_data(e))));
  sound_play(Sound::APPROVE);
  ui_rebuild_characters();
}

void characters_loaded(lv_event_t *) { ui_rebuild_characters(); }

// ---------------------------------------------------------------- pet petting

// Tap the pet → heart; rub it (keep dragging over it) → dizzy. Replaces the reference's shake.
void pet_touched(lv_event_t *e) {
  static int32_t travel = 0;
  static lv_point_t last;
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) {
    travel = 0;
    last = p;
  } else if (code == LV_EVENT_PRESSING) {
    travel += abs(p.x - last.x) + abs(p.y - last.y);
    last = p;
    if (travel > 400) {
      buddy_one_shot(P_DIZZY, 2000);
      travel = INT32_MIN / 2;
    }
  } else if (code == LV_EVENT_CLICKED && travel < 40 && travel >= 0) {
    buddy_one_shot(P_HEART, 2000);
  }
}

void make_pettable(lv_obj_t *img) {
  lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(img, pet_touched, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(img, pet_touched, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(img, pet_touched, LV_EVENT_CLICKED, nullptr);
}

// Tapping a settings row flips its switch (the switch alone is a small target).
void row_clicked(lv_event_t *e) {
  lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  if (lv_obj_has_state(sw, LV_STATE_CHECKED)) lv_obj_remove_state(sw, LV_STATE_CHECKED);
  else lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
}

void sync_switch(lv_obj_t *sw, bool on) {
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  else lv_obj_remove_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(lv_obj_get_parent(sw), row_clicked, LV_EVENT_CLICKED, sw);
}

// ---------------------------------------------------------------- navigation state

bool approval_had_prompt = false;
bool pairing_auto = false;
uint32_t pairing_done_at = 0;
int reset_armed = -1;
uint32_t reset_armed_at = 0;

void auto_navigation() {
  lv_obj_t *scr = lv_screen_active();
  if (buddy_prompt_arrived()) {
    approval_had_prompt = true;
    ui_go(SCREEN_ID_APPROVAL, LV_SCR_LOAD_ANIM_OVER_TOP);
  }
  if (scr == objects.approval) {
    if (buddy_in_prompt()) approval_had_prompt = true;
    else if (approval_had_prompt) {   // answered here or on the computer
      approval_had_prompt = false;
      ui_back(LV_SCR_LOAD_ANIM_OUT_BOTTOM);
    }
  }

  // Pairing codes: BLE (the computer asks) or a new companion.
  const uint32_t code = ble_passkey() ? ble_passkey() : companion_pair_code();
  if (code) {
    char b[8];
    snprintf(b, sizeof b, "%03lu %03lu", (unsigned long)(code / 1000), (unsigned long)(code % 1000));
    set_label(objects.pair_code, b);
    set_label(objects.pair_hint, ble_passkey() ? S(TrString::PAIR_TYPE) : S(TrString::PAIR_COMPANION));
    if (scr != objects.pairing) {
      pairing_auto = true;
      power_wake();
      sound_play(Sound::QUESTION);
      ui_go(SCREEN_ID_PAIRING, LV_SCR_LOAD_ANIM_OVER_TOP);
    }
    pairing_done_at = 0;
  } else if (scr == objects.pairing) {
    set_label(objects.pair_code, "--- ---");
    set_label(objects.pair_hint, ble_secure() ? S(TrString::PAIR_DONE) : ble_connected() ? S(TrString::NOT_ENCRYPTED)
                                                                      : S(TrString::PAIR_WAITING));
    if (pairing_auto) {
      if (!pairing_done_at) pairing_done_at = millis();
      if (millis() - pairing_done_at > 1500) {
        pairing_auto = false;
        ui_back(LV_SCR_LOAD_ANIM_OUT_BOTTOM);
      }
    }
  }

  // Idle on USB with the time known and nothing going on: the clock face (reference behaviour).
  const Tama &t = tama();
  if (scr == objects.home && link_rtc_valid() && power_on_usb() && !t.running && !t.waiting && !buddy_in_prompt() &&
      power_idle_ms() > 45000)
    ui_go(SCREEN_ID_CLOCK, LV_SCR_LOAD_ANIM_FADE_IN);

  if (reset_armed >= 0 && millis() - reset_armed_at > 3000) {
    lv_obj_t *hints[] = {objects.reset_hint_0, objects.reset_hint_1, objects.reset_hint_2};
    set_label(hints[reset_armed], "");
    reset_armed = -1;
  }
}

}  // namespace

// ---------------------------------------------------------------- public

void ui_go(ScreensEnum screen, lv_scr_load_anim_t anim) {
  // In ScreensEnum order (ui/screens.h).
  lv_obj_t *const targets[] = {nullptr,          objects.home,     objects.approval, objects.chat,
                               objects.voice,    objects.pet,      objects.more,     objects.today,
                               objects.characters, objects.install, objects.commands, objects.settings,
                               objects.headset,  objects.info,     objects.clock,    objects.pairing,
                               objects.projects, objects.chats,    objects.language, objects.speech_language,
                               objects.orientation, objects.wifi,  objects.reset};
  static_assert(sizeof targets / sizeof *targets == _SCREEN_ID_LAST + 1, "a screen is missing here");
  if (screen < _SCREEN_ID_FIRST || screen > _SCREEN_ID_LAST || lv_screen_active() == targets[screen]) return;
  eez_flow_push_screen(screen, anim, 220, 0);
}

void ui_back(lv_scr_load_anim_t anim) { eez_flow_pop_screen(anim, 220, 0); }

void ui_reset_tap(int item) {
  lv_obj_t *hints[] = {objects.reset_hint_0, objects.reset_hint_1, objects.reset_hint_2};
  if (reset_armed != item || millis() - reset_armed_at > 3000) {
    if (reset_armed >= 0) set_label(hints[reset_armed], "");
    reset_armed = item;
    reset_armed_at = millis();
    set_label(hints[item], S(TrString::CONFIRM));
    if (lv_screen_active() != objects.reset) ui_toast(S(TrString::CONFIRM));   // e.g. Forget pairing on the pairing screen
    sound_play(Sound::ATTENTION);
    return;
  }
  reset_armed = -1;
  set_label(hints[item], "");
  sound_play(Sound::DENY);
  switch (item) {
    case 0:
      pet_delete_all_packs();
      ui_rebuild_characters();
      ui_toast(S(TrString::SENT));
      break;
    case 1:
      ble_clear_bonds();
      companion_forget();
      ui_toast(S(TrString::NOT_CONNECTED));
      break;
    case 2: {
      set_label(hints[item], S(TrString::RESTARTING));
      lv_refr_now(nullptr);
      ble_clear_bonds();
      companion_forget();
      wifi_forget();
      LittleFS.format();
      nvs_flash_erase();
      delay(300);
      ESP.restart();
    }
  }
}

void ui_rebuild_characters() {
  lv_obj_t *grid = objects.characters_grid;
  clear_list(grid);
  lv_obj_update_layout(grid);
  const int cols = max<int>(1, (lv_obj_get_width(grid) - 4) / 100);   // 3 landscape, 2 portrait
  const int step = (lv_obj_get_width(grid) - 4) / cols;
  for (int i = 0; i < pet_count(); i++) {
    const bool sel = i == pet_selected();
    lv_obj_t *c = card(grid, step - 6, 86, THEME_SURFACE, 14);
    lv_obj_set_pos(c, 4 + (i % cols) * step, 4 + (i / cols) * 92);
    if (sel) {
      lv_obj_set_style_border_color(c, THEME_ACCENT, 0);
      lv_obj_set_style_border_width(c, 2, 0);
    }
    if (const lv_image_dsc_t *th = pet_thumb(i)) {
      lv_obj_t *img = lv_image_create(c);
      lv_image_set_src(img, th);
      lv_obj_align(img, LV_ALIGN_CENTER, 0, -10);
    }
    lv_obj_t *n = text(c, pet_entry_name(i), &ui_font_t12, THEME_TEXT);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_set_width(n, 46);
    lv_obj_align(n, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_t *tag = plain(c);
    lv_obj_set_size(tag, 34, 14);
    lv_obj_align(tag, LV_ALIGN_BOTTOM_RIGHT, -6, -8);
    lv_obj_set_style_radius(tag, 7, 0);
    lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
    const bool gif = pet_entry_is_gif(i);
    lv_obj_set_style_bg_color(tag, gif ? THEME_INFO : THEME_RAISED, 0);
    lv_obj_center(text(tag, gif ? "GIF" : "ASCII", &ui_font_t9, gif ? lv_color_hex(0x0B1626) : THEME_DIM));
    if (sel) {
      lv_obj_t *ck = plain(c);
      lv_obj_set_size(ck, 18, 18);
      lv_obj_align(ck, LV_ALIGN_TOP_RIGHT, -6, 6);
      lv_obj_set_style_radius(ck, 9, 0);
      lv_obj_set_style_bg_color(ck, THEME_ACCENT, 0);
      lv_obj_set_style_bg_opa(ck, LV_OPA_COVER, 0);
      lv_obj_center(text(ck, ICON_CHECK, &ui_font_i16, THEME_ON_ACCENT));
    }
    lv_obj_add_event_cb(c, character_clicked, LV_EVENT_CLICKED, (void *)intptr_t(i));
  }
}

// ---------------------------------------------------------------- today (daily summary)

void today_screen() {
  static uint16_t shown = 0xFFFF;
  static uint8_t shown_layout = 0;
  static bool shown_online = false;
  if (shown == summary_gen() && shown_layout == layout_gen && shown_online == companion_connected()) return;
  shown = summary_gen();
  shown_layout = layout_gen;
  shown_online = companion_connected();
  const DaySummary &d = summary_data();
  const bool have = summary_have();
  set_label(objects.today_title, S(summary_day() ? TrString::YESTERDAY : TrString::TODAY));
  char b[160];
  fmt_duration(b, sizeof b, uint32_t(have ? d.active : 0) * 60);
  set_label(objects.today_val_0, have ? b : "–");
  snprintf(b, sizeof b, "%u", d.prompts);
  set_label(objects.today_val_1, have ? b : "–");
  fmt_count(b, sizeof b, d.tokens);
  set_label(objects.today_val_2, have ? b : "–");
  snprintf(b, sizeof b, "%u", d.files);
  set_label(objects.today_val_3, have ? b : "–");

  // The recap card: Claude's recap, or what is happening (asking, writing, nothing that day).
  const SummaryWait w = summary_wait();
  const char *recap = !companion_connected() && !have           ? S(TrString::COMPANION_OFF)
                      : have && !d.prompts                       ? S(TrString::SUMMARY_NONE)
                      : w == SummaryWait::NUMBERS                ? S(TrString::SUMMARY_LOADING)
                      : !settings().recap                        ? ""
                      : d.recap[0]                               ? d.recap
                      : w == SummaryWait::RECAP                  ? S(TrString::RECAP_WRITING)
                                                                 : "";
  set_label(objects.today_recap, recap);
  show(objects.today_recap_card, recap[0] != 0);

  uint16_t appr = 0, den = 0;
  stats_day_answers(summary_day(), appr, den);
  char line[200];
  snprintf(line, sizeof line, S(TrString::CHATS_LINE), d.chats, d.replies);
  if (d.projects[0]) snprintf(line + strlen(line), sizeof line - strlen(line), "\n%s", d.projects);
  set_label(objects.today_projects, line);
  set_label(objects.today_tools, d.tools);
  show(objects.today_tools, d.tools[0] != 0);
  snprintf(b, sizeof b, S(TrString::BOARD_LINE), appr, den);
  set_label(objects.today_board, b);
  show(objects.today_details, have);

  // The week: bars scaled to its busiest day (at least an hour), the shown day in the accent colour.
  uint16_t top = 60;
  for (uint16_t m : d.week) top = max(top, m);
  lv_obj_t *bars[] = {objects.week_bar_0, objects.week_bar_1, objects.week_bar_2, objects.week_bar_3,
                      objects.week_bar_4, objects.week_bar_5, objects.week_bar_6};
  lv_obj_t *days[] = {objects.week_day_0, objects.week_day_1, objects.week_day_2, objects.week_day_3,
                      objects.week_day_4, objects.week_day_5, objects.week_day_6};
  const char *letters = S(TrString::WEEKDAYS);
  for (int i = 0; i < 7; i++) {
    lv_bar_set_range(bars[i], 0, top);
    lv_bar_set_value(bars[i], have ? d.week[i] : 0, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bars[i], i == 6 ? THEME_ACCENT : THEME_DIM, LV_PART_INDICATOR);
    const char letter[2] = {strlen(letters) == 7 ? letters[(d.week_start + i) % 7] : ' ', 0};
    set_label(days[i], letter);
  }
  snprintf(b, sizeof b, S(TrString::STREAK), d.streak);
  set_label(objects.today_streak, b);
  show(lv_obj_get_parent(objects.today_streak), have && d.streak > 0);
}

void ui_live_relayout() {
  layout_gen++;   // chat, projects, chats, WiFi lists and the approval buttons: on their next tick
  if (lv_screen_active() == objects.characters) ui_rebuild_characters();   // else when it opens
}

void ui_live_begin() {
  for (auto &v : var_cache) v[0] = 1;   // differs from every text: the first set always lands
  // Lists start empty: what the designer shows there is sample data.
  for (lv_obj_t *l : {objects.chat_list, objects.projects_list, objects.chats_list, objects.wifi_list,
                      objects.characters_grid})
    clear_list(l);
  make_pettable(objects.home_pet);
  make_pettable(objects.pet_pet);
  // Today: the cards stack in a column (their heights follow the text), the details too.
  lv_obj_set_flex_flow(objects.today_body, LV_FLEX_FLOW_COLUMN);
  // Its cards take no taps, so the body itself must, or a swipe finds nothing to scroll.
  lv_obj_add_flag(objects.today_body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_row(objects.today_body, 8, 0);
  lv_obj_set_style_pad_bottom(objects.today_body, 10, 0);
  lv_obj_set_flex_flow(objects.today_details, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(objects.today_details, 4, 0);
  for (lv_obj_t *l : {objects.today_recap, objects.today_projects, objects.today_tools, objects.today_board})
    lv_label_set_text(l, "");   // a copy, not the designer's text in flash
  lv_obj_add_event_cb(objects.characters, characters_loaded, LV_EVENT_SCREEN_LOADED, nullptr);

  const Settings &s = settings();
  lv_slider_set_value(objects.brightness_slider, s.brightness, LV_ANIM_OFF);
  lv_slider_set_value(objects.volume_slider, sound_volume(), LV_ANIM_OFF);
  lv_slider_set_value(objects.speaker_slider, s.speaker, LV_ANIM_OFF);
  sync_switch(objects.sound_switch, sound_enabled());
  sync_switch(objects.read_aloud_switch, s.read_aloud);
  sync_switch(objects.recap_switch, s.recap);
  sync_switch(objects.led_switch, s.led);
  sync_switch(objects.nap_switch, s.nap);
  sync_switch(objects.hud_switch, s.hud);
  sync_switch(objects.demo_switch, link_demo());
  set_label(objects.info_value_0, "Hosyond ES3C28P  ·  ESP32-S3");
  set_label(objects.install_status, "");
  for (lv_obj_t *h : {objects.reset_hint_0, objects.reset_hint_1, objects.reset_hint_2}) set_label(h, "");
  set_var(FLOW_GLOBAL_VARIABLE_VOICE_TRANSCRIPT, "");
  set_var(FLOW_GLOBAL_VARIABLE_CURRENT_PROJECT, "");
  set_var(FLOW_GLOBAL_VARIABLE_CURRENT_CHAT, "");
}

void ui_live_tick() {
  auto_navigation();
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();
  status_bar();
  static const TrString ORIENT[] = {TrString::ORIENT_0, TrString::ORIENT_1, TrString::ORIENT_2, TrString::ORIENT_3};
  set_var(FLOW_GLOBAL_VARIABLE_SCREEN_ORIENTATION_NAME, S(ORIENT[settings().rotation & 3]));
  set_var(FLOW_GLOBAL_VARIABLE_OWNER_NAME_TEXT, owner_name());
  set_var(FLOW_GLOBAL_VARIABLE_PET_NAME_TEXT, pet_name());
  lv_obj_t *scr = lv_screen_active();
  if (scr == objects.home) home();
  else if (scr == objects.approval) approval();
  else if (scr == objects.pet) pet_screen();
  else if (scr == objects.info) info();
  else if (scr == objects.clock) clock_screen();
  else if (scr == objects.voice) voice_screen();
  else if (scr == objects.headset) headset_screen();
  else if (scr == objects.commands) commands_screen();
  else if (scr == objects.wifi) wifi_screen();
  else if (scr == objects.today) today_screen();
  install_screen();
  if (scr == objects.chat) {
    chat_list();
    // Stop while Claude works (not again once a stop was sent: it acts at Claude's next step).
    // Landscape: shown only then; portrait: a grid cell, dimmed the rest of the time.
    lv_obj_t *stop = objects.chat_stop;
    const bool can = tama().running && !companion_stop_pending(), show = can || ui_portrait();
    if (show == lv_obj_has_flag(stop, LV_OBJ_FLAG_HIDDEN)) {
      if (show) lv_obj_remove_flag(stop, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(stop, LV_OBJ_FLAG_HIDDEN);
    }
    if (can == lv_obj_has_state(stop, LV_STATE_DISABLED)) {
      lv_obj_set_state(stop, LV_STATE_DISABLED, !can);   // disabled: no taps
      lv_obj_set_style_opa(stop, can ? LV_OPA_COVER : LV_OPA_40, 0);
    }
  }
  if (scr == objects.projects || scr == objects.chats) project_lists();
  if (xfer_active() || scr == objects.approval) home();   // keep the waiting pill right for the way back
}
