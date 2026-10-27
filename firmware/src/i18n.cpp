#include "i18n.h"
#include <Arduino.h>
#include <Preferences.h>
#include "i18n_strings.h"
#include "i18n_table.h"
#include "ui/vars.h"
#include "ui_data.h"
#include "companion.h"

namespace {
Preferences prefs;
int ui_lang = 0;
int speech = 0;   // 0 = follow the interface

void show_check(lv_obj_t *(*check)(int), int count, int selected) {
  for (int i = 0; i < count; i++) {
    if (i == selected) lv_obj_remove_flag(check(i), LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(check(i), LV_OBJ_FLAG_HIDDEN);
  }
}

void refresh_speech_name() {
  ui_set_text(FLOW_GLOBAL_VARIABLE_SPEECH_LANGUAGE_NAME,
              speech == 0 ? i18n_str(TrString::SAME_AS_INTERFACE) : I18N_NAMES[speech - 1]);
}
}  // namespace

void i18n_begin() {
  prefs.begin("i18n", false);
  ui_lang = constrain(prefs.getInt("ui", 0), 0, I18N_LANG_COUNT - 1);
  speech = constrain(prefs.getInt("speech", 0), 0, I18N_LANG_COUNT);
  i18n_set_ui(ui_lang);
  i18n_set_speech(speech);
}

void i18n_set_ui(int lang) {
  ui_lang = constrain(lang, 0, I18N_LANG_COUNT - 1);
  for (const TrLabel &t : TR_LABELS) {
    if (*t.obj) lv_label_set_text(*t.obj, t.text[ui_lang]);   // a copy: "…" labels write into their text
  }
  show_check(lang_check, I18N_LANG_COUNT, ui_lang);
  ui_set_text(FLOW_GLOBAL_VARIABLE_UI_LANGUAGE_NAME, I18N_NAMES[ui_lang]);
  refresh_speech_name();   // "Same as interface" is itself translated
  if (prefs.getInt("ui", -1) != ui_lang) prefs.putInt("ui", ui_lang);
  companion_sync_settings();
}

void i18n_set_speech(int choice) {
  speech = constrain(choice, 0, I18N_LANG_COUNT);
  show_check(speech_check, I18N_LANG_COUNT + 1, speech);
  refresh_speech_name();
  if (prefs.getInt("speech", -1) != speech) prefs.putInt("speech", speech);
  companion_sync_settings();   // whisper + Piper follow the speech language
}

int i18n_ui() { return ui_lang; }

const char *i18n_ui_code() { return I18N_CODES[ui_lang]; }

const char *i18n_speech_code() { return I18N_CODES[speech == 0 ? ui_lang : speech - 1]; }

const char *i18n_str(TrString s) { return TR_STRINGS[int(s)][ui_lang]; }
