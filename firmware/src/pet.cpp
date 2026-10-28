// Pet drawing. Everything is drawn into ARGB8888 buffers (PSRAM) with a transparent background
// and handed to the EEZ image widgets, so the pet floats over the screen like the designer preview.
//
//   ASCII species: the reference state functions draw text into a 135x82 1x canvas
//                  (GLCD 5x7 font, 6x8 cells), scaled with nearest-neighbour per slot.
//   GIF packs:     AnimatedGIF decodes a frame into its own canvas, scaled to fit the slot.
//                  Packs with an opaque background (like bufo's black) get that background keyed
//                  out from the edges inward, so dark ink inside the character stays.
#include "pet.h"
#include <Arduino.h>
#include <AnimatedGIF.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <new>
#include "glcdfont.h"
#include "pet_ascii.h"
#include "settings.h"
#include "ui/screens.h"

// ---------------------------------------------------------------- species API (pet_ascii.h)

const int BUDDY_X_CENTER = 67;
const int BUDDY_CANVAS_W = 135;
const int BUDDY_Y_BASE = 30;
const int BUDDY_Y_OVERLAY = 6;
const int BUDDY_CHAR_W = 6;
const int BUDDY_CHAR_H = 8;

const uint16_t BUDDY_BG = 0x0000;
const uint16_t BUDDY_HEART = 0xF810;
const uint16_t BUDDY_DIM = 0x8410;
const uint16_t BUDDY_YEL = 0xFFE0;
const uint16_t BUDDY_WHITE = 0xFFFF;
const uint16_t BUDDY_CYAN = 0x07FF;
const uint16_t BUDDY_GREEN = 0x07E0;
const uint16_t BUDDY_PURPLE = 0xA01F;
const uint16_t BUDDY_RED = 0xF800;
const uint16_t BUDDY_BLUE = 0x041F;

extern const Species CAPYBARA_SPECIES, DUCK_SPECIES, GOOSE_SPECIES, BLOB_SPECIES, CAT_SPECIES, DRAGON_SPECIES,
    OCTOPUS_SPECIES, OWL_SPECIES, PENGUIN_SPECIES, TURTLE_SPECIES, SNAIL_SPECIES, GHOST_SPECIES, AXOLOTL_SPECIES,
    CACTUS_SPECIES, ROBOT_SPECIES, RABBIT_SPECIES, MUSHROOM_SPECIES, CHONK_SPECIES;

namespace {

// Same order as the reference: the index is the "species" protocol value.
const Species *const SPECIES[] = {
    &CAPYBARA_SPECIES, &DUCK_SPECIES,   &GOOSE_SPECIES,   &BLOB_SPECIES,  &CAT_SPECIES,     &DRAGON_SPECIES,
    &OCTOPUS_SPECIES,  &OWL_SPECIES,    &PENGUIN_SPECIES, &TURTLE_SPECIES, &SNAIL_SPECIES,  &GHOST_SPECIES,
    &AXOLOTL_SPECIES,  &CACTUS_SPECIES, &ROBOT_SPECIES,   &RABBIT_SPECIES, &MUSHROOM_SPECIES, &CHONK_SPECIES,
};
constexpr int N_SPECIES = sizeof SPECIES / sizeof SPECIES[0];
constexpr int DEFAULT_SPECIES = 4;   // cat
constexpr int MAX_PACKS = 16;

constexpr int CW = 135, CH = 82;     // 1x canvas
uint32_t *canvas;
int cur_x = 0, cur_y = 0;
uint32_t cur_color = 0xFFFFFFFF;

void *ps_alloc(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

uint32_t argb(uint16_t c) {
  const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

void glyph(uint32_t *dst, int dw, int dh, int x, int y, uint8_t ch, uint32_t color, int size = 1) {
  for (int col = 0; col < 5; col++) {
    const uint8_t bits = GLCD_FONT[ch * 5 + col];
    for (int row = 0; row < 8; row++) {
      if (!(bits >> row & 1)) continue;
      for (int sy = 0; sy < size; sy++)
        for (int sx = 0; sx < size; sx++) {
          const int px = x + col * size + sx, py = y + row * size + sy;
          if (px >= 0 && px < dw && py >= 0 && py < dh) dst[py * dw + px] = color;
        }
    }
  }
}

void clear_canvas() { memset(canvas, 0, CW * CH * 4); }

}  // namespace

void buddyPrintLine(const char *line, int y, uint16_t color, int xOff) {
  const int len = strlen(line);
  const int x = BUDDY_X_CENTER - len * BUDDY_CHAR_W / 2 + xOff;
  const uint32_t c = argb(color);
  for (int i = 0; i < len; i++) glyph(canvas, CW, CH, x + i * BUDDY_CHAR_W, y, line[i], c);
}

void buddyPrintSprite(const char *const *lines, uint8_t n, int yOffset, uint16_t color, int xOff) {
  for (uint8_t i = 0; i < n; i++) buddyPrintLine(lines[i], BUDDY_Y_BASE + yOffset + i * BUDDY_CHAR_H, color, xOff);
}

void buddySetCursor(int x, int y) {
  cur_x = x;
  cur_y = y;
}

void buddySetColor(uint16_t fg) { cur_color = argb(fg); }

void buddyPrint(const char *s) {
  for (; *s; s++) {
    if (*s == '\n') {
      cur_x = 0;
      cur_y += BUDDY_CHAR_H;
      continue;
    }
    glyph(canvas, CW, CH, cur_x, cur_y, *s, cur_color);
    cur_x += BUDDY_CHAR_W;
  }
}

// ---------------------------------------------------------------- catalog

namespace {

const char *const STATE_NAMES[P_COUNT] = {"sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart", "talk", "listen"};

struct Entry {
  char name[24];
  bool gif;
  uint8_t species;   // ASCII: index into SPECIES
  lv_draw_buf_t *thumb;
};
Entry entries[N_SPECIES + MAX_PACKS];
int n_entries = 0;
int selected = DEFAULT_SPECIES;
bool fs_ok = false;

// The selected GIF pack, parsed from its manifest.
struct Pack {
  char dir[48];
  bool text_mode;
  bool key_bg;
  uint32_t bg;          // 0xRRGGBB
  uint16_t body;
  uint8_t start[P_COUNT], count[P_COUNT], rot[P_COUNT];
  char files[32][32];
  uint8_t n_files;
  char text[P_COUNT][8][20];
  uint8_t text_n[P_COUNT];
  uint16_t text_delay[P_COUNT];
} *pack;

// ---------------------------------------------------------------- GIF decoding

AnimatedGIF &gif = *new (ps_alloc(sizeof(AnimatedGIF))) AnimatedGIF;   // 24 KB: PSRAM, not internal RAM
File gif_file;
bool gif_open = false;
uint32_t *gcanvas = nullptr;
uint32_t *gqueue = nullptr;   // flood fill work list
int gw = 0, gh = 0;
size_t gcap = 0;

void *gif_open_cb(const char *fname, int32_t *size) {
  gif_file = LittleFS.open(fname, "r");
  if (!gif_file) return nullptr;
  *size = gif_file.size();
  return &gif_file;
}
void gif_close_cb(void *h) {
  if (h) static_cast<File *>(h)->close();
}
int32_t gif_read_cb(GIFFILE *f, uint8_t *buf, int32_t len) {
  File *file = static_cast<File *>(f->fHandle);
  const int32_t n = file->read(buf, len);
  f->iPos = file->position();
  return n;
}
int32_t gif_seek_cb(GIFFILE *f, int32_t pos) {
  File *file = static_cast<File *>(f->fHandle);
  file->seek(pos);
  f->iPos = file->position();
  return f->iPos;
}

// Packs are full-frame GIFs (the reference's prep tool unoptimizes them): transparent = background.
void gif_draw_cb(GIFDRAW *d) {
  const int y = d->iY + d->y;
  if (y < 0 || y >= gh) return;
  uint32_t *row = gcanvas + y * gw;
  const uint16_t *pal = d->pPalette;
  for (int i = 0; i < d->iWidth; i++) {
    const int x = d->iX + i;
    if (x < 0 || x >= gw) continue;
    const uint8_t idx = d->pPixels[i];
    row[x] = (d->ucHasTransparency && idx == d->ucTransparent) ? 0 : argb(pal[idx]);
  }
}

bool gif_alloc(int w, int h) {
  const size_t need = size_t(w) * h;
  if (need > gcap) {
    free(gcanvas);
    free(gqueue);
    gcanvas = static_cast<uint32_t *>(ps_alloc(need * 4));
    gqueue = static_cast<uint32_t *>(ps_alloc(need * 4));
    gcap = gcanvas && gqueue ? need : 0;
    if (!gcap) return false;
  }
  gw = w;
  gh = h;
  memset(gcanvas, 0, need * 4);
  return true;
}

// Make the background transparent where it touches the frame edge (flood fill from the border).
// GIF frames usually redraw only the part that changed, so pixels keyed out in earlier frames
// (alpha 0) must be passable too, or a freshly drawn patch of background inside them would stay
// as an opaque box. Each pass marks what it visits with its own invisible value (alpha 0).
void key_background(uint32_t bg) {
  static uint32_t mark = 0;
  mark = mark % 250 + 1;
  const uint32_t key = 0xFF000000u | bg;
  size_t head = 0, tail = 0;
  auto push = [&](int x, int y) {
    uint32_t &p = gcanvas[y * gw + x];
    if (p != key && ((p >> 24) || p == mark)) return;
    p = mark;
    gqueue[tail++] = y * gw + x;
  };
  for (int x = 0; x < gw; x++) push(x, 0), push(x, gh - 1);
  for (int y = 0; y < gh; y++) push(0, y), push(gw - 1, y);
  while (head < tail) {
    const uint32_t i = gqueue[head++];
    const int x = i % gw, y = i / gw;
    if (x > 0) push(x - 1, y);
    if (x < gw - 1) push(x + 1, y);
    if (y > 0) push(x, y - 1);
    if (y < gh - 1) push(x, y + 1);
  }
}

bool gif_start(const char *path) {
  if (gif_open) {
    gif.close();
    gif_open = false;
  }
  if (!gif.open(path, gif_open_cb, gif_close_cb, gif_read_cb, gif_seek_cb, gif_draw_cb)) {
    Serial.printf("[pet] cannot open %s (%d)\n", path, gif.getLastError());
    return false;
  }
  const int w = gif.getCanvasWidth(), h = gif.getCanvasHeight();
  if (w <= 0 || h <= 0 || w > 320 || h > 240 || !gif_alloc(w, h)) {
    Serial.printf("[pet] %s: unsupported size %dx%d\n", path, w, h);
    gif.close();
    return false;
  }
  gif_open = true;
  return true;
}

// ---------------------------------------------------------------- slots

struct Slot {
  lv_obj_t **obj;
  float scale;
  lv_draw_buf_t *buf;
};
Slot slots[] = {{&objects.home_pet, 1.5f}, {&objects.approval_pet, 1.0f}, {&objects.voice_pet, 1.5f},
                {&objects.pet_pet, 1.0f},  {&objects.clock_pet, 1.0f}};
constexpr int N_SLOTS = sizeof slots / sizeof slots[0];

void blit(Slot &s, const uint32_t *src, int sw, int sh, bool fit) {
  lv_draw_buf_t *b = s.buf;
  const int w = b->header.w, h = b->header.h, stride = b->header.stride / 4;
  uint32_t *dst = reinterpret_cast<uint32_t *>(b->data);
  int ow = w, oh = h, ox = 0, oy = 0;
  if (fit) {   // GIF: keep the aspect ratio, centered
    float k = min(float(w) / sw, float(h) / sh);
    // whole-pixel enlargement keeps pixel art crisp; 1.5x too, so canvas-sized packs (135x82, as
    // the ASCII pets) fill the Home slot the way they do
    if (k >= 2) k = floorf(k);
    else if (k >= 1.5f) k = 1.5f;
    else if (k >= 1) k = 1;
    ow = max(1, int(sw * k));
    oh = max(1, int(sh * k));
    ox = (w - ow) / 2;
    oy = (h - oh) / 2;
    for (int y = 0; y < h; y++) memset(dst + y * stride, 0, w * 4);
  }
  for (int y = 0; y < oh; y++) {
    const uint32_t *srow = src + (y * sh / oh) * sw;
    uint32_t *drow = dst + (oy + y) * stride + ox;
    for (int x = 0; x < ow; x++) drow[x] = srow[x * sw / ow];
  }
  lv_image_cache_drop(b);
  lv_obj_invalidate(*s.obj);
}

int active_slot() {
  lv_obj_t *scr = lv_screen_active();
  for (int i = 0; i < N_SLOTS; i++)
    if (*slots[i].obj && lv_obj_get_screen(*slots[i].obj) == scr) return i;
  return -1;
}

// ---------------------------------------------------------------- pack loading

bool parse_pack(const char *name, Pack &p) {
  memset(&p, 0, sizeof p);
  snprintf(p.dir, sizeof p.dir, "/characters/%s", name);
  char path[80];
  snprintf(path, sizeof path, "%s/manifest.json", p.dir);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[pet] %s: %s\n", path, err.c_str());
    return false;
  }
  const char *bg = doc["colors"]["bg"];
  p.key_bg = bg && (doc["key_bg"] | true);
  p.bg = bg ? strtoul(bg + (*bg == '#'), nullptr, 16) : 0;
  const char *body = doc["colors"]["body"];
  const uint32_t bc = body ? strtoul(body + (*body == '#'), nullptr, 16) : 0xC2A6C2;
  p.body = ((bc >> 19 & 31) << 11) | ((bc >> 10 & 63) << 5) | (bc >> 3 & 31);
  p.text_mode = doc["mode"] == "text";
  JsonObject states = doc["states"];
  for (int s = 0; s < P_COUNT; s++) {
    JsonVariant v = states[STATE_NAMES[s]];
    if (p.text_mode) {
      p.text_delay[s] = v["delay"] | 200;
      for (JsonVariant fr : v["frames"].as<JsonArray>()) {
        if (p.text_n[s] >= 8) break;
        strlcpy(p.text[s][p.text_n[s]++], fr | "", 20);
      }
      continue;
    }
    p.start[s] = p.n_files;
    auto add = [&](const char *fn) {
      if (fn && p.n_files < 32 && !strstr(fn, "..")) strlcpy(p.files[p.n_files++], fn, 32), p.count[s]++;
    };
    if (v.is<JsonArray>())
      for (JsonVariant e : v.as<JsonArray>()) add(e.as<const char *>());
    else
      add(v.as<const char *>());
  }
  return true;
}

// Missing optional states fall back to one the pack has.
uint8_t pack_state(uint8_t s) {
  auto has = [](uint8_t st) { return pack->text_mode ? pack->text_n[st] > 0 : pack->count[st] > 0; };
  if (has(s)) return s;
  if (s == P_TALK && has(P_BUSY)) return P_BUSY;
  if (s == P_LISTEN && has(P_ATTENTION)) return P_ATTENTION;
  return has(P_IDLE) ? P_IDLE : s;
}

// ---------------------------------------------------------------- playback state

uint8_t shown_state = 0xFF;
int shown_slot = -1;
int shown_entry = -1;
uint32_t tick_count = 0, next_tick = 0;
uint32_t next_frame = 0, variant_started = 0, pause_until = 0;
uint8_t text_frame = 0;
bool dirty = true;
constexpr uint32_t TICK_MS = 200;            // ASCII animation rate (reference: 5 fps)
constexpr uint32_t VARIANT_DWELL_MS = 5000;  // loop one idle variant this long before rotating
constexpr uint32_t ANIM_PAUSE_MS = 800;

void open_state(uint8_t s) {
  if (pack->text_mode || !pack->count[s]) return;
  char path[96];
  snprintf(path, sizeof path, "%s/%s", pack->dir, pack->files[pack->start[s] + pack->rot[s]]);
  if (gif_start(path)) {
    next_frame = 0;
    variant_started = millis();
  }
}

void draw_text_frame(uint8_t s) {
  clear_canvas();
  const char *line = pack->text[s][text_frame % max<uint8_t>(1, pack->text_n[s])];
  const int len = strlen(line);
  const uint32_t c = argb(pack->body);
  for (int i = 0; i < len; i++) glyph(canvas, CW, CH, (CW - len * 12) / 2 + i * 12, 34, line[i], c, 2);
}

void tick_gif(uint8_t s, Slot &slot, bool changed) {
  const uint32_t now = millis();
  if (pack->text_mode) {
    if (!changed && now < next_frame) return;
    next_frame = now + pack->text_delay[s];
    draw_text_frame(s);
    text_frame++;
    blit(slot, canvas, CW, CH, false);
    return;
  }
  if (changed) {
    pause_until = 0;
    open_state(s);
  }
  if (!gif_open) {
    if (pause_until && now >= pause_until) {
      pause_until = 0;
      open_state(s);
    }
    if (changed && gcanvas) blit(slot, gcanvas, gw, gh, true);
    return;
  }
  if (!changed && now < next_frame) return;
  int delay_ms = 0;
  if (!gif.playFrame(false, &delay_ms)) {
    // End of the animation: loop it; idle rotations move to the next variant after a while.
    if (pack->count[s] > 1 && now - variant_started >= VARIANT_DWELL_MS) {
      gif.close();
      gif_open = false;
      pack->rot[s] = (pack->rot[s] + 1) % pack->count[s];
      pause_until = now + ANIM_PAUSE_MS;
      return;
    }
    gif.reset();
    gif.playFrame(false, &delay_ms);
  }
  next_frame = now + (delay_ms > 0 ? delay_ms : 100);
  if (pack->key_bg) key_background(pack->bg);
  blit(slot, gcanvas, gw, gh, true);
}

void tick_ascii(uint8_t s, Slot &slot, bool changed) {
  const uint32_t now = millis();
  bool ticked = false;
  if (int32_t(now - next_tick) >= 0) {
    next_tick = now + TICK_MS;
    tick_count++;
    ticked = true;
  }
  if (!ticked && !changed) return;
  if (s == P_TALK) s = P_BUSY;
  if (s == P_LISTEN) s = P_ATTENTION;
  const Species *sp = SPECIES[entries[selected].species];
  clear_canvas();
  if (sp->states[s]) sp->states[s](tick_count);
  blit(slot, canvas, CW, CH, false);
}

void load_selected() {
  if (gif_open) {
    gif.close();
    gif_open = false;
  }
  if (entries[selected].gif && !parse_pack(entries[selected].name, *pack)) {
    Serial.printf("[pet] pack %s is broken, using cat\n", entries[selected].name);
    selected = DEFAULT_SPECIES;
  }
  dirty = true;
}

// ---------------------------------------------------------------- thumbnails

// Crop the drawn pixels and scale them into a new buffer no bigger than 80x56.
lv_draw_buf_t *make_thumb(const uint32_t *src, int sw, int sh, float max_scale) {
  int x0 = sw, y0 = sh, x1 = -1, y1 = -1;
  for (int y = 0; y < sh; y++)
    for (int x = 0; x < sw; x++)
      if (src[y * sw + x] >> 24) x0 = min(x0, x), x1 = max(x1, x), y0 = min(y0, y), y1 = max(y1, y);
  if (x1 < 0) return nullptr;
  const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  const float k = min(max_scale, min(80.0f / bw, 56.0f / bh));
  const int tw = max(1, int(bw * k)), th = max(1, int(bh * k));
  lv_draw_buf_t *t = lv_draw_buf_create(tw, th, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
  if (!t) return nullptr;
  for (int y = 0; y < th; y++) {
    uint32_t *drow = reinterpret_cast<uint32_t *>(t->data + y * t->header.stride);
    for (int x = 0; x < tw; x++) drow[x] = src[(y0 + y * bh / th) * sw + x0 + x * bw / tw];
  }
  return t;
}

lv_draw_buf_t *thumb_for(int i) {
  const Entry &e = entries[i];
  dirty = true;   // the shared canvases get overwritten: redraw the live pet afterwards
  if (!e.gif) {
    clear_canvas();
    SPECIES[e.species]->states[P_IDLE](0);
    return make_thumb(canvas, CW, CH, 1.5f);
  }
  static Pack tp;   // big: keep it off the stack
  if (!parse_pack(e.name, tp)) return nullptr;
  if (tp.text_mode) {
    clear_canvas();
    const char *line = tp.text[P_IDLE][0];
    for (int k = 0; line[k]; k++) glyph(canvas, CW, CH, k * 12, 34, line[k], argb(tp.body), 2);
    return make_thumb(canvas, CW, CH, 1.0f);
  }
  if (!tp.count[P_IDLE]) return nullptr;
  char path[96];
  snprintf(path, sizeof path, "%s/%s", tp.dir, tp.files[tp.start[P_IDLE]]);
  const bool was_open = gif_open;
  if (was_open) gif.close();   // the shared decoder; the current state reopens below
  gif_open = false;
  lv_draw_buf_t *t = nullptr;
  if (gif_start(path)) {
    int d;
    gif.playFrame(false, &d);
    if (tp.key_bg) key_background(tp.bg);
    t = make_thumb(gcanvas, gw, gh, 1.0f);
    gif.close();
    gif_open = false;
  }
  return t;
}

void scan() {
  for (int i = 0; i < n_entries; i++)
    if (entries[i].thumb) lv_draw_buf_destroy(entries[i].thumb);
  n_entries = 0;
  for (int i = 0; i < N_SPECIES; i++) {
    Entry &e = entries[n_entries++];
    strlcpy(e.name, SPECIES[i]->name, sizeof e.name);
    e.gif = false;
    e.species = i;
    e.thumb = nullptr;
  }
  if (!fs_ok) return;
  File root = LittleFS.open("/characters");
  if (!root || !root.isDirectory()) return;
  for (File d = root.openNextFile(); d && n_entries < N_SPECIES + MAX_PACKS; d = root.openNextFile()) {
    if (!d.isDirectory()) continue;
    char path[80];
    snprintf(path, sizeof path, "/characters/%s/manifest.json", d.name());
    if (!LittleFS.exists(path)) continue;
    Entry &e = entries[n_entries++];
    strlcpy(e.name, d.name(), sizeof e.name);
    e.gif = true;
    e.thumb = nullptr;
  }
}

int find(const char *key) {
  if (!key || strlen(key) < 3 || key[1] != ':') return -1;
  for (int i = 0; i < n_entries; i++)
    if (entries[i].gif == (key[0] == 'g') && !strcmp(entries[i].name, key + 2)) return i;
  return -1;
}

void restore_selection() {
  int i = find(character_key());
  if (i < 0) {   // nothing saved (or it was deleted): the newest pack if any, else the cat
    i = DEFAULT_SPECIES;
    for (int k = N_SPECIES; k < n_entries; k++) i = k;
  }
  selected = i;
  load_selected();
}

}  // namespace

// ---------------------------------------------------------------- public

void pet_begin() {
  canvas = static_cast<uint32_t *>(ps_alloc(CW * CH * 4));
  pack = static_cast<Pack *>(ps_alloc(sizeof(Pack)));
  fs_ok = LittleFS.begin(true);   // formats on first boot
  if (fs_ok && !LittleFS.exists("/characters")) LittleFS.mkdir("/characters");
  gif.begin(GIF_PALETTE_RGB565_LE);
  for (Slot &s : slots) {
    const int w = lroundf(CW * s.scale), h = lroundf(CH * s.scale);
    s.buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    lv_draw_buf_clear(s.buf, nullptr);
    lv_image_set_src(*s.obj, s.buf);
  }
  scan();
  restore_selection();
  Serial.printf("[pet] %d characters, showing %s\n", n_entries, entries[selected].name);
}

void pet_tick(uint8_t state) {
  const int si = active_slot();
  if (si < 0) return;
  bool changed = dirty || si != shown_slot || selected != shown_entry;
  if (entries[selected].gif) state = pack_state(state);
  if (state != shown_state) changed = true;
  shown_slot = si;
  shown_entry = selected;
  shown_state = state;
  dirty = false;
  if (entries[selected].gif) tick_gif(state, slots[si], changed);
  else tick_ascii(state, slots[si], changed);
}

void pet_rescan() {
  pet_close();
  scan();
  restore_selection();
}

int pet_count() { return n_entries; }
const char *pet_entry_name(int i) { return entries[i].name; }
bool pet_entry_is_gif(int i) { return entries[i].gif; }
int pet_selected() { return selected; }

void pet_select(int i) {
  if (i < 0 || i >= n_entries) return;
  selected = i;
  char key[32];
  snprintf(key, sizeof key, "%c:%s", entries[i].gif ? 'g' : 'a', entries[i].name);
  character_key_set(key);
  load_selected();
}

void pet_select_species(uint8_t idx) {
  if (idx < N_SPECIES) pet_select(idx);
  else if (idx == SPECIES_GIF && n_entries > N_SPECIES) pet_select(n_entries - 1);   // newest pack
}

const char *pet_select_pack(const char *name) {
  for (int k = N_SPECIES; k < n_entries; k++)
    if (!strcmp(entries[k].name, name)) {
      pet_select(k);
      return entries[k].name;
    }
  return nullptr;
}

uint8_t pet_species_index() { return entries[selected].gif ? SPECIES_GIF : entries[selected].species; }
const char *pet_species_name() { return entries[selected].name; }

const lv_image_dsc_t *pet_thumb(int i) {
  if (i < 0 || i >= n_entries) return nullptr;
  if (!entries[i].thumb) entries[i].thumb = thumb_for(i);
  return reinterpret_cast<const lv_image_dsc_t *>(entries[i].thumb);
}

void pet_close() {
  if (gif_open) {
    gif.close();
    gif_open = false;
  }
}

void pet_delete_all_packs() {
  pet_close();
  File root = LittleFS.open("/characters");
  if (root && root.isDirectory()) {
    for (File d = root.openNextFile(); d; d = root.openNextFile()) {
      if (!d.isDirectory()) continue;
      char dir[64];
      snprintf(dir, sizeof dir, "/characters/%s", d.name());
      File sub = LittleFS.open(dir);
      for (File f = sub.openNextFile(); f; f = sub.openNextFile()) {
        char p[112];
        snprintf(p, sizeof p, "%s/%s", dir, f.name());
        f.close();
        LittleFS.remove(p);
      }
      sub.close();
      LittleFS.rmdir(dir);
    }
  }
  pet_rescan();
}
