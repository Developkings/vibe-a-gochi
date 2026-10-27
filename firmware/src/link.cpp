#include "link.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <sys/time.h>
#include "ble_link.h"
#include "buddy.h"
#include "chat_log.h"
#include "companion.h"
#include "pet.h"
#include "power.h"
#include "settings.h"
#include "sound.h"
#include "stats.h"

bool xfer_command(JsonDocument &doc);   // xfer.cpp
void link_ack(const char *what, bool ok, uint32_t n = 0);

namespace {

Tama st;
uint32_t last_live_ms = 0, last_bt_byte_ms = 0, last_usb_byte_ms = 0;
// The same JSON can come over WiFi from the Claude Code hooks (companion/board_hook.py), for when
// the Claude app isn't connected. Hooks only fire on events (a long build sends nothing), so their
// data stays live for 30 min; the app's own link (USB / BLE) wins while it is live.
constexpr uint32_t REMOTE_LIVE_MS = 30 * 60000;
uint32_t last_remote_ms = 0;
bool from_remote = false;
uint32_t remote_client = 0;   // the hook connection applying a line right now
uint32_t prompt_client = 0;   // the hook waiting for the shown permission request (0 = the app)
bool rtc_valid = false;
bool demo = false;
uint8_t demo_idx = 0;
uint32_t demo_next = 0;

struct Fake {
  const char *name;
  uint8_t total, running, waiting;
  bool completed;
  uint32_t tokens;
};
const Fake FAKES[] = {{"asleep", 0, 0, 0, false, 0},
                      {"one idle", 1, 0, 0, false, 12000},
                      {"busy", 4, 3, 0, false, 89000},
                      {"attention", 2, 1, 1, false, 45000},
                      {"completed", 1, 0, 0, true, 142000}};

template <size_t N>
void copy(char (&dst)[N], const char *src) {
  strncpy(dst, src ? src : "", N - 1);
  dst[N - 1] = 0;
}

void send_status() {
  const int mv = power_battery_mv(), pct = max(power_battery_pct(), 0);
  char b[400];
  snprintf(b, sizeof b,
           "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{"
           "\"name\":\"%s\",\"owner\":\"%s\",\"sec\":%s,"
           "\"bat\":{\"pct\":%d,\"mV\":%d,\"mA\":0,\"usb\":%s},"
           "\"sys\":{\"up\":%lu,\"heap\":%u,\"fsFree\":%lu,\"fsTotal\":%lu},"
           "\"stats\":{\"appr\":%u,\"deny\":%u,\"vel\":%u,\"nap\":%lu,\"lvl\":%u}}}",
           pet_name(), owner_name(), ble_secure() ? "true" : "false", pct, mv, power_on_usb() ? "true" : "false",
           (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
           (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()), (unsigned long)LittleFS.totalBytes(),
           stats().approvals, stats().denials, stats_median_velocity(), (unsigned long)stats().nap_seconds,
           stats().level);
  link_send(b);
}

// Debug: the last lines received (any transport), cut to 380 bytes, printed by {"cmd":"rxlog"}.
constexpr int RXLOG_N = 40, RXLOG_LEN = 384;
char (*rxlog)[RXLOG_LEN] = nullptr;
int rxlog_head = 0;

void rxlog_add(const char *line) {
  static uint32_t last_hash = 0;   // skip repeats (unchanged heartbeats, status polls) so the ring keeps history
  uint32_t h = 2166136261u;
  for (const char *p = line; *p; p++) h = (h ^ uint8_t(*p)) * 16777619u;
  if (h == last_hash || strstr(line, "\"cmd\":\"status\"")) return;
  last_hash = h;
  if (!rxlog) rxlog = static_cast<char (*)[RXLOG_LEN]>(heap_caps_calloc(RXLOG_N, RXLOG_LEN, MALLOC_CAP_SPIRAM));
  if (!rxlog) return;
  snprintf(rxlog[rxlog_head], RXLOG_LEN, "%lu %s", (unsigned long)(millis() / 1000), line);
  rxlog_head = (rxlog_head + 1) % RXLOG_N;
}

void rxlog_dump() {
  Serial.println("[rxlog] begin");
  for (int i = 0; rxlog && i < RXLOG_N; i++) {
    const char *l = rxlog[(rxlog_head + i) % RXLOG_N];
    if (*l) Serial.printf("[rxlog] %s\n", l);
  }
  Serial.println("[rxlog] end");
}

bool command(JsonDocument &doc) {
  const char *cmd = doc["cmd"];
  if (!cmd) return false;
  if (!strcmp(cmd, "rxlog")) {
    rxlog_dump();
  } else if (!strcmp(cmd, "name")) {
    const char *n = doc["name"];
    if (n) pet_name_set(n);
    link_ack("name", n != nullptr);
  } else if (!strcmp(cmd, "owner")) {
    const char *n = doc["name"];
    if (n) owner_set(n);
    link_ack("owner", n != nullptr);
  } else if (!strcmp(cmd, "species")) {
    pet_select_species(doc["idx"] | SPECIES_GIF);
    link_ack("species", true);
  } else if (!strcmp(cmd, "unpair")) {
    link_ack("unpair", true);
    ble_clear_bonds();
  } else if (!strcmp(cmd, "status")) {
    send_status();
  } else {
    xfer_command(doc);   // unknown commands are ignored, never read as a heartbeat
  }
  return true;
}

bool reply_pending = false;   // Claude wrote something this turn: chime once when the turn ends
// The last AskUserQuestion (first question only), shown on its hint-less permission prompt.
char ask_text[200] = "", ask_choices[96] = "";

void remember_question(JsonObject input) {
  JsonObject q = input["questions"][0];
  strlcpy(ask_text, q["question"] | "", sizeof ask_text);
  size_t n = 0;
  ask_choices[0] = 0;
  for (JsonVariant o : q["options"].as<JsonArray>()) {   // "Red · Blue"
    if (n >= sizeof ask_choices) break;
    n += snprintf(ask_choices + n, sizeof ask_choices - n, "%s%s", n ? " · " : "", o["label"] | "");
  }
  char note[128];
  snprintf(note, sizeof note, "[ask] choices '%s'", ask_choices);
  rxlog_add(note);
}

void turn_event(JsonDocument &doc) {
  const bool mine = doc["role"] == "user";
  if (const char *typed = doc["content"]) {   // what you typed arrives as a plain string
    static char last[64] = "";                 // the app sometimes sends it twice
    bool shown = !strncmp(last, typed, sizeof last - 1);
    for (int i = chat_count() - 1; i >= 0 && i >= chat_count() - 3 && !shown; i--)   // typed on the board
      shown = chat_at(i).kind == ChatKind::MINE && !strncmp(chat_at(i).text, typed, sizeof chat_at(i).text - 1);
    if (!shown) chat_add(ChatKind::MINE, typed);
    strlcpy(last, typed, sizeof last);
    return;
  }
  for (JsonVariant block : doc["content"].as<JsonArray>()) {
    const char *type = block["type"] | "";
    if (!strcmp(type, "text")) {
      const char *text = block["text"] | "";
      chat_add(mine ? ChatKind::MINE : ChatKind::CLAUDE, text);
      reply_pending |= !mine && *text;
    } else if (!strcmp(type, "tool_use")) {
      const char *name = block["name"] | "";
      chat_add(ChatKind::TOOL, name);
      if (!strcmp(name, "AskUserQuestion")) remember_question(block["input"]);
    }
  }
}

void apply(const char *line) {
  JsonDocument doc;
  if (strstr(line, "\"rxlog\"") == nullptr) rxlog_add(line);
  if (deserializeJson(doc, line)) return;
  if (from_remote) {
    if (doc["cmd"].is<const char *>()) return;   // device commands only from the app
    last_remote_ms = millis();
  } else {
    last_live_ms = millis();
    if (command(doc)) return;
  }

  JsonArray t = doc["time"];
  if (!t.isNull() && t.size() == 2) {
    // Keep local time in the system clock (gmtime() then gives local fields, like the reference).
    timeval tv = {time_t(t[0].as<uint32_t>() + t[1].as<int32_t>()), 0};
    settimeofday(&tv, nullptr);
    rtc_valid = true;
    return;
  }
  if (doc["evt"] == "turn") {
    // Hooks report every Claude Code session: the chat shows only the one open on the device.
    if (!from_remote || companion_chat_accepts(doc["session"] | "", doc["project"] | "")) turn_event(doc);
    return;
  }

  st.total = doc["total"] | st.total;
  st.running = doc["running"] | st.running;
  st.waiting = doc["waiting"] | st.waiting;
  st.completed = doc["completed"] | false;
  if (doc["failed"] | false) buddy_one_shot(P_DIZZY, 2000);   // a tool failed (Claude Code hook)
  if (doc["tokens"].is<uint32_t>()) stats_on_bridge_tokens(doc["tokens"]);
  st.tokens_today = doc["tokens_today"] | st.tokens_today;
  st.tokens_session = doc["tokens_session"] | st.tokens_session;
  st.tokens_hour = doc["tokens_hour"] | st.tokens_hour;
  if (const char *m = doc["msg"]) copy(st.msg, m);
  JsonArray entries = doc["entries"];
  if (!entries.isNull()) {
    uint8_t n = 0;
    for (JsonVariant v : entries) {
      if (n >= 8) break;
      copy(st.lines[n++], v.as<const char *>());
    }
    if (n != st.n_lines || (n && strcmp(st.lines[n - 1], st.msg))) st.line_gen++;
    st.n_lines = n;
  }
  // A hook's status update leaves another hook's open request alone: hooks send "prompt" only to
  // show or clear one.
  JsonObject pr = doc["prompt"];
  if (!from_remote || !pr.isNull()) {
    copy(st.prompt_id, pr["id"]);
    copy(st.prompt_tool, pr["tool"]);
    copy(st.prompt_hint, pr["hint"]);
    if (!st.prompt_hint[0] && !strcmp(st.prompt_tool, "AskUserQuestion")) copy(st.prompt_hint, ask_text);
    prompt_client = from_remote && st.prompt_id[0] ? remote_client : 0;
  }

  // One chime per reply: when Claude has written something and nothing is running any more.
  if (reply_pending && doc["running"].is<int>() && st.running == 0) {
    reply_pending = false;
    sound_play(Sound::DONE);
  }
  st.last_updated = millis();
}

// Turn events run up to 4 KB (the desktop drops bigger ones): the buffers live in PSRAM.
template <size_t N>
struct LineBuf {
  char *buf = nullptr;
  size_t len = 0;
  bool overflow = false;   // a line longer than N is dropped whole, never parsed cut short
  void feed(int c) {
    if (!buf) buf = static_cast<char *>(heap_caps_malloc(N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (c == '\n' || c == '\r') {
      if (len && !overflow) {
        buf[len] = 0;
        if (buf[0] == '{') apply(buf);
      }
      len = 0;
      overflow = false;
    } else if (len < N - 1) {
      buf[len++] = char(c);
    } else {
      overflow = true;
    }
  }
};
LineBuf<4608> usb_line, bt_line;

}  // namespace

void link_ack(const char *what, bool ok, uint32_t n) {
  char b[80];
  snprintf(b, sizeof b, "{\"ack\":\"%s\",\"ok\":%s,\"n\":%lu}", what, ok ? "true" : "false", (unsigned long)n);
  link_send(b);
}

void link_begin() {}

void link_poll() {
  const uint32_t now = millis();
  while (Serial.available()) {
    last_usb_byte_ms = now;
    usb_line.feed(Serial.read());
  }
  for (int c; (c = ble_read()) >= 0;) {
    last_bt_byte_ms = now;
    bt_line.feed(c);
  }

  if (demo) {
    if (int32_t(now - demo_next) >= 0) {
      demo_idx = (demo_idx + 1) % 5;
      demo_next = now + 8000;
    }
    const Fake &f = FAKES[demo_idx];
    st.total = f.total;
    st.running = f.running;
    st.waiting = f.waiting;
    st.completed = f.completed;
    st.tokens_today = f.tokens;
    st.last_updated = now;
    st.connected = true;
    snprintf(st.msg, sizeof st.msg, "demo: %s", f.name);
    if (f.waiting) {   // a fake approval, so the whole flow can be tried without Claude
      if (!st.prompt_id[0]) {
        copy(st.prompt_id, "demo");
        copy(st.prompt_tool, "Bash");
        copy(st.prompt_hint, "npm test -- --watch=false");
      }
    } else {
      st.prompt_id[0] = 0;
    }
    return;
  }

  // millis() again, not `now`: a line applied above sets last_live_ms after `now`, and the
  // unsigned difference would wrap to "30 s gone" (a disconnect/connect chime on every message).
  st.connected = link_app_live() || (last_remote_ms && millis() - last_remote_ms <= REMOTE_LIVE_MS);
  if (!st.connected) {
    st.total = st.running = st.waiting = 0;
    st.completed = false;
    st.prompt_id[0] = 0;
    st.msg[0] = 0;
  }
}

const Tama &tama() { return st; }

void link_send(const char *json) {
  const size_t len = strlen(json);
  Serial.write(json, len);
  Serial.write('\n');
  ble_write(json, len);
  ble_write("\n", 1);
}

void link_send_permission(const char *id, bool approve, bool always) {
  if (demo || !id[0]) return;
  char b[96];
  const char *decision = !approve ? "deny" : always && prompt_client ? "always" : "once";
  snprintf(b, sizeof b, "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}", id, decision);
  if (prompt_client) companion_send_to(prompt_client, b);   // the hook that asked, over WiFi
  else link_send(b);
}

bool link_app_live() { return last_live_ms && millis() - last_live_ms <= 30000; }
bool link_hooks_active() { return !link_app_live() && last_remote_ms && millis() - last_remote_ms <= 5 * 60000; }
bool link_prompt_from_hook() { return prompt_client != 0 && st.prompt_id[0]; }

void link_apply_remote(uint32_t client, const char *json) {
  if (link_app_live() || demo) return;   // the Claude app's own link wins
  from_remote = true;
  remote_client = client;
  apply(json);
  from_remote = false;
}

void link_remote_gone(uint32_t client) {
  if (!client || client != prompt_client) return;
  prompt_client = 0;   // its hook stopped waiting (answered on the PC, or timed out)
  st.prompt_id[0] = 0;
  if (st.waiting) st.waiting--;
}

const char *link_question_choices() { return ask_choices; }
void link_debug_note(const char *note) { rxlog_add(note); }
bool link_rtc_valid() { return rtc_valid; }

void link_set_time(uint32_t epoch, int32_t tz_offset) {
  timeval tv = {time_t(epoch + tz_offset), 0};   // local time in the system clock, as above
  settimeofday(&tv, nullptr);
  rtc_valid = true;
}
bool link_bt_active() { return last_bt_byte_ms && millis() - last_bt_byte_ms <= 15000; }
bool link_usb_active() { return last_usb_byte_ms && millis() - last_usb_byte_ms <= 15000; }

void link_set_demo(bool on) {
  demo = on;
  demo_idx = 0;
  demo_next = millis() + 8000;
  if (!on) {
    st = Tama{};
    last_live_ms = last_remote_ms = 0;
  }
}

bool link_demo() { return demo; }
