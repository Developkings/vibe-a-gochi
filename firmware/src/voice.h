// Voice conversation with the companion: the mic streams while listening, the companion's
// speech plays on the speaker, and the state drives the voice screen and the talking pet.
#pragma once
#include <stdint.h>

enum class VoiceState : uint8_t { OFF, LISTENING, THINKING, SPEAKING };

void voice_start();     // voice screen opened / push-to-talk pressed
void voice_stop();      // stop button / screen left / push-to-talk released
void voice_set_muted(bool muted);
bool voice_muted();
void voice_on_remote(const char *state);   // companion: listening | thinking | speaking | idle
void voice_tick();
VoiceState voice_state();
uint8_t voice_level();  // mic level while listening, speech level while speaking (0..100)
