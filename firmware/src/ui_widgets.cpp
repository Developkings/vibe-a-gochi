// Widgets the firmware builds itself on the top layer: toast, keyboard, boot splash.
#include <Arduino.h>
#include "i18n.h"
#include "i18n_strings.h"
#include "settings.h"
#include "theme.h"
#include "ui/fonts.h"
#include "ui_data.h"

namespace {
KeyboardDone kb_done = nullptr;
KeyboardCancelled kb_cancelled = nullptr;
lv_obj_t *kb_overlay = nullptr;

lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  return o;
}

void kb_event(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
  lv_obj_t *ta = lv_keyboard_get_textarea(static_cast<lv_obj_t *>(lv_event_get_target(e)));
  KeyboardDone done = kb_done;
  KeyboardCancelled cancelled = kb_cancelled;
  kb_done = nullptr;
  kb_cancelled = nullptr;
  String text = lv_textarea_get_text(ta);   // copy: the overlay goes away below
  text.trim();
  lv_obj_delete_async(kb_overlay);
  kb_overlay = nullptr;
  if (code == LV_EVENT_READY && done && text.length()) done(text.c_str());
  else if (cancelled) cancelled();
}
}  // namespace

void ui_toast(const char *text) {
  lv_obj_t *t = plain(lv_layer_top());
  lv_obj_set_style_bg_color(t, THEME_RAISED, 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(t, 12, 0);
  lv_obj_set_style_pad_hor(t, 12, 0);
  lv_obj_set_style_pad_ver(t, 7, 0);
  lv_obj_set_style_border_color(t, THEME_LINE, 0);
  lv_obj_set_style_border_width(t, 1, 0);
  lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(t, ui_width() - 30, 0);
  lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -58);
  lv_obj_t *l = lv_label_create(t);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &ui_font_t12, 0);
  lv_obj_set_style_text_color(l, THEME_TEXT, 0);
  lv_obj_set_style_max_width(l, ui_width() - 54, 0);
  lv_obj_fade_out(t, 300, 2200);
  lv_obj_delete_delayed(t, 2600);
}

namespace {
lv_obj_t *prog_overlay, *prog_bar, *prog_pct, *prog_note, *prog_icon;
bool prog_done = false;

void anim_y(void *o, int32_t v) { lv_obj_set_style_translate_y(static_cast<lv_obj_t *>(o), v, 0); }
void anim_rot(void *o, int32_t v) { lv_obj_set_style_transform_rotation(static_cast<lv_obj_t *>(o), v, 0); }

void icon_anim(bool spin) {
  lv_anim_delete(prog_icon, nullptr);
  lv_obj_set_style_translate_y(prog_icon, 0, 0);
  lv_obj_set_style_transform_rotation(prog_icon, 0, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, prog_icon);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  if (spin) {   // checked: turning while it restarts
    lv_anim_set_exec_cb(&a, anim_rot);
    lv_anim_set_values(&a, 0, 3600);
    lv_anim_set_duration(&a, 900);
  } else {      // receiving: a gentle bob, like something dropping in
    lv_anim_set_exec_cb(&a, anim_y);
    lv_anim_set_values(&a, -4, 4);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_playback_duration(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  }
  lv_anim_start(&a);
}
}  // namespace

void ui_progress(const char *title, int pct, const char *note, bool done) {
  if (!prog_overlay) {
    const int dy = (ui_height() - 240) / 2;   // portrait: the same block, centred
    prog_overlay = plain(lv_layer_top());
    lv_obj_set_size(prog_overlay, ui_width(), ui_height());
    lv_obj_set_style_bg_color(prog_overlay, THEME_BG, 0);
    lv_obj_set_style_bg_opa(prog_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(prog_overlay, LV_OBJ_FLAG_CLICKABLE);   // nothing behind it reacts

    prog_icon = lv_label_create(prog_overlay);
    lv_obj_set_style_text_font(prog_icon, &ui_font_i20, 0);
    lv_obj_set_style_text_color(prog_icon, THEME_ACCENT, 0);
    lv_obj_set_style_transform_scale(prog_icon, 512, 0);   // 2x: a 40 px icon
    lv_obj_align(prog_icon, LV_ALIGN_TOP_MID, 0, 46 + dy);

    lv_obj_t *t = lv_label_create(prog_overlay);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &ui_font_h18, 0);
    lv_obj_set_style_text_color(t, THEME_TEXT, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 96 + dy);

    prog_bar = lv_bar_create(prog_overlay);
    lv_obj_set_size(prog_bar, ui_width() - 80, 12);
    lv_obj_align(prog_bar, LV_ALIGN_TOP_MID, 0, 134 + dy);
    lv_bar_set_range(prog_bar, 0, 100);
    lv_obj_set_style_radius(prog_bar, 6, 0);
    lv_obj_set_style_radius(prog_bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(prog_bar, THEME_SURFACE, 0);
    lv_obj_set_style_bg_opa(prog_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(prog_bar, THEME_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(prog_bar, THEME_ACCENT_HI, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(prog_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(prog_bar, 400, 0);   // the fill glides between updates

    prog_pct = lv_label_create(prog_overlay);
    lv_obj_set_style_text_font(prog_pct, &ui_font_t16, 0);
    lv_obj_set_style_text_color(prog_pct, THEME_ACCENT_HI, 0);
    lv_obj_align(prog_pct, LV_ALIGN_TOP_MID, 0, 156 + dy);

    prog_note = lv_label_create(prog_overlay);
    lv_obj_set_style_text_font(prog_note, &ui_font_t12, 0);
    lv_obj_set_style_text_color(prog_note, THEME_DIM, 0);
    lv_obj_align(prog_note, LV_ALIGN_TOP_MID, 0, 186 + dy);

    prog_done = !done;   // forces the icon and its animation below
  }
  if (done != prog_done) {
    prog_done = done;
    lv_label_set_text(prog_icon, done ? ICON_SYNC : ICON_DOWNLOAD);
    lv_obj_update_layout(prog_icon);
    lv_obj_set_style_transform_pivot_x(prog_icon, lv_obj_get_width(prog_icon) / 2, 0);
    lv_obj_set_style_transform_pivot_y(prog_icon, lv_obj_get_height(prog_icon) / 2, 0);
    icon_anim(done);
  }
  pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
  if (lv_bar_get_value(prog_bar) != pct) lv_bar_set_value(prog_bar, pct, LV_ANIM_ON);
  lv_label_set_text_fmt(prog_pct, "%d%%", pct);
  lv_label_set_text(prog_note, note);
}

void ui_progress_close() {
  if (!prog_overlay) return;
  lv_anim_delete(prog_icon, nullptr);
  lv_obj_delete(prog_overlay);
  prog_overlay = prog_bar = prog_pct = prog_note = prog_icon = nullptr;
}

void ui_keyboard(const char *title, const char *initial, bool password, KeyboardDone done,
                 KeyboardCancelled cancelled) {
  if (kb_overlay) lv_obj_delete(kb_overlay);
  kb_done = done;
  kb_cancelled = cancelled;
  kb_overlay = plain(lv_layer_top());
  lv_obj_set_size(kb_overlay, ui_width(), ui_height());
  lv_obj_set_style_bg_color(kb_overlay, THEME_BG, 0);
  lv_obj_set_style_bg_opa(kb_overlay, LV_OPA_COVER, 0);
  lv_obj_add_flag(kb_overlay, LV_OBJ_FLAG_CLICKABLE);   // nothing behind it reacts

  lv_obj_t *l = lv_label_create(kb_overlay);
  lv_label_set_text(l, title);
  lv_obj_set_style_text_font(l, &ui_font_t12, 0);
  lv_obj_set_style_text_color(l, THEME_DIM, 0);
  lv_obj_set_pos(l, 12, 6);

  lv_obj_t *ta = lv_textarea_create(kb_overlay);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_password_mode(ta, password);
  lv_textarea_set_text(ta, initial ? initial : "");
  lv_obj_set_pos(ta, 8, 24);
  lv_obj_set_size(ta, ui_width() - 16, 38);
  lv_obj_set_style_text_font(ta, &ui_font_t14, 0);
  lv_obj_set_style_text_color(ta, THEME_TEXT, 0);
  lv_obj_set_style_bg_color(ta, THEME_SURFACE, 0);
  lv_obj_set_style_border_color(ta, THEME_ACCENT, 0);
  lv_obj_set_style_radius(ta, 10, 0);
  lv_obj_set_style_bg_color(ta, THEME_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);

  lv_obj_t *kb = lv_keyboard_create(kb_overlay);
  lv_obj_set_size(kb, ui_width(), ui_portrait() ? 220 : 172);   // portrait: narrow keys, so taller ones
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_set_style_bg_color(kb, THEME_BG, 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_pad_all(kb, 4, 0);
  lv_obj_set_style_pad_gap(kb, 4, 0);
  lv_obj_set_style_bg_color(kb, THEME_SURFACE, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, THEME_TEXT, LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, LV_PART_ITEMS);   // has the key symbols (⌫ ↵ ⇧ …)
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, THEME_RAISED, LV_PART_ITEMS | LV_STATE_CHECKED);
  // The special keys (⌫ ↵ ⇧ ABC…) are "checked": the theme gives them a text colour close to THEME_RAISED.
  lv_obj_set_style_text_color(kb, THEME_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(kb, THEME_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, nullptr);
}

// ---------------------------------------------------------------- ask (board MCP)

namespace {
lv_obj_t *ask_overlay = nullptr;
AskDone ask_done = nullptr;
lv_obj_t *ask_btns[4];
int ask_n = 0;
bool ask_multi = false;
constexpr int ASK_CONFIRM = 100;   // the multi-select confirm button

void ask_finish(int choice) {
  AskDone done = ask_done;
  ask_done = nullptr;
  if (ask_overlay) lv_obj_delete_async(ask_overlay);
  ask_overlay = nullptr;
  if (done) done(choice);
}

void ask_clicked(lv_event_t *e) {
  const int i = int(intptr_t(lv_event_get_user_data(e)));
  if (ask_multi && i >= 0 && i < ask_n) return;   // a choice toggles (checkable)
  if (i != ASK_CONFIRM) return ask_finish(i);
  int mask = 0;
  for (int k = 0; k < ask_n; k++)
    if (lv_obj_has_state(ask_btns[k], LV_STATE_CHECKED)) mask |= 1 << k;
  if (mask) ask_finish(ASK_MULTI | mask);   // nothing checked yet: wait
}

lv_obj_t *ask_button(lv_obj_t *parent, const char *text, int id, bool primary, int w) {
  lv_obj_t *b = plain(parent);
  lv_obj_set_size(b, w, 34);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, primary ? THEME_ACCENT : THEME_SURFACE, 0);
  lv_obj_set_style_bg_color(b, THEME_ACCENT_HI, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(b, THEME_ACCENT_SOFT, LV_STATE_CHECKED);
  lv_obj_set_style_border_color(b, THEME_ACCENT, LV_STATE_CHECKED);
  lv_obj_set_style_border_width(b, 2, LV_STATE_CHECKED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, ask_clicked, LV_EVENT_CLICKED, (void *)intptr_t(id));
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, w - 16);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(l, w < 120 ? &ui_font_t12 : &ui_font_t14, 0);   // portrait half width
  lv_obj_set_style_text_color(l, primary ? THEME_ON_ACCENT : THEME_TEXT, 0);
  lv_obj_center(l);
  return b;
}
}  // namespace

void ui_ask(const char *question, const char *const *choices, int n, AskDone done, const char *who_text,
            const char *other, const char *multi_done) {
  if (ask_overlay) ask_finish(-1);   // a newer question replaces the open one
  ask_done = done;
  ask_overlay = plain(lv_layer_top());
  lv_obj_set_size(ask_overlay, ui_width(), ui_height());
  lv_obj_set_style_bg_color(ask_overlay, THEME_BG, 0);
  lv_obj_set_style_bg_opa(ask_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(ask_overlay, 10, 0);
  lv_obj_set_style_pad_row(ask_overlay, 6, 0);
  lv_obj_set_flex_flow(ask_overlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(ask_overlay, LV_OBJ_FLAG_CLICKABLE);   // nothing behind it reacts

  lv_obj_t *head = plain(ask_overlay);   // "? Claude asks" + close
  lv_obj_set_size(head, lv_pct(100), 24);
  lv_obj_t *icon = lv_label_create(head);
  lv_label_set_text(icon, ICON_QUESTION_MARK);
  lv_obj_set_style_text_font(icon, &ui_font_i20, 0);
  lv_obj_set_style_text_color(icon, THEME_ACCENT, 0);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *who = lv_label_create(head);
  lv_label_set_text(who, who_text && *who_text ? who_text : "Claude asks");
  lv_label_set_long_mode(who, LV_LABEL_LONG_DOT);
  lv_obj_set_width(who, ui_width() - 80);
  lv_obj_set_style_text_font(who, &ui_font_t12, 0);
  lv_obj_set_style_text_color(who, THEME_DIM, 0);
  lv_obj_align(who, LV_ALIGN_LEFT_MID, 26, 0);
  lv_obj_t *close = plain(head);
  lv_obj_set_size(close, 32, 24);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(close, 8);
  lv_obj_add_event_cb(close, ask_clicked, LV_EVENT_CLICKED, (void *)intptr_t(-1));
  lv_obj_t *x = lv_label_create(close);
  lv_label_set_text(x, ICON_CLOSE);
  lv_obj_set_style_text_font(x, &ui_font_i20, 0);
  lv_obj_set_style_text_color(x, THEME_FAINT, 0);
  lv_obj_center(x);

  // The question takes what the buttons leave (two lines with four choices) and scrolls when longer.
  lv_obj_t *qbox = plain(ask_overlay);
  lv_obj_set_width(qbox, lv_pct(100));
  lv_obj_set_flex_grow(qbox, 1);
  lv_obj_set_scroll_dir(qbox, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(qbox, LV_SCROLLBAR_MODE_AUTO);   // a bar shows there is more to read
  lv_obj_set_style_bg_color(qbox, THEME_FAINT, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(qbox, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(qbox, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(qbox, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(qbox, 6, 0);
  lv_obj_t *q = lv_label_create(qbox);
  lv_label_set_text(q, question);
  lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(q, lv_pct(100));
  lv_obj_set_style_text_font(q, strlen(question) > 60 ? &ui_font_t14 : &ui_font_t16, 0);
  lv_obj_set_style_text_color(q, THEME_TEXT, 0);

  n = n < 1 ? 0 : n > 4 ? 4 : n;
  ask_n = n;
  ask_multi = multi_done != nullptr;
  // The buttons in two columns when there are more than two (portrait: more than three), so the
  // question keeps room.
  const int total = n + (other ? 1 : 0) + (ask_multi ? 1 : 0);
  lv_obj_t *btns = plain(ask_overlay);
  lv_obj_set_size(btns, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(btns, 6, 0);
  const int full = ui_width() - 20, w = total > (ui_portrait() ? 3 : 2) ? (full - 6) / 2 : full;
  for (int i = 0; i < n; i++) {
    ask_btns[i] = ask_button(btns, choices[i], i, i == 0 && !ask_multi, w);
    if (ask_multi) lv_obj_add_flag(ask_btns[i], LV_OBJ_FLAG_CHECKABLE);
  }
  if (other) ask_button(btns, other, n, false, w);
  if (ask_multi) ask_button(btns, multi_done, ASK_CONFIRM, true, w);
}

void ui_ask_close() {
  ask_done = nullptr;
  if (ask_overlay) lv_obj_delete_async(ask_overlay);
  ask_overlay = nullptr;
}

bool ui_ask_active() { return ask_overlay != nullptr; }

// ---------------------------------------------------------------- splash

namespace {
void anim_scale(void *o, int32_t v) { lv_obj_set_style_transform_scale(static_cast<lv_obj_t *>(o), v, 0); }
void anim_width(void *o, int32_t v) { lv_obj_set_width(static_cast<lv_obj_t *>(o), v); }
void anim_opa(void *o, int32_t v) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(o), v, 0); }

void animate(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
             lv_anim_path_cb_t path = lv_anim_path_ease_out) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_path_cb(&a, path);
  lv_anim_start(&a);
}
}  // namespace

// Boot splash: a small "Developkings awesome" over a big orange VIBE-A-GOCHI that zooms in with a
// retro drop shadow, then an orange line sweeps out underneath.
void ui_splash() {
  lv_obj_t *s = plain(lv_layer_top());
  lv_obj_set_size(s, ui_width(), ui_height());
  lv_obj_set_style_bg_color(s, THEME_BG, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);

  lv_obj_t *small = lv_label_create(s);
  lv_label_set_text(small, "Developkings awesome");
  lv_obj_set_style_text_font(small, &ui_font_t12, 0);
  lv_obj_set_style_text_color(small, THEME_DIM, 0);
  lv_obj_set_style_text_letter_space(small, 3, 0);
  lv_obj_align(small, LV_ALIGN_CENTER, 0, -36);
  lv_obj_set_style_opa(small, LV_OPA_TRANSP, 0);

  // The title and its shadow share a box, so one transform zooms both.
  lv_obj_t *title = plain(s);
  lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -2);
  lv_obj_t *labels[2];
  for (int i = 0; i < 2; i++) {   // 0: shadow (dark orange, offset), 1: the title
    lv_obj_t *l = labels[i] = lv_label_create(title);
    lv_label_set_text(l, "VIBE-A-GOCHI");
    lv_obj_set_style_text_font(l, &ui_font_h24, 0);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_set_style_text_color(l, i ? THEME_ACCENT : lv_color_darken(THEME_ACCENT, LV_OPA_60), 0);
    lv_obj_set_pos(l, i ? 0 : 3, i ? 0 : 3);
  }
  lv_obj_update_layout(title);
  lv_obj_set_style_transform_pivot_x(title, lv_obj_get_width(title) / 2, 0);
  lv_obj_set_style_transform_pivot_y(title, lv_obj_get_height(title) / 2, 0);
  lv_obj_set_style_transform_scale(title, 0, 0);

  lv_obj_t *line = plain(s);
  lv_obj_set_size(line, 0, 3);
  lv_obj_set_style_bg_color(line, THEME_ACCENT, 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(line, 2, 0);
  lv_obj_align(line, LV_ALIGN_CENTER, 0, 30);

  // 256 = 1x; the title ends at 1.25x (about 300 px wide), overshooting a little on the way.
  animate(title, anim_scale, 0, ui_portrait() ? 228 : 320, 650, 100, lv_anim_path_overshoot);   // portrait: ~215 px
  animate(small, anim_opa, LV_OPA_TRANSP, LV_OPA_COVER, 400, 500);
  animate(line, anim_width, 0, ui_width() - 100, 450, 750);
  lv_obj_fade_out(s, 400, 2000);
  lv_obj_delete_delayed(s, 2500);
}
