// Audio through the ES8311 codec: event sounds (synthesized from sound_defs.h, see
// ui/tools/gen_sounds.py), the companion's speech on the speaker, and the microphone.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sound_defs.h"

void sound_begin();                 // codec, I2S, player + mic tasks; loads settings from NVS
void sound_play(Sound s);           // non-blocking; ignored while sound is off
void sound_set_enabled(bool on);    // "Sound effects" switch, saved in NVS
bool sound_enabled();
void sound_set_volume(int pct, bool save = true);  // 0..100; save=false while a slider is dragged
int sound_volume();

// Speech from the companion: 16 kHz mono PCM, buffered ~2 s, starts after a short jitter buffer.
void sound_stream_write(const int16_t *pcm, size_t n);   // any task
void sound_stream_stop();                                // drop what is buffered
bool sound_stream_active();                              // playing, or data in the last 300 ms
uint8_t sound_stream_level();                            // 0..100, for the talking pet
void sound_set_stream_volume(int pct);                   // Headset → Speaker

// Microphone: 20 ms blocks of 16 kHz mono PCM to the sink, from the mic task.
using MicSink = void (*)(const int16_t *pcm, size_t n, uint8_t level);
void sound_mic(bool on, MicSink sink);
uint8_t sound_mic_level();   // 0..100
