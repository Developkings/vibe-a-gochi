#include "buddy.h"
#include <Arduino.h>
#include <time.h>
#include "link.h"
#include "pet.h"
#include "power.h"
#include "settings.h"
#include "sound.h"
#include "stats.h"
#include "voice.h"
#include "ui/screens.h"

namespace {

constexpr int PIN_LED = 42;   // WS2812
uint8_t base_state = P_IDLE, active_state = P_IDLE;
uint32_t one_shot_until = 0;
char last_prompt[40] = "";
bool answered = false, arrived = false;
uint32_t prompt_ms = 0;
bool was_connected = false;
bool was_completed = false;
uint32_t last_activity_ms = 0;              // Claude working or waiting for you
constexpr uint32_t QUIET_SLEEP_MS = 30 * 60 * 1000;   // then the pet dozes off
uint32_t led_color = 1;   // forces the first write

// "completed" is a moment (celebrated as a one-shot in buddy_tick), not a state.
uint8_t derive(const Tama &t, uint32_t now) {
  if (t.connected && t.waiting > 0) return P_ATTENTION;
  if (t.connected && t.running > 0) return P_BUSY;   // one session working is enough
  if (link_demo() && t.total == 0) return P_SLEEP;    // the demo's "asleep" step: no sessions at all
  if (now - last_activity_ms > QUIET_SLEEP_MS) return P_SLEEP;   // a long quiet
  return P_IDLE;
}

// Clock face: the pet keeps the reference's time-of-day habits.
uint8_t clock_mood(uint32_t now) {
  const time_t t = time(nullptr);
  tm lt;
  gmtime_r(&t, &lt);
  const int h = lt.tm_hour, dow = lt.tm_wday;
  const bool weekend = dow == 0 || dow == 6;
  if (h >= 1 && h < 7) return P_SLEEP;
  if (weekend) return now / 8000 % 6 == 0 ? P_HEART : P_SLEEP;
  if (h < 9) return now / 6000 % 4 == 0 ? P_IDLE : P_SLEEP;
  if (h == 12) return now / 5000 % 3 == 0 ? P_HEART : P_IDLE;
  if (dow == 5 && h >= 15) return now / 4000 % 3 == 0 ? P_CELEBRATE : P_IDLE;
  if (h >= 22 || h == 0) return now / 7000 % 3 == 0 ? P_DIZZY : P_SLEEP;
  return now / 10000 % 5 == 0 ? P_SLEEP : P_IDLE;
}

void led(uint32_t rgb) {
  if (rgb == led_color) return;
  led_color = rgb;
  rgbLedWrite(PIN_LED, rgb >> 16 & 0xFF, rgb >> 8 & 0xFF, rgb & 0xFF);
}

}  // namespace

void buddy_begin() { led(0); }

void buddy_tick() {
  const uint32_t now = millis();
  const Tama &t = tama();

  if (stats_poll_level_up()) buddy_one_shot(P_CELEBRATE, 3000);
  if (t.connected && (t.running > 0 || t.waiting > 0)) last_activity_ms = now;
  if (t.completed && !was_completed) {   // a reply just finished
    buddy_one_shot(P_CELEBRATE, 3000);
    last_activity_ms = now;
  }
  was_completed = t.completed;
  base_state = derive(t, now);
  if (base_state == P_IDLE && int32_t(now - power_wake_hold_until()) < 0) base_state = P_SLEEP;
  if (power_napping() || power_screen_off()) base_state = P_SLEEP;
  if (int32_t(now - one_shot_until) >= 0) active_state = base_state;

  // Link sounds.
  const bool connected = t.connected && !link_demo();
  if (connected != was_connected) {
    sound_play(connected ? Sound::CONNECTED : Sound::DISCONNECTED);
    was_connected = connected;
  }
  // No chime when a turn completes (every tool step completes one): the chime is for a new
  // message from Claude, played where the message arrives (link.cpp, companion.cpp).

  // A new permission request: chime, wake, and let the UI jump to the approval screen.
  if (strcmp(t.prompt_id, last_prompt)) {
    strlcpy(last_prompt, t.prompt_id, sizeof last_prompt);
    answered = false;
    if (t.prompt_id[0]) {
      prompt_ms = now;
      arrived = true;
      power_wake();
      const bool question = strstr(t.prompt_tool, "Question") != nullptr;
      sound_play(question ? Sound::QUESTION : Sound::ATTENTION);
    }
  }

  // LED: clay-orange blink while Claude waits for you (reference: 400 ms), otherwise dark.
  const bool blink = settings().led && (buddy_in_prompt() || active_state == P_ATTENTION);
  led(blink && (now / 400) % 2 ? 0x401000 : 0);
}

uint8_t buddy_state() {
  lv_obj_t *scr = lv_screen_active();
  if (scr == objects.clock) return clock_mood(millis());
  if (scr == objects.voice) {
    switch (voice_state()) {
      case VoiceState::SPEAKING: return P_TALK;
      case VoiceState::LISTENING: return P_LISTEN;
      case VoiceState::THINKING: return P_BUSY;
      default: break;
    }
  }
  if (scr == objects.approval && buddy_in_prompt()) return P_ATTENTION;
  return active_state;
}

void buddy_one_shot(uint8_t state, uint32_t ms) {
  active_state = state;
  one_shot_until = millis() + ms;
}

bool buddy_in_prompt() { return tama().prompt_id[0] && !answered; }

bool buddy_prompt_arrived() {
  const bool r = arrived;
  arrived = false;
  return r && buddy_in_prompt();
}

uint32_t buddy_prompt_age_s() { return (millis() - prompt_ms) / 1000; }

void buddy_answer(bool approve, bool always) {
  if (!buddy_in_prompt()) return;
  link_send_permission(tama().prompt_id, approve, always);
  answered = true;
  const uint32_t took = buddy_prompt_age_s();
  if (approve) {
    stats_on_approval(took);
    sound_play(Sound::APPROVE);
    if (took < 5) buddy_one_shot(P_HEART, 2000);
  } else {
    stats_on_denial();
    sound_play(Sound::DENY);
    buddy_one_shot(P_DIZZY, 2000);
  }
}
