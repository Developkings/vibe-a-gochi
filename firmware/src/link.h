// Hardware Buddy protocol (see reference/claude-desktop-buddy/REFERENCE.md): newline-delimited JSON
// from the Claude desktop app over BLE (or USB serial), the heartbeat state, commands and acks.
// Ported from the reference data.h / xfer.h.
#pragma once
#include <stdint.h>

struct Tama {
  uint8_t total, running, waiting;
  bool completed;           // a turn just finished
  uint32_t tokens_today;
  uint32_t tokens_session;  // the latest active session, and the last 60 min (hooks only, 0 = unknown)
  uint32_t tokens_hour;
  uint32_t last_updated;    // millis of the last heartbeat
  bool connected;           // live data in the last 30 s (or demo)
  char msg[96];
  char lines[8][92];        // recent transcript entries
  uint8_t n_lines;
  uint16_t line_gen;        // bumps when the entries change
  char prompt_id[40];       // pending permission request ("" = none)
  char prompt_tool[24];
  char prompt_hint[200];    // for AskUserQuestion: the question and its choices
};

void link_begin();
void link_poll();                       // read BLE + USB, apply JSON, run demo mode
const Tama &tama();
void link_send(const char *json_line);  // to the desktop (BLE and USB); adds the newline
void link_send_permission(const char *id, bool approve, bool always = false);   // to the app, or the hook that asked
bool link_prompt_from_hook();           // the shown request came over WiFi (it can be allowed "always")
bool link_app_live();                   // the Claude app sent something over USB / BLE in the last 30 s
bool link_hooks_active();               // Claude Code hooks sent status over WiFi in the last 5 min
void link_apply_remote(uint32_t client, const char *json);   // a line from a Claude Code hook (WiFi)
void link_remote_gone(uint32_t client); // that hook's connection closed
const char *link_question_choices();    // "Red · Blue" of the last AskUserQuestion
void link_debug_note(const char *note); // adds a line to the receive log ({"cmd":"rxlog"})
bool link_rtc_valid();                  // the desktop sent the time
void link_set_time(uint32_t epoch, int32_t tz_offset);   // from the PC companion (WiFi)
bool link_bt_active();                  // BLE traffic in the last 15 s
bool link_usb_active();                 // USB serial traffic in the last 15 s
void link_set_demo(bool on);
bool link_demo();

// Folder push progress (char_begin .. char_end).
bool xfer_active();
uint32_t xfer_progress();
uint32_t xfer_total();
const char *xfer_name();
