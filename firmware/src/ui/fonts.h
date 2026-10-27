#ifndef EEZ_LVGL_UI_FONTS_H
#define EEZ_LVGL_UI_FONTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t ui_font_t9;
extern const lv_font_t ui_font_t11;
extern const lv_font_t ui_font_t12;
extern const lv_font_t ui_font_t14;
extern const lv_font_t ui_font_t16;
extern const lv_font_t ui_font_h18;
extern const lv_font_t ui_font_h24;
extern const lv_font_t ui_font_m12;
extern const lv_font_t ui_font_clock40;
extern const lv_font_t ui_font_clock64;
extern const lv_font_t ui_font_i16;
extern const lv_font_t ui_font_i20;
extern const lv_font_t ui_font_i24;
extern const lv_font_t ui_font_i32;

#ifndef EXT_FONT_DESC_T
#define EXT_FONT_DESC_T
typedef struct _ext_font_desc_t {
    const char *name;
    const void *font_ptr;
} ext_font_desc_t;
#endif

extern ext_font_desc_t fonts[];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_FONTS_H*/