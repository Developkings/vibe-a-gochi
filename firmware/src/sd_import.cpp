// Characters → Install → "Import from SD card": copies every /characters/<name>/ folder with a
// manifest.json from the SD card (SDIO, 4-bit) into LittleFS, then selects the last one.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include "i18n.h"
#include "i18n_strings.h"
#include "pet.h"
#include "ui_data.h"

namespace {
constexpr int SD_CLK = 38, SD_CMD = 40, SD_D0 = 39, SD_D1 = 41, SD_D2 = 48, SD_D3 = 47;

void status(const char *t) {
  lv_label_set_text(objects.install_status, t);
  lv_refr_now(nullptr);   // the copy blocks the loop: show progress now
}

bool copy_file(File &src, const char *dst_path) {
  File dst = LittleFS.open(dst_path, "w");
  if (!dst) return false;
  constexpr size_t CAP = 4096;
  static uint8_t *const buf = static_cast<uint8_t *>(heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  for (size_t n; (n = src.read(buf, CAP)) > 0;)
    if (dst.write(buf, n) != n) return false;
  return true;
}
}  // namespace

void sd_import() {
  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3) || !SD_MMC.begin("/sdcard", false)) {
    status(i18n_str(TrString::SD_MISSING));
    return;
  }
  File root = SD_MMC.open("/characters");
  int imported = 0;
  char last[24] = "";
  if (root && root.isDirectory()) {
    for (File dir = root.openNextFile(); dir; dir = root.openNextFile()) {
      if (!dir.isDirectory() || dir.name()[0] == '.' || strlen(dir.name()) >= sizeof last) continue;
      char path[96];
      snprintf(path, sizeof path, "/characters/%s/manifest.json", dir.name());
      if (!SD_MMC.exists(path)) continue;
      // Fits? Size the folder first so a full flash never leaves half a character behind.
      uint32_t bytes = 0;
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) bytes += f.size();
      if (bytes + 8192 > LittleFS.totalBytes() - LittleFS.usedBytes()) {
        status(i18n_str(TrString::NO_SPACE));
        continue;
      }
      char msg[80];
      snprintf(msg, sizeof msg, i18n_str(TrString::INSTALLING), dir.name(), 0);
      status(msg);
      pet_close();
      char dst_dir[48];
      snprintf(dst_dir, sizeof dst_dir, "/characters/%s", dir.name());
      LittleFS.mkdir(dst_dir);
      dir.rewindDirectory();
      bool ok = true;
      uint32_t done = 0;
      for (File f = dir.openNextFile(); f && ok; f = dir.openNextFile()) {
        if (f.isDirectory() || f.name()[0] == '.') continue;
        char dst[96];
        snprintf(dst, sizeof dst, "%s/%s", dst_dir, f.name());
        ok = copy_file(f, dst);
        done += f.size();
        snprintf(msg, sizeof msg, i18n_str(TrString::INSTALLING), dir.name(), bytes ? int(done * 100ull / bytes) : 100);
        status(msg);
      }
      if (ok) {
        imported++;
        strlcpy(last, dir.name(), sizeof last);
      }
    }
  }
  SD_MMC.end();
  if (!imported) {
    status(i18n_str(TrString::SD_NONE));
    return;
  }
  pet_rescan();
  pet_select_pack(last);
  ui_rebuild_characters();
  char msg[64];
  snprintf(msg, sizeof msg, i18n_str(TrString::INSTALLED), last);
  status(msg);
}
