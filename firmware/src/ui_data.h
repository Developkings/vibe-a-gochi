// Firmware-side UI helpers: global variables, toasts, the on-screen keyboard, the live screens.
#pragma once
#include "ui/screens.h"
#include "ui/vars.h"

// Push a string into one of the UI's global variables (labels bound to it update).
void ui_set_text(FlowGlobalVariables var, const char *text);

// ui_widgets.cpp
void ui_toast(const char *text);
// Full-screen progress (firmware update): pct 0..100; done = checked, restarting (spinning icon).
void ui_progress(const char *title, int pct, const char *note, bool done);
void ui_progress_close();   // short note above the dock, fades by itself
using KeyboardDone = void (*)(const char *text);
using KeyboardCancelled = void (*)();
void ui_keyboard(const char *title, const char *initial, bool password, KeyboardDone done,
                 KeyboardCancelled cancelled = nullptr);   // closed, or nothing typed
void ui_splash();                  // boot greeting: "Developkings awesome" + VIBE-A-GOCHI
// A question with up to 4 choice buttons on top of everything (ask_on_board, AskUserQuestion,
// plans). done(index) gets the tapped choice, -1 when it was closed or replaced by another
// question, n for the "other" button (the asker opens the keyboard), and in multi-select mode
// (multi_done = the confirm button's label) ASK_MULTI | a bit per checked choice.
constexpr int ASK_MULTI = 0x10000;
using AskDone = void (*)(int choice);
void ui_ask(const char *question, const char *const *choices, int n, AskDone done,
            const char *who = nullptr,          // header; nullptr = "Claude asks"
            const char *other = nullptr,        // a last button for a typed answer
            const char *multi_done = nullptr);
void ui_ask_close();               // the asker gave up (timeout, disconnect): no callback
bool ui_ask_active();

// ui_live.cpp
void ui_live_begin();              // after ui_init(): hooks, saved settings into the widgets
void ui_live_tick();               // main loop: labels, lists, auto-navigation
void ui_go(ScreensEnum screen, lv_scr_load_anim_t anim);   // push a screen (no-op if shown)
void ui_back(lv_scr_load_anim_t anim);
void ui_reset_tap(int item);       // Reset screen: tap twice within 3 s
void ui_rebuild_characters();
void ui_live_relayout();           // after the screen turned: rebuild what the firmware sized

// ui_layout.cpp
void ui_orientation_set(uint8_t rotation);   // Settings.rotation: turn the display, move the widgets, save
bool ui_portrait();
int ui_width();                    // the display's current size
int ui_height();
