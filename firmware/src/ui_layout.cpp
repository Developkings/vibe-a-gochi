// Screen orientation (Settings · Screen): the display and touch turn, and every generated widget
// moves to its place for that shape (ui_layout.h, from gen_ui.py's second, portrait layout).
#include <Arduino.h>
#include "settings.h"
#include "ui_data.h"
#include "ui_layout.h"

void display_rotate(uint8_t rotation);   // main.cpp

namespace {
bool portrait = false;

lv_coord_t size(int16_t v) { return v == LAYOUT_CONTENT ? LV_SIZE_CONTENT : v; }
}  // namespace

bool ui_portrait() { return portrait; }
int ui_width() { return portrait ? 240 : 320; }
int ui_height() { return portrait ? 320 : 240; }

void ui_orientation_set(uint8_t rotation) {
  rotation &= 3;
  portrait = rotation >= 2;
  display_rotate(rotation);
  for (const LayoutBox &b : LAYOUT) {
    lv_obj_t *o = *b.screen;
    for (const char *p = b.path; *p && o; p++) o = lv_obj_get_child(o, uint8_t(*p) - 1);
    if (!o) continue;
    const int16_t *g = portrait ? b.port : b.land;
    lv_obj_set_pos(o, g[0], g[1]);
    lv_obj_set_size(o, size(g[2]), size(g[3]));
  }
  lv_obj_t *checks[] = {objects.orient_check_0, objects.orient_check_1, objects.orient_check_2,
                        objects.orient_check_3};
  for (int i = 0; i < 4; i++) {
    if (i == rotation) lv_obj_remove_flag(checks[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(checks[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (const LayoutBox &b : LAYOUT)   // every screen: the firmware's lists read their new widths
    if (!*b.path && *b.screen) lv_obj_update_layout(*b.screen);
  ui_live_relayout();   // the lists and buttons the firmware sizes itself
  lv_obj_invalidate(lv_screen_active());
  if (settings().rotation != rotation) {
    settings().rotation = rotation;
    settings_save();
  }
}
