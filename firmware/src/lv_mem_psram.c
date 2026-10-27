/* LVGL heap in PSRAM (LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM).
 *
 * With the C library allocator every small LVGL object (thousands across 20 screens) landed in
 * internal RAM and used it all up, leaving nothing for LittleFS, BLE and WiFi. The display's
 * DMA draw buffers are allocated separately in main.cpp and stay internal. */
#include "lvgl.h"
#include <esp_heap_caps.h>

#define CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void *lv_malloc_core(size_t size) { return heap_caps_malloc(size, CAPS); }

void *lv_realloc_core(void *p, size_t new_size) { return heap_caps_realloc(p, new_size, CAPS); }

void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, CAPS);
  mon_p->total_size = info.total_free_bytes + info.total_allocated_bytes;
  mon_p->free_size = info.total_free_bytes;
  mon_p->free_biggest_size = info.largest_free_block;
  mon_p->used_cnt = info.allocated_blocks;
  mon_p->free_cnt = info.free_blocks;
  mon_p->used_pct = mon_p->total_size ? 100 - (100 * info.total_free_bytes) / mon_p->total_size : 0;
  mon_p->frag_pct = 0;
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
