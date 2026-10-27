// Saved preferences (NVS namespace "buddy", same keys as the reference firmware where they overlap).
// Sound and volume live in sound.cpp, languages in i18n.cpp.
#pragma once
#include <stdint.h>

struct Settings {
  bool led = true;          // status LED blinks while Claude waits for you
  bool hud = true;          // last message bubble on the home screen
  bool nap = true;          // doze (dim, pet sleeps) after a minute without touches
  bool read_aloud = true;   // the companion reads Claude Code replies on the speaker
  uint8_t brightness = 70;  // 5..100 %
  uint8_t speaker = 70;     // companion voice volume 0..100 %
  uint8_t rotation = 0;     // 0 landscape, 1 landscape flipped, 2 portrait, 3 portrait flipped
  bool recap = true;        // daily summary: Claude Haiku writes a recap (else the numbers only)
};

void settings_load();
void settings_save();
Settings &settings();

const char *pet_name();
void pet_name_set(const char *name);   // strips " and \ like the reference (_safeCopy)
const char *owner_name();
void owner_set(const char *name);

// Selected character: "a:<species>" or "g:<pack>" ("" = default).
const char *character_key();
void character_key_set(const char *key);

// One user-defined quick command (Commands screen, long press).
const char *custom_command();
void custom_command_set(const char *text);
