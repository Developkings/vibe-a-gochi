#include "chat_log.h"
#include <esp_heap_caps.h>
#include <string.h>

namespace {
ChatMsg *const ring =   // 24 KB in PSRAM: internal RAM is kept for BLE and WiFi
    static_cast<ChatMsg *>(heap_caps_calloc(CHAT_MAX, sizeof(ChatMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
int head = 0, count = 0;
uint16_t gen = 0;
}  // namespace

void chat_clear() {
  head = count = 0;
  gen++;
}

void chat_add(ChatKind kind, const char *text) {
  if (!text || !*text) return;
  ChatMsg &m = ring[(head + count) % CHAT_MAX];
  if (count == CHAT_MAX) head = (head + 1) % CHAT_MAX;
  else count++;
  m.kind = kind;
  size_t n = strlen(text);
  if (n >= sizeof m.text) {   // too long: cut before a UTF-8 continuation byte and mark it with "…"
    n = sizeof m.text - 4;
    while (n && (uint8_t(text[n]) & 0xC0) == 0x80) n--;
    memcpy(m.text, text, n);
    memcpy(m.text + n, "\xE2\x80\xA6", 4);
  } else {
    memcpy(m.text, text, n + 1);
  }
  gen++;
}

int chat_count() { return count; }
const ChatMsg &chat_at(int i) { return ring[(head + i) % CHAT_MAX]; }
uint16_t chat_gen() { return gen; }
