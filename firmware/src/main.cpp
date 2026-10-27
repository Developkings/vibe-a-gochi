// Vibe-a-Gochi: a Claude Hardware Buddy for the Hosyond ES3C28P (ESP32-S3, 2.8" touch).
// Display, touch and LVGL live here; the rest is wired up in setup()/loop().
#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include "ble_link.h"
#include "buddy.h"
#include "companion.h"
#include "i18n.h"
#include "link.h"
#include "pet.h"
#include "power.h"
#include "settings.h"
#include "sound.h"
#include "stats.h"
#include "summary.h"
#include "ui/screens.h"
#include "ui/ui.h"
#include "ui/vars.h"
#include "ui_data.h"
#include "voice.h"
#include "wifi_link.h"

// ---------------------------------------------------------------- board

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_FT5x06 _touch;

 public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host = SPI2_HOST;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.pin_sclk = 12;
      c.pin_mosi = 11;
      c.pin_miso = 13;
      c.pin_dc = 46;
      c.dma_channel = SPI_DMA_CH_AUTO;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs = 10;
      c.pin_rst = -1;
      c.panel_width = 240;
      c.panel_height = 320;
      c.invert = true;           // IPS panel: the board's BSP sets BSP_LCD_INVERTED too
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl = 45;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    {
      auto c = _touch.config();
      c.i2c_port = 0;
      c.pin_sda = 16;
      c.pin_scl = 15;
      c.pin_int = 17;
      c.pin_rst = 18;
      c.i2c_addr = 0x38;
      c.freq = 400000;
      c.x_min = 0;
      c.x_max = 239;
      c.y_min = 0;
      c.y_max = 319;
      c.offset_rotation = 2;     // this board's touch panel is mounted turned 180° to the display
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;

void display_backlight(uint8_t level) { tft.setBrightness(level); }

// ---------------------------------------------------------------- LVGL glue

static constexpr int W = 320, H = 240;
static constexpr int BUF_LINES = 30;   // two 19 KB partial buffers in internal DMA RAM

// Screenshots (display_capture): while capturing, every flushed area is also copied here.
static uint16_t *shot = nullptr;
static bool capturing = false;

static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
  const int w = lv_area_get_width(a), h = lv_area_get_height(a);
  tft.pushImage(a->x1, a->y1, w, h, reinterpret_cast<lgfx::rgb565_t *>(px));
  if (capturing && shot) {
    const int sw = lv_display_get_horizontal_resolution(disp);
    for (int y = 0; y < h; y++)
      memcpy(shot + (a->y1 + y) * sw + a->x1, px + y * w * 2, w * 2);
  }
  lv_display_flush_ready(disp);
}

// A touch that wakes the screen is not passed on (until the finger lifts), so it can't press anything.
static void touch_cb(lv_indev_t *, lv_indev_data_t *d) {
  static bool swallow = false;
  int32_t x, y;
  const bool down = tft.getTouch(&x, &y);
  if (down && !swallow && power_touch() && !power_napping()) swallow = true;
  if (!down) swallow = false;
  if (down && !swallow) {
    d->point.x = x;
    d->point.y = y;
    d->state = LV_INDEV_STATE_PRESSED;
  } else {
    d->state = LV_INDEV_STATE_RELEASED;
  }
}

static uint32_t tick_cb() { return millis(); }

static lv_display_t *disp;

// Settings.rotation → LovyanGFX rotation (it turns the touch with the panel); LVGL gets the new size.
// The draw buffers hold 30 landscape lines, which is 40 portrait ones: no new buffers needed.
void display_rotate(uint8_t rotation) {
  static const uint8_t PANEL[] = {1, 3, 2, 0};   // portrait: USB side down, flipped: USB side up
  tft.setRotation(PANEL[rotation & 3]);
  const bool portrait = (rotation & 3) >= 2;
  lv_display_set_resolution(disp, portrait ? H : W, portrait ? W : H);
}

static void lvgl_begin() {
  lv_init();
  lv_tick_set_cb(tick_cb);

  disp = lv_display_create(W, H);
  const size_t bytes = W * BUF_LINES * sizeof(uint16_t);
  void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);

  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_cb);
}

// The whole screen as RGB565 (overlays included): redraws everything once into a PSRAM copy.
const uint16_t *display_capture(int &w, int &h) {
  w = lv_display_get_horizontal_resolution(disp);
  h = lv_display_get_vertical_resolution(disp);
  if (!shot) shot = static_cast<uint16_t *>(heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM));
  if (!shot) return nullptr;
  capturing = true;
  lv_obj_invalidate(lv_screen_active());
  lv_obj_invalidate(lv_layer_top());
  lv_refr_now(disp);
  capturing = false;
  return shot;
}

void ui_set_text(FlowGlobalVariables var, const char *text) {
  eez::flow::setGlobalVariable(var, eez::StringValue(text));
}

// ---------------------------------------------------------------- main

// EEZ sets label texts with lv_label_set_text_static, pointing into flash. A label in "…" mode
// writes the dots into its text, which faults on read-only flash: give those labels a copy.
static void own_dot_texts(lv_obj_t *obj) {
  if (lv_obj_check_type(obj, &lv_label_class) && lv_label_get_long_mode(obj) == LV_LABEL_LONG_DOT)
    lv_label_set_text(obj, lv_label_get_text(obj));
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) own_dot_texts(lv_obj_get_child(obj, i));
}

static void own_dot_texts_all() {
  lv_obj_t **all = reinterpret_cast<lv_obj_t **>(&objects);
  for (size_t i = 0; i < sizeof objects / sizeof(lv_obj_t *); i++)
    if (all[i] && !lv_obj_get_parent(all[i])) own_dot_texts(all[i]);   // screens only
}

// Boot diagnostics: free internal RAM after each stage (BLE, WiFi and LittleFS need it).
static void boot_mem(const char *stage) {
  Serial.printf("[boot] %-6s internal %6u (largest %6u) psram %7u\n", stage,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 1500) delay(10);   // let a connected PC catch the boot log
  // Plain malloc/new from 256 bytes up goes to PSRAM (default: only from 4 KB), so the internal
  // RAM stays free for what needs it: BLE and WiFi buffers, DMA, task stacks.
  heap_caps_malloc_extmem_enable(256);
  settings_load();
  stats_load();
  tft.init();
  tft.setRotation(1);          // landscape, 320x240
  tft.setBrightness(0);
  tft.fillScreen(TFT_BLACK);
  sound_begin();               // after tft.init(): shares the touch I2C bus
  sound_set_stream_volume(settings().speaker);

  boot_mem("start");
  lvgl_begin();
  boot_mem("lvgl");
  ui_init();
  own_dot_texts_all();
  boot_mem("ui");
  ui_orientation_set(settings().rotation);   // after ui_init: moves the generated widgets
  pet_begin();                // LittleFS, characters, the pet image buffers
  i18n_begin();                // saved interface language relabels all screens
  ui_live_begin();
  power_begin();               // backlight on at the saved brightness
  ui_splash();
  boot_mem("pet+ui");

  ble_begin();
  boot_mem("ble");
  link_begin();
  buddy_begin();
  wifi_begin();
  boot_mem("wifi");
  Serial.printf("Vibe-a-Gochi up · heap %u · psram %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop() {
  static uint32_t next_beat = 0;
  if (millis() >= next_beat) {   // heartbeat: shows the loop runs, and the memory over time
    next_beat = millis() + 60000;
    Serial.printf("[alive] %lus internal %u psram %u\n", millis() / 1000, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  }
  link_poll();
  buddy_tick();
  voice_tick();
  wifi_tick();
  companion_tick();
  summary_tick();
  power_tick(buddy_in_prompt() || voice_state() != VoiceState::OFF || xfer_active() || ble_passkey() ||
             companion_pair_code() || ui_ask_active());
  if (!power_screen_off()) pet_tick(buddy_state());
  ui_live_tick();
  lv_timer_handler();
  ui_tick();
  delay(power_screen_off() ? 20 : 2);
}
