#pragma once

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "msx.h"

/*
 * Host keyboard adapter for the standard international MSX matrix.
 *
 * The machine-facing matrix remains in MsxMachine so the hardware core does
 * not depend on SDL. This adapter tracks physical host scancodes, translates
 * them to matrix positions, and prevents aliases such as both Shift keys from
 * releasing a matrix bit while another host key for that position is held.
 */
typedef struct {
    bool down[SDL_SCANCODE_COUNT];
    bool guest_special[SDL_SCANCODE_COUNT];
    unsigned guest_special_count;
} KbdHost;

void kbd_init(KbdHost *host);
void kbd_release_all(KbdHost *host, MsxMachine *msx);

/*
 * Handle Shift+F1..F5 as the MSX function keys, Shift+F7 as SELECT,
 * and Shift+F8 as STOP. This is separate so the main loop can give these
 * guest chords priority before dispatching unmodified function keys to the
 * emulator frontend.
 */
bool kbd_handle_guest_function(KbdHost *host, MsxMachine *msx,
                               const SDL_KeyboardEvent *event);

/* Translate a regular key event, or release a guest function-key chord. */
bool kbd_handle(KbdHost *host, MsxMachine *msx,
                const SDL_KeyboardEvent *event);
