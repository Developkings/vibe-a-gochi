// Audio: ES8311 setup over I2C, 16 kHz full-duplex I2S, a player task (event sounds and the
// companion's speech) and a mic task.
#include "sound.h"
#include <Arduino.h>
#include <ESP_I2S.h>
#include <Preferences.h>
#include "link.h"
#define LGFX_USE_V1
#include <LovyanGFX.hpp>   // its I2C driver already owns the bus (touch), so the codec goes through it too

namespace {

constexpr int PIN_MCLK = 4, PIN_BCLK = 5, PIN_LRCK = 7, PIN_DOUT = 8, PIN_DIN = 6;
constexpr int PIN_AMP_EN = 1;            // active low (tested on the board, as the README says)
constexpr int I2C_PORT = 0, ES8311_ADDR = 0x18;
constexpr int CHUNK = 256;               // frames per I2S write
constexpr uint32_t AMP_IDLE_MS = 400;    // keep the amp on briefly so back-to-back sounds don't pop
constexpr size_t STREAM_CAP = 32768;     // 2 s of speech
constexpr size_t STREAM_START = 4000;    // 250 ms jitter buffer before speech starts
constexpr int MIC_BLOCK = 320;           // 20 ms

I2SClass i2s;
QueueHandle_t queue;
Preferences prefs;
bool enabled = true;
int volume = 55;
int stream_volume = 70;

int16_t *stream;                          // PSRAM ring, single producer (network) / single consumer (player)
volatile size_t s_head = 0, s_tail = 0;
volatile uint32_t s_last_write = 0;
volatile bool s_playing = false;
volatile uint8_t s_level = 0;

volatile bool mic_on = false;
volatile MicSink mic_sink = nullptr;
volatile uint8_t mic_level = 0;

bool codec_write(uint8_t reg, uint8_t val) {
  return lgfx::i2c::writeRegister8(I2C_PORT, ES8311_ADDR, reg, val, 0, 400000).has_value();
}

// ES8311 as I2S slave, MCLK = 256 x 16 kHz from the ESP32, 16-bit Philips, DAC and analog mic ADC on.
// Same order and values as Espressif's esp_codec_dev es8311 driver (open + start), which the
// board's BSP uses; 0x10/0x11 power the analog reference, without them the output stays silent.
bool codec_begin() {
  static const uint8_t seq[][2] = {
      {0x00, 0x1F}, {0x00, 0x00},                 // reset
      {0x0B, 0x00}, {0x0C, 0x00},                 // system: default power-up timing
      {0x10, 0x1F}, {0x11, 0x7F},                 // analog reference (VMID) and bias on
      {0x00, 0x80},                               // power on, slave mode
      {0x01, 0x3F},                               // all clocks on, MCLK from pin
      {0x02, 0x00},                               // pre-divider 1, multiplier 1
      {0x03, 0x10}, {0x04, 0x10},                 // single speed, ADC/DAC OSR 16
      {0x05, 0x00},                               // ADC/DAC clock dividers 1
      {0x06, 0x03},                               // BCLK = MCLK / 4
      {0x07, 0x00}, {0x08, 0xFF},                 // LRCK = MCLK / 256
      {0x09, 0x0C}, {0x0A, 0x0C},                 // serial in/out: I2S, 16 bit
      {0x0D, 0x01},                               // power up analog circuits
      {0x0E, 0x02},                               // enable PGA + ADC modulator
      {0x0F, 0x44},                               // low-power settings for the ADC/DAC references
      {0x12, 0x00}, {0x13, 0x10},                 // power up DAC, enable the output driver
      {0x14, 0x1A},                               // analog mic on MIC1P/N, PGA +30 dB
      {0x15, 0x00}, {0x1B, 0x0A},                 // ADC ramp rate, ADC high-pass filter
      {0x16, 0x04},                               // ADC digital gain +24 dB (esp_codec_dev default)
      {0x17, 0xBF},                               // ADC volume 0 dB
      {0x1C, 0x6A}, {0x37, 0x08},                 // bypass ADC/DAC equalizers
      {0x44, 0x08},                               // DAC takes the I2S input (no ADC loopback)
      {0x31, 0x00},                               // DAC unmuted
      {0x32, 0xBF},                               // DAC volume 0 dB (loudness is set in software)
  };
  for (auto &r : seq) {
    if (!codec_write(r[0], r[1])) return false;
    if (r[0] == 0x00 && r[1] == 0x1F) delay(20);
  }
  const uint8_t id = lgfx::i2c::readRegister8(I2C_PORT, ES8311_ADDR, 0xFD, 400000).value_or(0);
  if (id != 0x83) Serial.printf("[sound] unexpected codec id %02X (ES8311: 83)\n", id);
  return true;
}

// Same math as ui/tools/gen_sounds.py, so the WAV previews sound like the device.
float voice(float t, float freq, float length) {
  const float env = t < SOUND_ATTACK_S ? t / SOUND_ATTACK_S : expf(-(t - SOUND_ATTACK_S) / (length / 4));
  float s = 0, norm = 0;
  for (auto &h : SOUND_HARMONICS) {
    s += h[1] * sinf(2 * PI * freq * h[0] * t);
    norm += h[1];
  }
  return env * s / norm;
}

// The whole sound is computed first and then written in one go: computing it while playing let
// the DMA run dry whenever BLE/WiFi took the core, which crackled.
void render(const SoundDef &def, float gain) {
  uint32_t end_ms = 0;
  for (int i = 0; i < def.count; i++) end_ms = max<uint32_t>(end_ms, def.notes[i].start_ms + def.notes[i].length_ms);
  const uint32_t frames = (end_ms + 20) * SOUND_RATE / 1000;
  int16_t *buf = static_cast<int16_t *>(heap_caps_malloc(frames * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  if (!buf) return;
  for (uint32_t f = 0; f < frames; f++) {
    const float t = float(f) / SOUND_RATE;
    float v = 0;
    for (int k = 0; k < def.count; k++) {
      const SoundNote &note = def.notes[k];
      const float tt = t - note.start_ms / 1000.0f, len = note.length_ms / 1000.0f;
      if (tt >= 0 && tt < len) v += note.gain * voice(tt, note.freq, len);
    }
    buf[f * 2] = buf[f * 2 + 1] = int16_t(constrain(v * gain, -1.0f, 1.0f) * 32767);
  }
  i2s.write(reinterpret_cast<uint8_t *>(buf), frames * 2 * sizeof(int16_t));
  heap_caps_free(buf);
}

size_t stream_available() { return (s_head + STREAM_CAP - s_tail) % STREAM_CAP; }

// One chunk of speech: mono → stereo with the speaker volume; tracks the level for the pet's mouth.
void play_stream_chunk() {
  int16_t buf[CHUNK * 2];
  const size_t n = min<size_t>(CHUNK, stream_available());
  const float v = stream_volume / 100.0f, gain = v * v * 1.6f;
  uint32_t peak = 0;
  for (size_t i = 0; i < n; i++) {
    const int32_t s = int32_t(stream[s_tail] * gain);
    s_tail = (s_tail + 1) % STREAM_CAP;
    const int16_t c = int16_t(constrain(s, -32768, 32767));
    buf[i * 2] = buf[i * 2 + 1] = c;
    peak = max<uint32_t>(peak, abs(c));
  }
  s_level = min<uint32_t>(100, peak * 100 / 12000);
  i2s.write(reinterpret_cast<uint8_t *>(buf), n * 2 * sizeof(int16_t));
}

void player_task(void *) {
  bool amp_on = false;
  uint32_t last_audio = 0;
  auto amp = [&](bool on) {
    if (on == amp_on) return;
    digitalWrite(PIN_AMP_EN, on ? LOW : HIGH);
    amp_on = on;
    if (on) delay(15);   // let the amp settle before the first sample
  };
  for (;;) {
    Sound s;
    if (xQueueReceive(queue, &s, pdMS_TO_TICKS(s_playing ? 0 : 10)) == pdTRUE) {
      amp(true);
      const float v = volume / 100.0f;
      render(SOUNDS[int(s)], v * v);   // squared: the slider feels even across its range
      last_audio = millis();
      continue;
    }
    const size_t avail = stream_available();
    if (!s_playing && (avail >= STREAM_START || (avail && millis() - s_last_write > 300))) s_playing = true;
    if (s_playing && avail) {
      amp(true);
      play_stream_chunk();
      last_audio = millis();
      continue;
    }
    if (s_playing) {   // ran dry
      s_playing = false;
      s_level = 0;
    }
    if (amp_on && millis() - last_audio > AMP_IDLE_MS) amp(false);   // idle: amp off (no hiss)
  }
}

void mic_task(void *) {
  int16_t raw[MIC_BLOCK * 2], mono[MIC_BLOCK];
  uint32_t energy[2] = {0, 0};
  int blocks = 0, channel = 0;
  bool was_on = false;
  for (;;) {
    if (!mic_on) {
      was_on = false;
      mic_level = 0;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (!was_on) {   // drop what the DMA collected while nobody listened
      for (int i = 0; i < 4; i++) i2s.readBytes(reinterpret_cast<char *>(raw), sizeof raw);
      was_on = true;
    }
    if (i2s.readBytes(reinterpret_cast<char *>(raw), sizeof raw) != sizeof raw) continue;
    // The ES8311 is mono; find which I2S slot carries the mic during the first half second.
    if (blocks < 25) {
      for (int i = 0; i < MIC_BLOCK; i++) energy[0] += abs(raw[i * 2]), energy[1] += abs(raw[i * 2 + 1]);
      channel = energy[1] > energy[0] ? 1 : 0;
      blocks++;
    }
    uint32_t peak = 0;
    for (int i = 0; i < MIC_BLOCK; i++) {
      mono[i] = raw[i * 2 + channel];
      peak = max<uint32_t>(peak, abs(mono[i]));
    }
    mic_level = min<uint32_t>(100, peak * 100 / 16000);
    MicSink sink = mic_sink;
    if (sink) sink(mono, MIC_BLOCK, mic_level);
  }
}

}  // namespace

void sound_begin() {
  prefs.begin("sound", false);
  enabled = prefs.getBool("on", true);
  volume = prefs.getInt("vol", 55);
  stream = static_cast<int16_t *>(heap_caps_malloc(STREAM_CAP * sizeof(int16_t), MALLOC_CAP_SPIRAM));

  pinMode(PIN_AMP_EN, OUTPUT);
  digitalWrite(PIN_AMP_EN, HIGH);  // amp off until a sound plays
  i2s.setPins(PIN_BCLK, PIN_LRCK, PIN_DOUT, PIN_DIN, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SOUND_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[sound] I2S start failed");
    return;
  }
  if (!codec_begin()) Serial.println("[sound] ES8311 not answering on I2C 0x18");
  else Serial.printf("[sound] ES8311 ready, sound %s, volume %d\n", enabled ? "on" : "off", volume);

  queue = xQueueCreate(4, sizeof(Sound));
  xTaskCreatePinnedToCore(player_task, "sound", 6144, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(mic_task, "mic", 6144, nullptr, 3, nullptr, 0);
}

void sound_play(Sound s) {
  static const char *const NAMES[] = {"attention", "question", "done", "approve", "deny", "connected", "disconnected"};
  char note[40];
  snprintf(note, sizeof note, "[sound] %s", NAMES[int(s)]);
  link_debug_note(note);
  if (!enabled || volume == 0 || !queue) return;
  xQueueSend(queue, &s, 0);   // full queue → drop; never block the UI
}

void sound_set_enabled(bool on) {
  enabled = on;
  prefs.putBool("on", on);
}

bool sound_enabled() { return enabled; }

void sound_set_volume(int pct, bool save) {
  volume = constrain(pct, 0, 100);
  if (save) prefs.putInt("vol", volume);   // only on release: spares the flash
}

int sound_volume() { return volume; }

void sound_stream_write(const int16_t *pcm, size_t n) {
  if (!stream) return;
  for (size_t i = 0; i < n; i++) {
    const size_t next = (s_head + 1) % STREAM_CAP;
    if (next == s_tail) break;   // full: the companion sends faster than real time; drop the rest
    stream[s_head] = pcm[i];
    s_head = next;
  }
  s_last_write = millis();
}

void sound_stream_stop() {
  s_tail = s_head;
  s_playing = false;
  s_level = 0;
}

bool sound_stream_active() { return s_playing || stream_available() || millis() - s_last_write < 300; }
uint8_t sound_stream_level() { return s_level; }
void sound_set_stream_volume(int pct) { stream_volume = constrain(pct, 0, 100); }

void sound_mic(bool on, MicSink sink) {
  mic_sink = sink;
  mic_on = on;
}

uint8_t sound_mic_level() { return mic_level; }
