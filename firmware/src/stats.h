// Pet stats, ported from the reference stats.h: approvals, denials, response speed, naps,
// and the token-driven level. Saved to NVS only on events (flash has ~100K write cycles).
#pragma once
#include <stdint.h>

constexpr uint32_t TOKENS_PER_LEVEL = 50000;

struct Stats {
  uint32_t nap_seconds;
  uint16_t approvals;
  uint16_t denials;
  uint16_t velocity[8];   // seconds to answer, last 8 approvals
  uint8_t vel_idx;
  uint8_t vel_count;
  uint8_t level;
  uint32_t tokens;        // cumulative output tokens
};

void stats_load();
const Stats &stats();
void stats_on_approval(uint32_t seconds_to_respond);
void stats_on_denial();
void stats_on_bridge_tokens(uint32_t bridge_total);   // heartbeat "tokens" (cumulative since the desktop started)
bool stats_poll_level_up();
void stats_on_nap_end(uint32_t seconds);
void stats_on_wake();                                  // energy back to full
uint16_t stats_median_velocity();
uint8_t stats_mood_tier();     // 0..4
uint8_t stats_energy_tier();   // 0..5
uint8_t stats_fed_progress();  // 0..9 toward the next level
void stats_reset();            // factory reset
// Approvals / denials of today (0) or yesterday (1), for the daily summary (needs the clock).
void stats_day_answers(int day, uint16_t &approved, uint16_t &denied);
