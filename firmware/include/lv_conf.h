/* LVGL 9.2 configuration for Vibe-a-Gochi. Anything not set here keeps LVGL's default. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Use the C library allocator: on the S3 with PSRAM, large blocks land in PSRAM. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM   /* src/lv_mem_psram.c: LVGL objects live in PSRAM */
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 16      /* ~60 fps cap */
#define LV_DPI_DEF 143             /* 2.8" 320x240 */

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Fallback/default font; the UI uses its own generated fonts. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1    /* keyboard keys: it carries the LV_SYMBOL_* glyphs the UI fonts lack */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_PERF_MONITOR 0

#endif
