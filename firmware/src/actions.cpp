// Native actions wired to the UI's controls (declared by EEZ in ui/actions.h, named in gen_ui.py).
#include <Arduino.h>
#include "buddy.h"
#include "chat_log.h"
#include "companion.h"
#include "i18n.h"
#include "i18n_strings.h"
#include "link.h"
#include "power.h"
#include "settings.h"
#include "sound.h"
#include "summary.h"
#include "ui/actions.h"
#include "ui_data.h"
#include "voice.h"
#include "wifi_link.h"

void sd_import();

namespace {

lv_obj_t *target(lv_event_t *e) { return static_cast<lv_obj_t *>(lv_event_get_target(e)); }
int index_of(lv_event_t *e) { return int(intptr_t(lv_event_get_user_data(e))); }
bool checked(lv_event_t *e) { return lv_obj_has_state(target(e), LV_STATE_CHECKED); }

// Slider: live while dragging, saved once on release.
bool released(lv_event_t *e) { return lv_event_get_code(e) == LV_EVENT_RELEASED; }

// Something for Claude: through the companion (Claude Code chat) — the BLE link has no text channel.
// Without the companion, Claude Code's WiFi hooks pick it up at Claude's next step.
void send_to_claude(const char *text) {
  if (companion_send_text(text)) {
    chat_add(ChatKind::MINE, text);
    ui_toast(i18n_str(TrString::SENT));
  } else if (companion_queue_for_hooks(text)) {
    chat_add(ChatKind::MINE, text);
    ui_toast(i18n_str(TrString::QUEUED));
  } else {
    ui_toast(i18n_str(TrString::COMPANION_OFF));
  }
}

// The first label inside a button (quick reply chips: their text is already translated).
const char *label_of(lv_obj_t *btn) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(btn); i++) {
    lv_obj_t *c = lv_obj_get_child(btn, i);
    if (lv_obj_check_type(c, &lv_label_class)) return lv_label_get_text(c);
  }
  return "";
}

void custom_saved(const char *text) { custom_command_set(text); }
void owner_saved(const char *text) { owner_set(text); }
void pet_name_saved(const char *text) { pet_name_set(text); }

}  // namespace

extern "C" {

void action_permission_approve(lv_event_t *) { buddy_answer(true); }
void action_permission_deny(lv_event_t *) { buddy_answer(false); }
void action_permission_always(lv_event_t *) { buddy_answer(true, true); }   // allow, and don't ask again

void action_send_quick_reply(lv_event_t *e) {
  if (index_of(e) == 0) send_to_claude(i18n_str(TrString::CMD_CONTINUE));
  else if (index_of(e) == 3) send_to_claude(i18n_str(TrString::CMD_EXPLAIN));
  else send_to_claude(label_of(target(e)));
}

void action_type_message(lv_event_t *) { ui_keyboard(i18n_str(TrString::MESSAGE_TO_CLAUDE), "", false, send_to_claude); }

// Voice screen: opening it starts the conversation, leaving it ends it.
void action_voice_open(lv_event_t *) { voice_start(); }
void action_voice_close(lv_event_t *) { voice_stop(); }
void action_voice_stop(lv_event_t *) {
  voice_stop();
  sound_stream_stop();
  ui_back(LV_SCR_LOAD_ANIM_OUT_BOTTOM);
}
void action_voice_mute(lv_event_t *) { voice_set_muted(!voice_muted()); }
void action_push_to_talk_down(lv_event_t *) { voice_start(); }
void action_push_to_talk_up(lv_event_t *) { voice_stop(); }

// Commands screen, index = gen_ui.COMMANDS.
void action_stop_claude(lv_event_t *) {
  if (!companion_stop()) return ui_toast(i18n_str(TrString::COMPANION_OFF));
  sound_play(Sound::DENY);
  ui_toast(i18n_str(TrString::STOPPING));
}

void action_send_command(lv_event_t *e) {
  switch (index_of(e)) {
    case 0: return send_to_claude(i18n_str(TrString::CMD_CONTINUE));
    case 1: return send_to_claude(i18n_str(TrString::CMD_TESTS));
    case 2: return send_to_claude(i18n_str(TrString::CMD_COMMIT));
    case 3: return send_to_claude("/compact");
    case 4: return send_to_claude(i18n_str(TrString::CMD_EXPLAIN));
    case 5: return action_stop_claude(e);
    case 6: return action_type_message(e);
    case 7:
      if (custom_command()[0]) send_to_claude(custom_command());
      else ui_keyboard(i18n_str(TrString::NEW_COMMAND), "", false, custom_saved);
      return;
  }
}

void action_edit_custom_command(lv_event_t *) {
  ui_keyboard(i18n_str(TrString::NEW_COMMAND), custom_command(), false, custom_saved);
}

void action_new_chat(lv_event_t *) {
  if (!companion_connected()) return ui_toast(i18n_str(TrString::COMPANION_OFF));
  companion_new_chat();
  ui_go(SCREEN_ID_CHAT, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

void action_set_brightness(lv_event_t *e) {
  settings().brightness = constrain(lv_slider_get_value(target(e)), 5, 100);
  power_apply_brightness();
  if (released(e)) settings_save();
}

void action_set_volume(lv_event_t *e) {
  lv_obj_t *s = target(e);
  sound_set_volume(lv_slider_get_value(s), released(e));
  static uint32_t last = 0;
  if (millis() - last > 350) {   // throttled preview while dragging
    last = millis();
    sound_play(Sound::APPROVE);
  }
}

void action_set_sound_enabled(lv_event_t *e) {
  sound_set_enabled(checked(e));
  sound_play(Sound::APPROVE);   // audible confirmation when turned back on (no-op when off)
}

void action_set_read_aloud(lv_event_t *e) {
  settings().read_aloud = checked(e);
  settings_save();
  companion_sync_settings();
}

void action_set_led(lv_event_t *e) {
  settings().led = checked(e);
  settings_save();
}

void action_set_nap(lv_event_t *e) {
  settings().nap = checked(e);
  settings_save();
}

void action_set_hud(lv_event_t *e) {
  settings().hud = checked(e);
  settings_save();
}

void action_set_demo(lv_event_t *e) { link_set_demo(checked(e)); }

void action_set_ui_language(lv_event_t *e) { i18n_set_ui(index_of(e)); }
void action_set_speech_language(lv_event_t *e) { i18n_set_speech(index_of(e)); }
void action_set_orientation(lv_event_t *e) { ui_orientation_set(index_of(e)); }

void action_edit_owner(lv_event_t *) {
  ui_keyboard(i18n_str(TrString::YOUR_NAME), owner_name(), false, owner_saved);
}
void action_edit_pet_name(lv_event_t *) {
  ui_keyboard(i18n_str(TrString::PET_NAME), pet_name(), false, pet_name_saved);
}

void action_set_recap(lv_event_t *e) {
  settings().recap = checked(e);
  settings_save();
}

// Today screen: opening it asks for the shown day; the history button switches today ⇄ yesterday.
void action_summary_open(lv_event_t *) { summary_refresh(); }
void action_summary_toggle(lv_event_t *) { summary_request(summary_day() ? 0 : 1, false); }

void action_set_speaker_volume(lv_event_t *e) {
  settings().speaker = lv_slider_get_value(target(e));
  sound_set_stream_volume(settings().speaker);
  if (released(e)) settings_save();
}

void action_wifi_scan(lv_event_t *) { wifi_scan(); }

void action_reset_action(lv_event_t *e) { ui_reset_tap(index_of(e)); }

void action_import_from_sd(lv_event_t *) { sd_import(); }

}
