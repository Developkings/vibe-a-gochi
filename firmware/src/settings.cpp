#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>

namespace {
Preferences prefs;
Settings s;
char pet[24] = "Buddy";
char owner[32] = "";
char character[32] = "";
char custom[48] = "";

// These names go into hand-written JSON (status ack) unescaped: drop the characters that break it.
void safe_copy(char *dst, size_t len, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j < len - 1; i++) {
    const char c = src[i];
    if (c != '"' && c != '\\' && (uint8_t)c >= 0x20) dst[j++] = c;
  }
  dst[j] = 0;
}

void put_string(const char *key, const char *val) {
  prefs.begin("buddy", false);
  prefs.putString(key, val);
  prefs.end();
}
}  // namespace

void settings_load() {
  prefs.begin("buddy", true);
  s.led = prefs.getBool("s_led", true);
  s.hud = prefs.getBool("s_hud", true);
  s.nap = prefs.getBool("s_nap", true);
  s.read_aloud = prefs.getBool("s_read", true);
  s.brightness = constrain(prefs.getUChar("s_bri", 70), 5, 100);
  s.speaker = constrain(prefs.getUChar("s_spk", 70), 0, 100);
  s.rotation = prefs.getUChar("s_rot", 0) & 3;
  s.recap = prefs.getBool("s_recap", true);
  prefs.getString("petname", pet, sizeof pet);
  prefs.getString("owner", owner, sizeof owner);
  prefs.getString("char", character, sizeof character);
  prefs.getString("cmd", custom, sizeof custom);
  prefs.end();
}

void settings_save() {
  prefs.begin("buddy", false);
  prefs.putBool("s_led", s.led);
  prefs.putBool("s_hud", s.hud);
  prefs.putBool("s_nap", s.nap);
  prefs.putBool("s_read", s.read_aloud);
  prefs.putUChar("s_bri", s.brightness);
  prefs.putUChar("s_spk", s.speaker);
  prefs.putUChar("s_rot", s.rotation);
  prefs.putBool("s_recap", s.recap);
  prefs.end();
}

Settings &settings() { return s; }

const char *pet_name() { return pet; }
void pet_name_set(const char *name) {
  safe_copy(pet, sizeof pet, name);
  put_string("petname", pet);
}

const char *owner_name() { return owner; }
void owner_set(const char *name) {
  safe_copy(owner, sizeof owner, name);
  put_string("owner", owner);
}

const char *character_key() { return character; }
void character_key_set(const char *key) {
  if (!strcmp(key, character)) return;
  safe_copy(character, sizeof character, key);
  put_string("char", character);
}

const char *custom_command() { return custom; }
void custom_command_set(const char *text) {
  safe_copy(custom, sizeof custom, text);
  put_string("cmd", custom);
}
