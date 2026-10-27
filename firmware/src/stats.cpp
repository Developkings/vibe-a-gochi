#include "stats.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "link.h"

namespace {
Stats st;
Preferences prefs;

// Per-day answers: [0] today, [1] yesterday, for the day `day_key` (year * 400 + day of year).
uint32_t day_key = 0;
uint16_t day_appr[2] = {}, day_den[2] = {};

uint32_t today_key() {
  if (!link_rtc_valid()) return 0;
  const time_t now = time(nullptr);
  tm lt;
  gmtime_r(&now, &lt);   // the system clock holds local time (link.cpp)
  return uint32_t(lt.tm_year) * 400 + lt.tm_yday;
}

// Moves the counters along when the day changed (a gap of more days clears both).
void roll_days() {
  const uint32_t k = today_key();
  if (!k || k == day_key) return;
  const bool next_day = day_key && (k == day_key + 1 || (k / 400 == day_key / 400 + 1 && k % 400 == 0));
  day_appr[1] = next_day ? day_appr[0] : 0;
  day_den[1] = next_day ? day_den[0] : 0;
  day_appr[0] = day_den[0] = 0;
  day_key = k;
}
uint32_t last_bridge_tokens = 0;
bool tokens_synced = false;   // first-sight latch: a device reboot must not re-credit the whole session
bool level_up_pending = false;
uint32_t last_nap_end_ms = 0;
uint8_t energy_at_nap = 3;

void save() {
  prefs.begin("buddy", false);
  prefs.putUInt("nap", st.nap_seconds);
  prefs.putUShort("appr", st.approvals);
  prefs.putUShort("deny", st.denials);
  prefs.putUChar("vidx", st.vel_idx);
  prefs.putUChar("vcnt", st.vel_count);
  prefs.putUChar("lvl", st.level);
  prefs.putUInt("tok", st.tokens);
  prefs.putBytes("vel", st.velocity, sizeof st.velocity);
  prefs.putUInt("dkey", day_key);
  prefs.putBytes("dappr", day_appr, sizeof day_appr);
  prefs.putBytes("dden", day_den, sizeof day_den);
  prefs.end();
}
}  // namespace

void stats_day_answers(int day, uint16_t &approved, uint16_t &denied) {
  roll_days();
  const int i = day ? 1 : 0;
  approved = day_key ? day_appr[i] : 0;
  denied = day_key ? day_den[i] : 0;
}

void stats_load() {
  prefs.begin("buddy", true);
  st.nap_seconds = prefs.getUInt("nap", 0);
  st.approvals = prefs.getUShort("appr", 0);
  st.denials = prefs.getUShort("deny", 0);
  st.vel_idx = prefs.getUChar("vidx", 0) % 8;
  st.vel_count = min<uint8_t>(prefs.getUChar("vcnt", 0), 8);
  st.level = prefs.getUChar("lvl", 0);
  st.tokens = prefs.getUInt("tok", 0);
  if (prefs.getBytes("vel", st.velocity, sizeof st.velocity) != sizeof st.velocity) memset(st.velocity, 0, sizeof st.velocity);
  day_key = prefs.getUInt("dkey", 0);
  prefs.getBytes("dappr", day_appr, sizeof day_appr);
  prefs.getBytes("dden", day_den, sizeof day_den);
  prefs.end();
  if (st.tokens == 0 && st.level > 0) st.tokens = uint32_t(st.level) * TOKENS_PER_LEVEL;
}

const Stats &stats() { return st; }

void stats_on_approval(uint32_t secs) {
  st.approvals++;
  roll_days();
  day_appr[0]++;
  st.velocity[st.vel_idx] = uint16_t(min<uint32_t>(secs, 65535));
  st.vel_idx = (st.vel_idx + 1) % 8;
  if (st.vel_count < 8) st.vel_count++;
  save();
}

void stats_on_denial() {
  st.denials++;
  roll_days();
  day_den[0]++;
  save();
}

void stats_on_bridge_tokens(uint32_t total) {
  if (!tokens_synced) {
    last_bridge_tokens = total;
    tokens_synced = true;
    return;
  }
  if (total < last_bridge_tokens) {   // the desktop restarted
    last_bridge_tokens = total;
    return;
  }
  const uint32_t delta = total - last_bridge_tokens;
  last_bridge_tokens = total;
  if (!delta) return;
  const uint8_t before = st.tokens / TOKENS_PER_LEVEL;
  st.tokens += delta;
  const uint8_t after = st.tokens / TOKENS_PER_LEVEL;
  if (after > before) {   // persist only on level-ups: heartbeats are frequent
    st.level = after;
    level_up_pending = true;
    save();
  }
}

bool stats_poll_level_up() {
  const bool r = level_up_pending;
  level_up_pending = false;
  return r;
}

void stats_on_nap_end(uint32_t seconds) {
  st.nap_seconds += seconds;
  save();
}

void stats_on_wake() {
  last_nap_end_ms = millis();
  energy_at_nap = 5;
}

uint16_t stats_median_velocity() {
  if (!st.vel_count) return 0;
  uint16_t tmp[8];
  memcpy(tmp, st.velocity, sizeof tmp);
  const uint8_t n = st.vel_count;
  for (uint8_t i = 1; i < n; i++) {
    const uint16_t k = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > k) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = k;
  }
  return tmp[n / 2];
}

uint8_t stats_mood_tier() {
  const uint16_t vel = stats_median_velocity();
  int tier = vel == 0 ? 2 : vel < 15 ? 4 : vel < 30 ? 3 : vel < 60 ? 2 : vel < 120 ? 1 : 0;
  const uint16_t a = st.approvals, d = st.denials;
  if (a + d >= 3) {
    if (d > a) tier -= 2;
    else if (d * 2 > a) tier -= 1;
  }
  return max(tier, 0);
}

uint8_t stats_energy_tier() {
  const uint32_t hours = (millis() - last_nap_end_ms) / 3600000;
  return constrain(int(energy_at_nap) - int(hours / 2), 0, 5);
}

uint8_t stats_fed_progress() { return (st.tokens % TOKENS_PER_LEVEL) / (TOKENS_PER_LEVEL / 10); }

void stats_reset() {
  memset(&st, 0, sizeof st);
  day_key = 0;
  memset(day_appr, 0, sizeof day_appr);
  memset(day_den, 0, sizeof day_den);
  save();
}
