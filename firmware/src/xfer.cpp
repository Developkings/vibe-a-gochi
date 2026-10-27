// Folder push: the desktop's Hardware Buddy window streams a character folder as
// char_begin → (file → chunk… → file_end)… → char_end, each acked. Port of the reference xfer.h,
// except that several packs can be installed side by side (only a pack of the same name is replaced)
// and names and paths are checked so nothing lands outside /characters/<name>/.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include "link.h"
#include "pet.h"

void link_ack(const char *what, bool ok, uint32_t n = 0);

namespace {

File file;
uint32_t expected = 0, written = 0;
uint32_t total = 0, total_written = 0;
char name[24] = "";
bool active = false;

// A pack name or file name: letters, digits, - _ . and not starting with a dot (so no "..").
bool safe_name(const char *s, size_t max_len) {
  const size_t n = strlen(s);
  if (!n || n >= max_len || s[0] == '.') return false;
  for (size_t i = 0; i < n; i++) {
    const char c = s[i];
    if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
  }
  return true;
}

uint32_t dir_bytes(const char *dir, bool remove) {
  uint32_t bytes = 0;
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) return 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    bytes += f.size();
    char p[112];
    snprintf(p, sizeof p, "%s/%s", dir, f.name());
    f.close();
    if (remove) LittleFS.remove(p);
  }
  d.close();
  if (remove) LittleFS.rmdir(dir);
  return bytes;
}

}  // namespace

bool xfer_command(JsonDocument &doc) {
  const char *cmd = doc["cmd"];
  if (!strcmp(cmd, "char_begin")) {
    const char *n = doc["name"] | "pet";
    total = doc["total"] | 0;
    if (!safe_name(n, sizeof name)) {
      link_ack("char_begin", false);
      return true;
    }
    char dir[48];
    snprintf(dir, sizeof dir, "/characters/%s", n);
    const uint32_t available = LittleFS.totalBytes() - LittleFS.usedBytes() + dir_bytes(dir, false);
    if (total && total + 4096 > available) {   // LittleFS metadata needs some headroom
      char b[128];
      snprintf(b, sizeof b, "{\"ack\":\"char_begin\",\"ok\":false,\"n\":%lu,\"error\":\"need %luK, have %luK\"}",
               (unsigned long)available, (unsigned long)(total / 1024), (unsigned long)(available / 1024));
      link_send(b);
      return true;
    }
    strlcpy(name, n, sizeof name);
    pet_close();
    dir_bytes(dir, true);
    LittleFS.mkdir("/characters");
    LittleFS.mkdir(dir);
    total_written = 0;
    active = true;
    link_ack("char_begin", true);
    return true;
  }
  if (!active) return false;

  if (!strcmp(cmd, "file")) {
    const char *path = doc["path"] | "";
    expected = doc["size"] | 0;
    written = 0;
    if (!safe_name(path, 32)) {
      link_ack("file", false);
      return true;
    }
    char full[80];
    snprintf(full, sizeof full, "/characters/%s/%s", name, path);
    file = LittleFS.open(full, "w");
    link_ack("file", bool(file));
    return true;
  }
  if (!strcmp(cmd, "chunk")) {
    const char *b64 = doc["d"];
    uint8_t buf[512];
    size_t out = 0;
    if (!b64 || !file || mbedtls_base64_decode(buf, sizeof buf, &out, (const uint8_t *)b64, strlen(b64))) {
      link_ack("chunk", false);
      return true;
    }
    file.write(buf, out);
    written += out;
    total_written += out;
    link_ack("chunk", true, written);   // per-chunk ack: flash erases block, the sender must wait
    return true;
  }
  if (!strcmp(cmd, "file_end")) {
    const bool ok = file && (written == expected || expected == 0);
    if (file) file.close();
    link_ack("file_end", ok, written);
    return true;
  }
  if (!strcmp(cmd, "char_end")) {
    active = false;
    pet_rescan();
    const bool ok = pet_select_pack(name) != nullptr;
    link_ack("char_end", ok);
    return true;
  }
  return false;
}

bool xfer_active() { return active; }
uint32_t xfer_progress() { return total_written; }
uint32_t xfer_total() { return total; }
const char *xfer_name() { return name; }
