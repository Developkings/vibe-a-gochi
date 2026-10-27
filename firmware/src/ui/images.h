#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_pv_approval;
extern const lv_img_dsc_t img_pv_clock;
extern const lv_img_dsc_t img_pv_home;
extern const lv_img_dsc_t img_pv_pet;
extern const lv_img_dsc_t img_thumb_axolotl;
extern const lv_img_dsc_t img_thumb_blob;
extern const lv_img_dsc_t img_thumb_capybara;
extern const lv_img_dsc_t img_thumb_cat;
extern const lv_img_dsc_t img_thumb_developking;
extern const lv_img_dsc_t img_thumb_dragon;
extern const lv_img_dsc_t img_thumb_duck;
extern const lv_img_dsc_t img_thumb_ghost;
extern const lv_img_dsc_t img_thumb_penguin;
extern const lv_img_dsc_t img_thumb_robot;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[14];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/