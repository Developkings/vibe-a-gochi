// The PC companion link (docs/companion.md): one WebSocket at ws://<device>:8787/ws, JSON text
// frames for control, binary frames for ADPCM audio. A companion must present the token it got
// when it was paired with the 6-digit code shown on the device; others are ignored.
#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint16_t COMPANION_PORT = 8787;

struct Project {
  char name[40];
  char path[80];
  uint16_t chats;
  bool assistant;   // the board's Assistant (companion/sessions.py)
};

struct ChatInfo {
  char id[48];
  char title[72];
  char when[24];
  bool running;
  bool old;   // an Assistant chat you left: it can be deleted
};

void companion_begin();     // after WiFi is up (wifi_link.cpp)
void companion_tick();      // main loop: handles queued messages (LVGL is not thread-safe)
bool companion_connected(); // a paired companion is connected
const char *companion_peer();   // its computer name
uint32_t companion_pair_code(); // non-zero while a new companion waits for the code
void companion_forget();        // drop the paired token (Forget pairing / factory reset)

bool companion_send(const char *json);                  // control message
bool companion_send_to(uint32_t client, const char *json);   // to one helper (a hook)
bool companion_queue_for_hooks(const char *text);   // no companion: Claude Code's hooks deliver it
void companion_send_audio(const uint8_t *frame, size_t len);   // any task; dropped when the link is busy

// Commands (no-ops when not connected; return false then).
bool companion_send_text(const char *text);   // quick replies, commands, typed messages
bool companion_command(const char *cmd);      // e.g. "stop", "list_projects"
bool companion_stop();                        // Stop: the companion's run, and Claude Code via its hooks
bool companion_stop_pending();                // a stop waits for Claude Code's next step
void companion_open_project(int i);           // asks for its chats
void companion_open_chat(int i);
void companion_new_chat();
// Claude Code hook turns: true when the chat shows this session (the one opened on the device,
// or, before one is opened, the latest; a change of session clears the chat first).
bool companion_chat_accepts(const char *session, const char *project);
bool companion_chat_pinned();   // a chat was opened or started on the device
void companion_sync_settings();               // language + read aloud

int companion_project_count();
const Project &companion_project(int i);
int companion_chat_count();
const ChatInfo &companion_chat(int i);
uint16_t companion_lists_gen();               // bumps when projects or chats change
bool companion_chats_deletable();             // the chats shown are the Assistant's
void companion_delete_chat(int i);            // one old Assistant chat (-1: all old ones)
int32_t companion_usage_left();               // s left in the 5-hour usage window (estimate), -1 unknown
