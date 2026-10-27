// IMA ADPCM, 4 bits per sample (16 kHz speech: 64 kbit/s instead of 256). Each WiFi frame carries
// its own start state, so a lost frame costs only its own 20 ms. companion/adpcm.py is the twin.
//   frame = [type][predictor lo][predictor hi][step index][nibbles, low nibble first]
#pragma once
#include <stddef.h>
#include <stdint.h>

struct AdpcmState {
  int16_t pred = 0;
  uint8_t index = 0;
};

namespace adpcm_detail {
static const int16_t STEP[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

inline int16_t step(AdpcmState &s, uint8_t code) {
  const int st = STEP[s.index];
  int diff = st >> 3;
  if (code & 4) diff += st;
  if (code & 2) diff += st >> 1;
  if (code & 1) diff += st >> 2;
  int p = s.pred + ((code & 8) ? -diff : diff);
  p = p > 32767 ? 32767 : p < -32768 ? -32768 : p;
  s.pred = int16_t(p);
  const int i = s.index + INDEX[code];
  s.index = uint8_t(i < 0 ? 0 : i > 88 ? 88 : i);
  return s.pred;
}

inline uint8_t encode(AdpcmState &s, int16_t sample) {
  const int st = STEP[s.index];
  int diff = sample - s.pred;
  uint8_t code = 0;
  if (diff < 0) {
    code = 8;
    diff = -diff;
  }
  if (diff >= st) code |= 4, diff -= st;
  if (diff >= st >> 1) code |= 2, diff -= st >> 1;
  if (diff >= st >> 2) code |= 1;
  step(s, code);   // track the decoder's view
  return code;
}
}  // namespace adpcm_detail

// n samples (even) → n/2 bytes after a 4-byte header. Returns the frame size.
inline size_t adpcm_encode_frame(uint8_t type, AdpcmState &s, const int16_t *pcm, size_t n, uint8_t *out) {
  out[0] = type;
  out[1] = uint8_t(s.pred & 0xFF);
  out[2] = uint8_t(uint16_t(s.pred) >> 8);
  out[3] = s.index;
  for (size_t i = 0; i + 1 < n; i += 2) {
    const uint8_t lo = adpcm_detail::encode(s, pcm[i]);
    const uint8_t hi = adpcm_detail::encode(s, pcm[i + 1]);
    out[4 + i / 2] = uint8_t(lo | hi << 4);
  }
  return 4 + n / 2;
}

// A frame (with header) → samples. Returns the sample count.
inline size_t adpcm_decode_frame(const uint8_t *in, size_t len, int16_t *pcm) {
  if (len < 4) return 0;
  AdpcmState s;
  s.pred = int16_t(in[1] | in[2] << 8);
  s.index = in[3] > 88 ? 88 : in[3];
  size_t n = 0;
  for (size_t i = 4; i < len; i++) {
    pcm[n++] = adpcm_detail::step(s, in[i] & 0x0F);
    pcm[n++] = adpcm_detail::step(s, in[i] >> 4);
  }
  return n;
}
