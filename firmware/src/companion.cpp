#include "companion.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_app_desc.h>
#include <WiFi.h>
#include "adpcm.h"
#include "chat_log.h"
#include "i18n.h"
#include "i18n_strings.h"
#include "link.h"
#include "power.h"
#include "settings.h"
#include "sound.h"
#include "summary.h"
#include "ui_data.h"
#include "voice.h"
#include "wifi_link.h"

const uint16_t *display_capture(int &w, int &h);   // main.cpp: the screen as RGB565

namespace {

AsyncWebServer server(COMPANION_PORT);
AsyncWebSocket ws("/ws");
Preferences prefs;
char token[33] = "";
char peer[40] = "";
volatile uint32_t client_id = 0;   // the authorized companion (voice, projects), 0 = none
// Helper clients with the same token: the board MCP server and the Claude Code hooks (a short
// connection per event). They ask questions, post notes and Claude's status, and don't push the
// companion out.
constexpr int MAX_HELPERS = 8;
uint32_t helpers[MAX_HELPERS] = {};
uint32_t pair_code = 0, pair_client = 0;
bool pair_helper = false;
uint32_t ask_client = 0;   // who asked the open question
char ask_id[40] = "";
// Typed on the board while no companion runs: kept for the Claude Code hooks, which hand it to
// Claude at its next step or when it finishes (board_hook.py).
char typed_inbox[600] = "";
// Stop tapped here: the companion ends its own run, and the hooks stop Claude Code after its
// current tool (they read it in hello_ok and answer stop_done).
constexpr uint32_t STOP_WAIT_MS = 120000;
uint32_t stop_ms = 0;   // 0 = none
char stop_for[48] = "";   // the session the chat shows ("" = whichever works)
SemaphoreHandle_t lock;
QueueHandle_t inbox;   // char* JSON messages (heap), from the network task to the main loop

constexpr int MAX_PROJECTS = 16, MAX_CHATS = 16;
Project projects[MAX_PROJECTS];
ChatInfo chats[MAX_CHATS];
int n_projects = 0, n_chats = 0, cur_project = -1;
bool chats_deletable = false;
int32_t usage_left = -1;   // "limits" from the companion, counted down from usage_ms
uint32_t usage_ms = 0;
char open_chat_id[48] = "";   // the chat open on the device ("" = a new one): sent with every message
bool chat_pinned = false;     // a chat was opened or started here: the chat shows only that one
char open_project[64] = "";   // its project, kept (with the id) across restarts and updates
char shown_session[48] = "";  // otherwise: the session the chat shows (another one starts a new page)

// The chat open here survives a restart (a WiFi update restarts the board).
void save_open_chat(const char *title) {
  prefs.putString("chat", open_chat_id);
  prefs.putString("chat_proj", open_project);
  prefs.putString("chat_title", title ? title : "");
}

void load_open_chat() {
  prefs.getString("chat", open_chat_id, sizeof open_chat_id);
  prefs.getString("chat_proj", open_project, sizeof open_project);
  chat_pinned = open_project[0] != 0;
  if (!chat_pinned) return;
  char title[80] = "";
  prefs.getString("chat_title", title, sizeof title);
  ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_PROJECT, open_project);
  ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_CHAT, title);
}

uint16_t lists_gen = 0;

// Text messages can arrive in several WebSocket frames: collect them per message.
char *partial = nullptr;
size_t partial_len = 0;

struct Message {
  uint32_t client;
  char *json;
};

void post(uint32_t client, char *json) {
  Message m{client, json};
  if (xQueueSend(inbox, &m, 0) != pdTRUE) free(json);
}

void on_audio(const uint8_t *data, size_t len) {
  if (len < 5 || data[0] != 0x02) return;
  constexpr size_t CAP = 2048;   // samples; network task only, in PSRAM (off its stack and internal RAM)
  static int16_t *const pcm =
      static_cast<int16_t *>(heap_caps_malloc(CAP * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const size_t n = adpcm_decode_frame(data, min<size_t>(len, 4 + CAP / 2), pcm);
  sound_stream_write(pcm, n);
}

void on_event(AsyncWebSocket *, AsyncWebSocketClient *c, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DISCONNECT) {
    if (c->id() == client_id) {
      client_id = 0;
      post(c->id(), strdup("{\"evt\":\"_gone\"}"));
    } else {
      post(c->id(), strdup("{\"evt\":\"_helper_gone\"}"));   // the main loop drops it (and its question)
    }
    return;
  }
  if (type != WS_EVT_DATA) return;
  const AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
  if (info->opcode == WS_BINARY || (info->opcode == WS_CONTINUATION && info->message_opcode == WS_BINARY)) {
    if (c->id() == client_id && info->final && info->index == 0 && info->len == len) on_audio(data, len);
    return;
  }
  if (info->index == 0) {
    free(partial);
    partial_len = info->len;
    partial = static_cast<char *>(info->len < 65536 ? malloc(info->len + 1) : nullptr);
  }
  if (!partial || info->index + len > partial_len) return;
  memcpy(partial + info->index, data, len);
  if (info->final && info->index + len == partial_len) {
    partial[partial_len] = 0;
    post(c->id(), partial);
    partial = nullptr;
  }
}

bool send_to(uint32_t id, const char *json) {
  if (!id) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  AsyncWebSocketClient *c = ws.client(id);
  const bool ok = c && !c->queueIsFull() && c->text(json);
  xSemaphoreGive(lock);
  return ok;
}

void send_doc(uint32_t id, JsonDocument &doc) {
  char buf[1024];
  serializeJson(doc, buf, sizeof buf);
  send_to(id, buf);
}

void authorized(uint32_t id, const char *name) {
  if (client_id && client_id != id) ws.close(client_id);   // the newest paired companion wins
  client_id = id;
  strlcpy(peer, name ? name : "PC", sizeof peer);
  pair_code = 0;
  JsonDocument d;
  d["evt"] = "hello_ok";
  char fw[17];
  esp_app_get_elf_sha256(fw, sizeof fw);   // the build running here: the companion offers newer ones
  d["fw"] = fw;
  d["device"] = WiFi.getHostname();
  d["pet"] = pet_name();
  send_doc(id, d);
  companion_sync_settings();
  companion_command("list_projects");
  Serial.printf("[companion] %s connected\n", peer);
}

bool is_helper(uint32_t id) {
  for (uint32_t h : helpers)
    if (id && h == id) return true;
  return false;
}

void add_helper(uint32_t id) {
  int slot = 0;
  for (int i = 0; i < MAX_HELPERS; i++) {
    if (!helpers[i] || !ws.client(helpers[i])) {   // free, or its socket is gone
      slot = i;
      break;
    }
    if (helpers[i] != ask_client) slot = i;   // all busy: reuse one that isn't waiting for an answer
  }
  if (helpers[slot] && helpers[slot] != id) ws.close(helpers[slot]);
  helpers[slot] = id;
  JsonDocument d;
  d["evt"] = "hello_ok";
  d["device"] = WiFi.getHostname();
  d["pet"] = pet_name();
  d["role"] = "helper";
  d["app"] = link_app_live();   // the Claude app is connected itself: hooks stay quiet
  d["inbox"] = typed_inbox[0] != 0;   // something typed here waits for Claude
  if (stop_ms && millis() - stop_ms < STOP_WAIT_MS) {
    d["stop"] = true;
    d["stop_session"] = stop_for;
  }
  d["rssi"] = WiFi.RSSI();             // WiFi diagnostics: signal (dBm), access point, channel
  d["ap"] = WiFi.BSSIDstr();
  d["channel"] = WiFi.channel();
  d["uptime"] = millis() / 1000;       // resets after a crash
  d["drops"] = wifi_drops();
  d["drop_reason"] = wifi_last_drop_reason();
  d["reset_reason"] = int(esp_reset_reason());
  d["rotation"] = settings().rotation;   // screenshots.py turns the display and back
  d["owner"] = owner_name();             // … and puts it back after a sample name
  send_doc(id, d);
  Serial.printf("[companion] helper %lu connected\n", (unsigned long)id);
}

void drop_helper(uint32_t id) {
  for (uint32_t &h : helpers)
    if (h == id) h = 0;
  link_remote_gone(id);
  if (ask_client == id) {   // its question has nobody to answer to
    ask_client = 0;
    ui_ask_close();
  }
}

// {"evt":"screenshot","w":320,"h":240} then the RGB565 pixels (little-endian) in binary frames.
void send_screenshot(uint32_t to) {
  int w, h;
  const uint16_t *px = display_capture(w, h);
  JsonDocument d;
  d["evt"] = "screenshot";
  d["w"] = px ? w : 0;
  d["h"] = px ? h : 0;
  send_doc(to, d);
  if (!px) return;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(px);
  constexpr size_t CHUNK = 8192;
  for (size_t off = 0, total = size_t(w) * h * 2; off < total; off += CHUNK) {
    for (int tries = 0;; tries++) {   // the socket's queue drains while we wait
      xSemaphoreTake(lock, portMAX_DELAY);
      AsyncWebSocketClient *c = ws.client(to);
      const bool ok = c && !c->queueIsFull() && c->binary(bytes + off, min(CHUNK, total - off));
      const bool gone = !c;
      xSemaphoreGive(lock);
      if (ok || gone || tries > 400) break;
      delay(5);
    }
  }
}

// The open question (to show it again after the keyboard for a typed answer closes empty) and
// the index of its "type an answer" button (-1 = none).
String ask_json;
int ask_other = -1;
char ask_other_label[64] = "";

void ask_show(uint32_t from, JsonDocument &doc, bool chime);

void ask_typed(const char *text) {
  JsonDocument d;
  d["evt"] = "answer";
  d["id"] = ask_id;
  d["choice"] = ask_other;
  d["text"] = text;
  send_doc(ask_client, d);
  ask_client = 0;
  sound_play(Sound::APPROVE);
}

void ask_retype() {
  JsonDocument doc;
  if (ask_client && !deserializeJson(doc, ask_json)) ask_show(ask_client, doc, false);
}

// The tapped choice goes back to whoever asked; -1 = closed on the board. Multi-select: "choices".
void ask_answered(int choice) {
  if (choice >= 0 && choice == ask_other) return ui_keyboard(ask_other_label, "", false, ask_typed, ask_retype);
  JsonDocument d;
  d["evt"] = "answer";
  d["id"] = ask_id;
  if (choice >= 0 && (choice & ASK_MULTI)) {
    JsonArray picked = d["choices"].to<JsonArray>();
    for (int i = 0; i < 4; i++)
      if (choice & (1 << i)) picked.add(i);
    d["choice"] = picked[0];
  } else {
    d["choice"] = choice;
  }
  send_doc(ask_client, d);
  ask_client = 0;
  sound_play(choice >= 0 ? Sound::APPROVE : Sound::DENY);
}

// {"cmd":"ask","id":"…","question":"…","choices":["Red","Blue"]}: the question screen with buttons.
// Optional: "from" (the header), "other" (true or a label: a button for a typed answer, "text" in
// the answer), "multi" (choices toggle, a Done button sends "choices"), "kind":"plan" (Claude's
// plan: Approve [/ auto-accept edits when "auto"] / Keep planning / Change something).
void ask_show(uint32_t from, JsonDocument &doc, bool chime) {
  ask_client = 0;   // ui_ask closes the open one first; its callback must not answer
  ui_ask_close();
  const char *choices[4];
  int n = 0;
  char who[72] = "";
  const char *other = nullptr;
  if (doc["kind"] == "plan") {
    choices[n++] = i18n_str(TrString::PLAN_APPROVE);
    if (doc["auto"] | false) choices[n++] = i18n_str(TrString::PLAN_AUTO);
    choices[n++] = i18n_str(TrString::PLAN_KEEP);
    other = i18n_str(TrString::PLAN_CHANGE);
    snprintf(who, sizeof who, "%s · %s", i18n_str(TrString::PLAN_TITLE), doc["from"] | "");
  } else {
    for (JsonVariant c : doc["choices"].as<JsonArray>())
      if (n < 4) choices[n++] = c | "";
    strlcpy(who, doc["from"] | "", sizeof who);
    if (doc["other"].is<const char *>()) other = doc["other"];
    else if (doc["other"] | false) other = i18n_str(TrString::TYPE_ANSWER);
  }
  ask_other = other ? n : -1;
  strlcpy(ask_other_label, other ? other : "", sizeof ask_other_label);
  ask_client = from;
  strlcpy(ask_id, doc["id"] | "", sizeof ask_id);
  power_wake();
  if (chime) sound_play(Sound::QUESTION);
  ui_ask(doc["question"] | "", choices, n, ask_answered, who[0] ? who : nullptr, other,
         (doc["multi"] | false) ? i18n_str(TrString::ASK_DONE) : nullptr);
}

void ask(uint32_t from, JsonDocument &doc) {
  if (ask_client && ask_client != from) {   // someone else's question is replaced: tell them
    JsonDocument d;
    d["evt"] = "answer";
    d["id"] = ask_id;
    d["choice"] = -1;
    d["replaced"] = true;
    send_doc(ask_client, d);
  }
  ask_json = "";
  serializeJson(doc, ask_json);
  ask_show(from, doc, true);
}

void handle(uint32_t from, JsonDocument &doc) {
  const char *cmd = doc["cmd"] | "";
  if (!strcmp(cmd, "hello")) {
    const char *t = doc["token"] | "";
    const bool helper = doc["role"] == "helper";
    if (token[0] && !strcmp(t, token)) {
      if (helper) add_helper(from);
      else authorized(from, doc["name"]);
      return;
    }
    pair_code = 100000 + esp_random() % 900000;   // shown on the pairing screen
    pair_client = from;
    pair_helper = helper;
    send_to(from, "{\"evt\":\"pair_required\"}");
    return;
  }
  if (!strcmp(doc["evt"] | "", "_helper_gone")) return drop_helper(from);
  if (!strcmp(cmd, "pair")) {
    const uint32_t code = doc["code"].is<const char *>() ? strtoul(doc["code"], nullptr, 10) : doc["code"].as<uint32_t>();
    if (from != pair_client || !pair_code || code != pair_code) {
      send_to(from, "{\"evt\":\"pair_failed\"}");
      return;
    }
    for (int i = 0; i < 16; i++) snprintf(token + i * 2, 3, "%02x", uint8_t(esp_random()));
    prefs.putString("token", token);
    JsonDocument d;
    d["evt"] = "paired";
    d["token"] = token;
    send_doc(from, d);
    pair_code = 0;
    if (pair_helper) return add_helper(from);
    return authorized(from, doc["name"]);
  }

  // Questions and notes: from the companion or a helper.
  if (from == client_id || is_helper(from)) {
    if (!strcmp(cmd, "claude")) {   // {"cmd":"claude","data":{â€¦}}: a heartbeat / turn line from a hook
      String line;
      serializeJson(doc["data"], line);
      return link_apply_remote(from, line.c_str());
    }
    if (!strcmp(cmd, "take_inbox")) {   // a hook delivers what was typed here
      JsonDocument d;
      d["evt"] = "inbox";
      d["text"] = typed_inbox;
      send_doc(from, d);
      typed_inbox[0] = 0;
      return;
    }
    if (!strcmp(cmd, "goto")) {   // {"cmd":"goto","screen":5}: ScreensEnum
      const int s = doc["screen"] | 0;
      if (s >= _SCREEN_ID_FIRST && s <= _SCREEN_ID_LAST) ui_go(ScreensEnum(s), LV_SCR_LOAD_ANIM_NONE);
      return;
    }
    if (!strcmp(cmd, "rotate")) return ui_orientation_set(uint8_t(doc["rotation"] | 0) & 3);
    if (!strcmp(cmd, "owner")) return owner_set(doc["name"] | "");   // {"cmd":"owner","name":"Alex"}: guide screenshots
    if (!strcmp(cmd, "screenshot")) return send_screenshot(from);
    if (!strcmp(cmd, "stop_done")) {
      stop_ms = 0;
      return;
    }
    if (!strcmp(cmd, "ask")) return ask(from, doc);
    if (!strcmp(cmd, "ask_cancel")) {
      if (from == ask_client && !strcmp(doc["id"] | "", ask_id)) {
        ask_client = 0;
        ui_ask_close();
      }
      return;
    }
    if (!strcmp(cmd, "notify")) {   // {"cmd":"notify","text":"â€¦","sound":true}
      power_wake();
      ui_toast(doc["text"] | "");
      if (doc["sound"] | true) sound_play(Sound::DONE);
      return;
    }
  }
  // Everything else only from the paired companion, or, marked "demo", from a helper (the guide's
  // screenshots show sample projects, chats and days; "demo_end" brings the real ones back).
  if (is_helper(from) && !strcmp(cmd, "demo_end")) {
    cur_project = -1;   // its index was into the sample list
    n_chats = 0;
    lists_gen++;
    companion_command("list_projects");
    if (open_chat_id[0]) {
      JsonDocument d;
      d["cmd"] = "open_chat";
      d["project"] = open_project;
      d["session"] = open_chat_id;
      send_doc(client_id, d);
    }
    summary_request(summary_day(), false);
    return;
  }
  if (from != client_id && !(is_helper(from) && (doc["demo"] | false))) return;

  const char *evt = doc["evt"] | "";
  if (!strcmp(evt, "_gone")) {
    voice_on_remote("idle");
    Serial.println("[companion] disconnected");
  } else if (!strcmp(evt, "voice")) {
    voice_on_remote(doc["state"] | "idle");
  } else if (!strcmp(evt, "transcript")) {
    ui_set_text(FLOW_GLOBAL_VARIABLE_VOICE_TRANSCRIPT, doc["text"] | "");
    if (doc["final"] | false) chat_add(ChatKind::MINE, doc["text"]);
  } else if (!strcmp(evt, "projects")) {
    n_projects = 0;
    for (JsonVariant p : doc["items"].as<JsonArray>()) {
      if (n_projects >= MAX_PROJECTS) break;
      Project &d = projects[n_projects++];
      strlcpy(d.name, p["name"] | "", sizeof d.name);
      strlcpy(d.path, p["path"] | "", sizeof d.path);
      d.chats = p["chats"] | 0;
      d.assistant = p["assistant"] | false;
    }
    lists_gen++;
  } else if (!strcmp(evt, "limits")) {
    usage_left = doc["left"] | -1;
    usage_ms = millis();
  } else if (!strcmp(evt, "chats")) {
    n_chats = 0;
    chats_deletable = doc["deletable"] | false;
    for (JsonVariant p : doc["items"].as<JsonArray>()) {
      if (n_chats >= MAX_CHATS) break;
      ChatInfo &d = chats[n_chats++];
      strlcpy(d.id, p["id"] | "", sizeof d.id);
      strlcpy(d.title, p["title"] | "", sizeof d.title);
      strlcpy(d.when, p["when"] | "", sizeof d.when);
      d.running = p["running"] | false;
      d.old = p["old"] | false;
    }
    lists_gen++;
  } else if (!strcmp(evt, "chat")) {
    if (const char *p = doc["project"]) ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_PROJECT, p);
    if (const char *t = doc["title"]) ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_CHAT, t);
    chat_clear();
    for (JsonVariant m : doc["messages"].as<JsonArray>()) {
      const char *role = m["role"] | "assistant";
      chat_add(!strcmp(role, "user") ? ChatKind::MINE : !strcmp(role, "tool") ? ChatKind::TOOL : ChatKind::CLAUDE,
               m["text"]);
    }
  } else if (!strcmp(evt, "chat_id")) {   // the new chat started from here now exists
    strlcpy(open_chat_id, doc["session"] | "", sizeof open_chat_id);
    if (const char *t = doc["title"]) ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_CHAT, t);
    save_open_chat(doc["title"] | "");
  } else if (!strcmp(evt, "reply")) {
    chat_add(ChatKind::CLAUDE, doc["text"]);
    ui_set_text(FLOW_GLOBAL_VARIABLE_LAST_MESSAGE, doc["text"] | "");
    if ((doc["text"] | "")[0]) sound_play(Sound::DONE);   // new message from Claude
  } else if (!strcmp(evt, "tool")) {
    chat_add(ChatKind::TOOL, doc["name"]);
  } else if (!strcmp(evt, "error")) {
    ui_toast(doc["text"] | "?");
  } else if (!strncmp(evt, "summary", 7)) {   // summary, summary_recap
    summary_on_event(doc);
  } else if (!strcmp(cmd, "time")) {          // {"cmd":"time","t":<unix>,"tz":<offset s>}: the clock over WiFi
    link_set_time(doc["t"] | 0u, doc["tz"] | 0);
  }
}

// Firmware update over WiFi: POST /update with the pairing token (X-Token) and the image's MD5
// (X-MD5) as the raw body (companion/flash_wifi.py). The web server task writes it; the UI shows
// progress from companion_tick and restarts once the image checks out.
enum class Ota : uint8_t { idle, running, done, failed };
volatile Ota ota = Ota::idle;
volatile uint32_t ota_last_ms = 0;
volatile uint32_t ota_written = 0, ota_total = 0;   // for the progress bar
char ota_error[48] = "";   // why the last upload failed (Update.abort() would overwrite it)

bool ota_allowed(AsyncWebServerRequest *r) {
  return token[0] && r->hasHeader("X-Token") && r->header("X-Token") == token;
}

void ota_fail() {
  strlcpy(ota_error, Update.errorString(), sizeof ota_error);
  Update.abort();
  ota = Ota::failed;
}

// Runs on the web server task only (Update is not shared with the UI task).
void ota_body(AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
  if (!ota_allowed(r)) return;
  ota_last_ms = millis();   // before `running`: the UI's stall check must never see an old time
  if (index == 0) {
    if (Update.isRunning()) Update.abort();   // an earlier upload the PC gave up on
    ota_error[0] = 0;
    ota_written = 0;
    ota_total = total;
    ota = Ota::running;
    if (!Update.begin(total) || (r->hasHeader("X-MD5") && !Update.setMD5(r->header("X-MD5").c_str())))
      return ota_fail();
  }
  if (ota != Ota::running) return;
  if (Update.write(data, len) != len) return ota_fail();
  ota_written = index + len;
  if (index + len == total) {
    if (Update.end()) ota = Ota::done;
    else ota_fail();
  }
}

void ota_done(AsyncWebServerRequest *r) {
  if (!ota_allowed(r)) return r->send(401, "text/plain", "pair first");
  if (ota == Ota::done) return r->send(200, "text/plain", "ok");
  r->send(500, "text/plain", ota_error[0] ? ota_error : "upload incomplete");
}

void ota_tick() {
  static Ota shown = Ota::idle;
  static uint32_t done_ms = 0;
  Ota now = ota;
  if (now == Ota::running && millis() - ota_last_ms > 15000) {   // the PC went away mid-upload
    strlcpy(ota_error, "timed out", sizeof ota_error);   // the next upload discards the partial one
    ota = Ota::failed;
    now = Ota::failed;
  }
  static int shown_pct = -1;
  if (now == Ota::running) {   // received bytes go straight to flash: received = written
    const int pct = ota_total ? int(uint64_t(ota_written) * 100 / ota_total) : 0;
    if (pct != shown_pct || shown != Ota::running) {   // redraw only on a change
      shown_pct = pct;
      power_wake();
      ui_progress(i18n_str(TrString::UPDATE_TITLE), pct, i18n_str(TrString::UPDATING), false);
    }
  }
  if (now == shown) {
    if (now == Ota::done && millis() - done_ms > 1500) ESP.restart();   // the reply has left
    return;
  }
  shown = now;
  if (now == Ota::done) {
    ui_progress(i18n_str(TrString::UPDATE_TITLE), 100, i18n_str(TrString::UPDATE_DONE), true);
    done_ms = millis();
  }
  if (now == Ota::failed) {
    ui_progress_close();
    ui_toast(i18n_str(TrString::UPDATE_FAILED));
    ota = Ota::idle;
    shown = Ota::idle;
  }
}

}  // namespace

void companion_begin() {
  lock = xSemaphoreCreateMutex();
  inbox = xQueueCreate(16, sizeof(Message));
  prefs.begin("companion", false);
  prefs.getString("token", token, sizeof token);
  load_open_chat();
  ws.onEvent(on_event);
  server.addHandler(&ws);
  server.on("/update", HTTP_POST, ota_done, nullptr, ota_body);
  server.begin();
  Serial.printf("[companion] listening on port %u\n", COMPANION_PORT);
}

void companion_tick() {
  if (!inbox) return;
  ota_tick();
  Message m;
  while (xQueueReceive(inbox, &m, 0) == pdTRUE) {
    JsonDocument doc;
    if (!deserializeJson(doc, m.json)) handle(m.client, doc);
    free(m.json);
  }
  static uint32_t last_cleanup = 0;
  if (millis() - last_cleanup > 2000) {
    last_cleanup = millis();
    ws.cleanupClients(2);
  }
}

bool companion_chat_pinned() { return chat_pinned; }

bool companion_chat_accepts(const char *session, const char *project) {
  if (!*session) return !chat_pinned;
  if (chat_pinned) return !strcmp(session, open_chat_id);
  if (strcmp(session, shown_session)) {   // another session: its own page, named after its project
    strlcpy(shown_session, session, sizeof shown_session);
    chat_clear();
    ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_PROJECT, project);
    ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_CHAT, project);
  }
  return true;
}

bool companion_connected() { return client_id != 0; }
const char *companion_peer() { return peer; }
uint32_t companion_pair_code() { return pair_code; }

void companion_forget() {
  token[0] = 0;
  if (inbox) prefs.remove("token");
  if (client_id) ws.close(client_id);
  client_id = 0;
}

bool companion_send(const char *json) { return send_to(client_id, json); }
bool companion_send_to(uint32_t client, const char *json) { return send_to(client, json); }

bool companion_queue_for_hooks(const char *text) {
  if (!link_hooks_active() || !text[0]) return false;
  const size_t n = strlen(typed_inbox);
  snprintf(typed_inbox + n, sizeof typed_inbox - n, "%s%s", n ? "\n" : "", text);
  return true;
}

void companion_send_audio(const uint8_t *frame, size_t len) {
  const uint32_t id = client_id;
  if (!id) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  AsyncWebSocketClient *c = ws.client(id);
  if (c && !c->queueIsFull()) c->binary(frame, len);
  xSemaphoreGive(lock);
}

bool companion_send_text(const char *text) {
  JsonDocument d;
  d["cmd"] = "send_text";
  d["text"] = text;
  if (cur_project >= 0) d["project"] = projects[cur_project].name;
  else if (open_project[0]) d["project"] = open_project;   // after a restart, before Projects is opened
  if (open_chat_id[0]) d["session"] = open_chat_id;   // a restarted companion still knows the chat
  char buf[1024];
  serializeJson(d, buf, sizeof buf);
  return companion_send(buf);
}

bool companion_stop() {
  const bool hooks = link_hooks_active();
  if (hooks) {
    stop_ms = millis() | 1;
    strlcpy(stop_for, open_chat_id[0] ? open_chat_id : shown_session, sizeof stop_for);
  }
  return companion_command("stop") || hooks;
}

bool companion_stop_pending() { return stop_ms && millis() - stop_ms < STOP_WAIT_MS; }

bool companion_command(const char *cmd) {   // with the chat open here (voice goes to it)
  JsonDocument d;
  d["cmd"] = cmd;
  if (cur_project >= 0) d["project"] = projects[cur_project].name;
  else if (open_project[0]) d["project"] = open_project;
  if (open_chat_id[0]) d["session"] = open_chat_id;
  char buf[192];
  serializeJson(d, buf, sizeof buf);
  return companion_send(buf);
}

void companion_open_project(int i) {
  if (i < 0 || i >= n_projects) return;
  cur_project = i;
  n_chats = 0;
  lists_gen++;
  ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_PROJECT, projects[i].name);
  JsonDocument d;
  d["cmd"] = "list_chats";
  d["project"] = projects[i].name;
  send_doc(client_id, d);
}

void companion_open_chat(int i) {
  if (i < 0 || i >= n_chats || cur_project < 0) return;
  ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_CHAT, chats[i].title);
  chat_clear();
  JsonDocument d;
  d["cmd"] = "open_chat";
  d["project"] = projects[cur_project].name;
  d["session"] = chats[i].id;
  strlcpy(open_chat_id, chats[i].id, sizeof open_chat_id);
  strlcpy(open_project, projects[cur_project].name, sizeof open_project);
  chat_pinned = true;
  save_open_chat(chats[i].title);
  send_doc(client_id, d);
}

void companion_new_chat() {
  if (cur_project < 0) return;
  open_chat_id[0] = 0;   // the companion sends the id once the chat exists ("chat_id")
  strlcpy(open_project, projects[cur_project].name, sizeof open_project);
  chat_pinned = true;
  save_open_chat("");
  chat_clear();
  ui_set_text(FLOW_GLOBAL_VARIABLE_CURRENT_CHAT, "");
  JsonDocument d;
  d["cmd"] = "new_chat";
  d["project"] = projects[cur_project].name;
  send_doc(client_id, d);
}

void companion_sync_settings() {
  JsonDocument d;
  d["cmd"] = "set_language";
  d["ui"] = i18n_ui_code();
  d["speech"] = i18n_speech_code();
  send_doc(client_id, d);
  JsonDocument r;
  r["cmd"] = "read_aloud";
  r["on"] = settings().read_aloud;
  send_doc(client_id, r);
}

int companion_project_count() { return n_projects; }
const Project &companion_project(int i) { return projects[i]; }
int companion_chat_count() { return n_chats; }
const ChatInfo &companion_chat(int i) { return chats[i]; }
uint16_t companion_lists_gen() { return lists_gen; }
bool companion_chats_deletable() { return chats_deletable; }

void companion_delete_chat(int i) {
  JsonDocument d;
  d["cmd"] = "delete_chats";
  if (i < 0) d["old"] = true;
  else if (i < n_chats) d["sessions"].add(chats[i].id);
  send_doc(client_id, d);
}

int32_t companion_usage_left() {
  if (usage_left < 0 || !client_id) return -1;
  const int32_t gone = (millis() - usage_ms) / 1000;
  return usage_left > gone ? usage_left - gone : 0;
}
