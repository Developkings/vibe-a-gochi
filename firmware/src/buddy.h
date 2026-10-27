// The pet's mood and the approval flow, ported from the reference main loop:
// derive() from the heartbeat, one-shot states (level-up, quick approval, petting),
// the 12 s sleepy hold after waking, the clock-face moods, and the status LED.
#pragma once
#include <stdint.h>

void buddy_begin();
void buddy_tick();                 // after link_poll()
uint8_t buddy_state();             // PetState for the pet on the current screen
void buddy_one_shot(uint8_t state, uint32_t ms);

bool buddy_in_prompt();            // a permission request is waiting for an answer
bool buddy_prompt_arrived();       // true once per new request (UI jumps to the approval screen)
uint32_t buddy_prompt_age_s();
void buddy_answer(bool approve, bool always = false);   // send, count, sounds, heart one-shot when quick
