#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_permission_approve(lv_event_t * e);
extern void action_permission_deny(lv_event_t * e);
extern void action_permission_always(lv_event_t * e);
extern void action_send_quick_reply(lv_event_t * e);
extern void action_type_message(lv_event_t * e);
extern void action_voice_open(lv_event_t * e);
extern void action_voice_close(lv_event_t * e);
extern void action_voice_stop(lv_event_t * e);
extern void action_voice_mute(lv_event_t * e);
extern void action_push_to_talk_down(lv_event_t * e);
extern void action_push_to_talk_up(lv_event_t * e);
extern void action_send_command(lv_event_t * e);
extern void action_edit_custom_command(lv_event_t * e);
extern void action_new_chat(lv_event_t * e);
extern void action_set_brightness(lv_event_t * e);
extern void action_set_volume(lv_event_t * e);
extern void action_set_sound_enabled(lv_event_t * e);
extern void action_set_read_aloud(lv_event_t * e);
extern void action_set_led(lv_event_t * e);
extern void action_set_nap(lv_event_t * e);
extern void action_set_hud(lv_event_t * e);
extern void action_set_demo(lv_event_t * e);
extern void action_set_ui_language(lv_event_t * e);
extern void action_set_speech_language(lv_event_t * e);
extern void action_set_orientation(lv_event_t * e);
extern void action_set_speaker_volume(lv_event_t * e);
extern void action_set_recap(lv_event_t * e);
extern void action_summary_toggle(lv_event_t * e);
extern void action_summary_open(lv_event_t * e);
extern void action_stop_claude(lv_event_t * e);
extern void action_edit_owner(lv_event_t * e);
extern void action_edit_pet_name(lv_event_t * e);
extern void action_wifi_scan(lv_event_t * e);
extern void action_reset_action(lv_event_t * e);
extern void action_import_from_sd(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/