#include "voice.h"
#include <Arduino.h>
#include "adpcm.h"
#include "companion.h"
#include "sound.h"

namespace {

volatile VoiceState state = VoiceState::OFF;
volatile bool muted = false;
bool session = false;   // voice screen open / push-to-talk held
uint32_t quiet_since = 0;
AdpcmState enc;

// Mic task: 20 ms blocks → ADPCM → companion. Silent while muted or while the pet speaks (no echo).
void mic_sink(const int16_t *pcm, size_t n, uint8_t) {
  if (muted || state != VoiceState::LISTENING || sound_stream_active()) return;
  uint8_t frame[4 + 320 / 2];
  const size_t len = adpcm_encode_frame(0x01, enc, pcm, min<size_t>(n, 320), frame);
  companion_send_audio(frame, len);
}

void mic(bool on) { sound_mic(on, on ? mic_sink : nullptr); }

}  // namespace

void voice_start() {
  if (!companion_connected()) {
    state = VoiceState::OFF;
    return;
  }
  session = true;
  enc = AdpcmState{};
  sound_stream_stop();
  companion_command("voice_start");
  state = VoiceState::LISTENING;
  mic(true);
}

void voice_stop() {
  if (state == VoiceState::LISTENING) companion_command("voice_stop");   // ends the utterance now
  session = false;
  mic(false);
  state = VoiceState::OFF;
}

void voice_set_muted(bool m) { muted = m; }
bool voice_muted() { return muted; }

void voice_on_remote(const char *s) {
  if (!session && strcmp(s, "speaking")) {   // outside a conversation only read-aloud replies count
    state = VoiceState::OFF;
    return;
  }
  if (!strcmp(s, "listening")) state = VoiceState::LISTENING;
  else if (!strcmp(s, "thinking")) state = VoiceState::THINKING;
  else if (!strcmp(s, "speaking")) state = VoiceState::SPEAKING, quiet_since = millis();
  else state = session ? VoiceState::LISTENING : VoiceState::OFF;
  mic(session && state == VoiceState::LISTENING);
}

void voice_tick() {
  if (!companion_connected() && state != VoiceState::OFF) {
    mic(false);
    session = false;
    state = VoiceState::OFF;
  }
  // The companion says "speaking" before the audio; when the speaker runs dry, listen again.
  if (state == VoiceState::SPEAKING) {
    if (sound_stream_active()) quiet_since = millis();
    else if (millis() - quiet_since > 1500) voice_on_remote(session ? "listening" : "idle");
  }
}

VoiceState voice_state() {
  if (state != VoiceState::SPEAKING && sound_stream_active()) return VoiceState::SPEAKING;   // read-aloud reply
  return state;
}

uint8_t voice_level() { return voice_state() == VoiceState::SPEAKING ? sound_stream_level() : sound_mic_level(); }
