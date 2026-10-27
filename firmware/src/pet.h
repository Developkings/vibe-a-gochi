// The pet: catalog of characters (18 built-in ASCII species + GIF packs in LittleFS
// /characters/<name>), the selected one, and live drawing into the screen that shows it.
#pragma once
#include <lvgl.h>
#include <stdint.h>

// Persona states. The first 7 are the Hardware Buddy states (manifest names sleep..heart);
// TALK and LISTEN are optional extra states of GIF packs, used by the voice screen.
enum PetState : uint8_t { P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART, P_TALK, P_LISTEN, P_COUNT };

constexpr uint8_t SPECIES_GIF = 0xFF;   // protocol species index meaning "the installed GIF character"

void pet_begin();                 // mount LittleFS, scan packs, restore the saved selection
void pet_tick(uint8_t state);     // draw the current frame into the slot of the active screen
void pet_rescan();                // after an install or delete

int pet_count();
const char *pet_entry_name(int i);
bool pet_entry_is_gif(int i);
int pet_selected();
void pet_select(int i);           // saved
void pet_select_species(uint8_t idx);   // species command: 0..17 or SPECIES_GIF
const char *pet_select_pack(const char *name);   // select a pack by name; returns its name or nullptr
uint8_t pet_species_index();      // for the status/species protocol: 0..17 or SPECIES_GIF
const char *pet_species_name();   // "cat", "bufo", ...

// Thumbnail of entry i (idle pose), drawn once and cached; fits 80x56.
const lv_image_dsc_t *pet_thumb(int i);

// Install helpers (xfer.cpp, SD import): delete every pack, or close files before writing.
void pet_close();
void pet_delete_all_packs();
