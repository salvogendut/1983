#include "kbd.h"

#include <string.h>

typedef struct {
    int row;
    int column;
} MatrixPosition;

static MatrixPosition no_position(void) {
    return (MatrixPosition){ -1, -1 };
}

/*
 * Standard international MSX keyboard matrix, cross-checked against
 * openMSX 21.0 src/input/Keyboard.cc. Rows and columns are active-low.
 */
static MatrixPosition regular_position(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_0: return (MatrixPosition){ 0, 0 };
        case SDL_SCANCODE_1: return (MatrixPosition){ 0, 1 };
        case SDL_SCANCODE_2: return (MatrixPosition){ 0, 2 };
        case SDL_SCANCODE_3: return (MatrixPosition){ 0, 3 };
        case SDL_SCANCODE_4: return (MatrixPosition){ 0, 4 };
        case SDL_SCANCODE_5: return (MatrixPosition){ 0, 5 };
        case SDL_SCANCODE_6: return (MatrixPosition){ 0, 6 };
        case SDL_SCANCODE_7: return (MatrixPosition){ 0, 7 };

        case SDL_SCANCODE_8:            return (MatrixPosition){ 1, 0 };
        case SDL_SCANCODE_9:            return (MatrixPosition){ 1, 1 };
        case SDL_SCANCODE_MINUS:        return (MatrixPosition){ 1, 2 };
        case SDL_SCANCODE_EQUALS:       return (MatrixPosition){ 1, 3 };
        case SDL_SCANCODE_BACKSLASH:
        case SDL_SCANCODE_INTERNATIONAL3:
            return (MatrixPosition){ 1, 4 };
        case SDL_SCANCODE_LEFTBRACKET:  return (MatrixPosition){ 1, 5 };
        case SDL_SCANCODE_RIGHTBRACKET: return (MatrixPosition){ 1, 6 };
        case SDL_SCANCODE_SEMICOLON:    return (MatrixPosition){ 1, 7 };

        case SDL_SCANCODE_APOSTROPHE:     return (MatrixPosition){ 2, 0 };
        case SDL_SCANCODE_GRAVE:          return (MatrixPosition){ 2, 1 };
        case SDL_SCANCODE_COMMA:          return (MatrixPosition){ 2, 2 };
        case SDL_SCANCODE_PERIOD:         return (MatrixPosition){ 2, 3 };
        case SDL_SCANCODE_SLASH:          return (MatrixPosition){ 2, 4 };
        case SDL_SCANCODE_RCTRL:
        case SDL_SCANCODE_NONUSBACKSLASH:
            return (MatrixPosition){ 2, 5 }; /* ACC/dead key */
        case SDL_SCANCODE_A:              return (MatrixPosition){ 2, 6 };
        case SDL_SCANCODE_B:              return (MatrixPosition){ 2, 7 };

        case SDL_SCANCODE_C: return (MatrixPosition){ 3, 0 };
        case SDL_SCANCODE_D: return (MatrixPosition){ 3, 1 };
        case SDL_SCANCODE_E: return (MatrixPosition){ 3, 2 };
        case SDL_SCANCODE_F: return (MatrixPosition){ 3, 3 };
        case SDL_SCANCODE_G: return (MatrixPosition){ 3, 4 };
        case SDL_SCANCODE_H: return (MatrixPosition){ 3, 5 };
        case SDL_SCANCODE_I: return (MatrixPosition){ 3, 6 };
        case SDL_SCANCODE_J: return (MatrixPosition){ 3, 7 };

        case SDL_SCANCODE_K: return (MatrixPosition){ 4, 0 };
        case SDL_SCANCODE_L: return (MatrixPosition){ 4, 1 };
        case SDL_SCANCODE_M: return (MatrixPosition){ 4, 2 };
        case SDL_SCANCODE_N: return (MatrixPosition){ 4, 3 };
        case SDL_SCANCODE_O: return (MatrixPosition){ 4, 4 };
        case SDL_SCANCODE_P: return (MatrixPosition){ 4, 5 };
        case SDL_SCANCODE_Q: return (MatrixPosition){ 4, 6 };
        case SDL_SCANCODE_R: return (MatrixPosition){ 4, 7 };

        case SDL_SCANCODE_S: return (MatrixPosition){ 5, 0 };
        case SDL_SCANCODE_T: return (MatrixPosition){ 5, 1 };
        case SDL_SCANCODE_U: return (MatrixPosition){ 5, 2 };
        case SDL_SCANCODE_V: return (MatrixPosition){ 5, 3 };
        case SDL_SCANCODE_W: return (MatrixPosition){ 5, 4 };
        case SDL_SCANCODE_X: return (MatrixPosition){ 5, 5 };
        case SDL_SCANCODE_Y: return (MatrixPosition){ 5, 6 };
        case SDL_SCANCODE_Z: return (MatrixPosition){ 5, 7 };

        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:  return (MatrixPosition){ 6, 0 };
        case SDL_SCANCODE_LCTRL:   return (MatrixPosition){ 6, 1 };
        case SDL_SCANCODE_LALT:    return (MatrixPosition){ 6, 2 }; /* GRAPH */
        case SDL_SCANCODE_CAPSLOCK:return (MatrixPosition){ 6, 3 };
        case SDL_SCANCODE_RALT:    return (MatrixPosition){ 6, 4 }; /* CODE */

        case SDL_SCANCODE_ESCAPE:    return (MatrixPosition){ 7, 2 };
        case SDL_SCANCODE_TAB:       return (MatrixPosition){ 7, 3 };
        case SDL_SCANCODE_BACKSPACE: return (MatrixPosition){ 7, 5 };
        case SDL_SCANCODE_RETURN:    return (MatrixPosition){ 7, 7 };

        case SDL_SCANCODE_SPACE:  return (MatrixPosition){ 8, 0 };
        case SDL_SCANCODE_HOME:   return (MatrixPosition){ 8, 1 };
        case SDL_SCANCODE_INSERT: return (MatrixPosition){ 8, 2 };
        case SDL_SCANCODE_DELETE: return (MatrixPosition){ 8, 3 };
        case SDL_SCANCODE_LEFT:   return (MatrixPosition){ 8, 4 };
        case SDL_SCANCODE_UP:     return (MatrixPosition){ 8, 5 };
        case SDL_SCANCODE_DOWN:   return (MatrixPosition){ 8, 6 };
        case SDL_SCANCODE_RIGHT:  return (MatrixPosition){ 8, 7 };

        case SDL_SCANCODE_KP_MULTIPLY:return (MatrixPosition){ 9, 0 };
        case SDL_SCANCODE_KP_PLUS:    return (MatrixPosition){ 9, 1 };
        case SDL_SCANCODE_KP_DIVIDE:  return (MatrixPosition){ 9, 2 };
        case SDL_SCANCODE_KP_0:       return (MatrixPosition){ 9, 3 };
        case SDL_SCANCODE_KP_1:       return (MatrixPosition){ 9, 4 };
        case SDL_SCANCODE_KP_2:       return (MatrixPosition){ 9, 5 };
        case SDL_SCANCODE_KP_3:       return (MatrixPosition){ 9, 6 };
        case SDL_SCANCODE_KP_4:       return (MatrixPosition){ 9, 7 };

        case SDL_SCANCODE_KP_5:     return (MatrixPosition){ 10, 0 };
        case SDL_SCANCODE_KP_6:     return (MatrixPosition){ 10, 1 };
        case SDL_SCANCODE_KP_7:     return (MatrixPosition){ 10, 2 };
        case SDL_SCANCODE_KP_8:     return (MatrixPosition){ 10, 3 };
        case SDL_SCANCODE_KP_9:     return (MatrixPosition){ 10, 4 };
        case SDL_SCANCODE_KP_MINUS: return (MatrixPosition){ 10, 5 };
        case SDL_SCANCODE_KP_COMMA:
        case SDL_SCANCODE_KP_ENTER:
            return (MatrixPosition){ 10, 6 };
        case SDL_SCANCODE_KP_PERIOD:return (MatrixPosition){ 10, 7 };

        default:
            return no_position();
    }
}

static MatrixPosition guest_function_position(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_F1: return (MatrixPosition){ 6, 5 };
        case SDL_SCANCODE_F2: return (MatrixPosition){ 6, 6 };
        case SDL_SCANCODE_F3: return (MatrixPosition){ 6, 7 };
        case SDL_SCANCODE_F4: return (MatrixPosition){ 7, 0 };
        case SDL_SCANCODE_F5: return (MatrixPosition){ 7, 1 };
        case SDL_SCANCODE_F7: return (MatrixPosition){ 7, 6 }; /* SELECT */
        case SDL_SCANCODE_F8: return (MatrixPosition){ 7, 4 }; /* STOP */
        default:
            return no_position();
    }
}

static bool valid_scancode(SDL_Scancode scancode) {
    return scancode > SDL_SCANCODE_UNKNOWN &&
           scancode < SDL_SCANCODE_COUNT;
}

static void suppress_guest_shift(KbdHost *host, MsxMachine *msx) {
    if (host->down[SDL_SCANCODE_LSHIFT])
        msx_keyboard_release(msx, 6, 0);
    if (host->down[SDL_SCANCODE_RSHIFT])
        msx_keyboard_release(msx, 6, 0);
}

static void restore_guest_shift(KbdHost *host, MsxMachine *msx) {
    if (host->down[SDL_SCANCODE_LSHIFT])
        msx_keyboard_press(msx, 6, 0);
    if (host->down[SDL_SCANCODE_RSHIFT])
        msx_keyboard_press(msx, 6, 0);
}

void kbd_init(KbdHost *host) {
    if (host)
        memset(host, 0, sizeof(*host));
}

void kbd_release_all(KbdHost *host, MsxMachine *msx) {
    if (host)
        memset(host, 0, sizeof(*host));
    msx_keyboard_clear(msx);
}

bool kbd_handle_guest_function(KbdHost *host, MsxMachine *msx,
                               const SDL_KeyboardEvent *event) {
    MatrixPosition position;
    SDL_Scancode scancode;

    if (!host || !msx || !event)
        return false;
    scancode = event->scancode;
    if (!valid_scancode(scancode))
        return false;
    position = guest_function_position(scancode);
    if (position.row < 0)
        return false;

    if (event->down) {
        if (!(event->mod & SDL_KMOD_SHIFT))
            return false;
        if (host->guest_special[scancode])
            return true;
        if (!host->guest_special_count)
            suppress_guest_shift(host, msx);
        host->down[scancode] = true;
        host->guest_special[scancode] = true;
        ++host->guest_special_count;
        msx_keyboard_press(msx, (unsigned)position.row,
                           (unsigned)position.column);
        return true;
    }

    host->down[scancode] = false;
    if (!host->guest_special[scancode])
        return false;
    msx_keyboard_release(msx, (unsigned)position.row,
                         (unsigned)position.column);
    host->guest_special[scancode] = false;
    if (host->guest_special_count)
        --host->guest_special_count;
    if (!host->guest_special_count)
        restore_guest_shift(host, msx);
    return true;
}

bool kbd_handle(KbdHost *host, MsxMachine *msx,
                const SDL_KeyboardEvent *event) {
    MatrixPosition position;
    SDL_Scancode scancode;

    if (!host || !msx || !event)
        return false;
    if (kbd_handle_guest_function(host, msx, event))
        return true;

    scancode = event->scancode;
    if (!valid_scancode(scancode))
        return false;
    if (guest_function_position(scancode).row >= 0)
        return false;
    position = regular_position(scancode);
    if (position.row < 0)
        return false;

    /*
     * Ignore key auto-repeat: host->down already tracks the physical hold.
     * After kbd_release_all (e.g. the released Ctrl+V chord), a repeated
     * key-down would otherwise be mistaken for a fresh press and leak the
     * still-held key back into the guest matrix while a paste is typing.
     */
    if (event->repeat)
        return true;

    if (host->down[scancode] == event->down)
        return true;
    host->down[scancode] = event->down;

    if ((scancode == SDL_SCANCODE_LSHIFT ||
         scancode == SDL_SCANCODE_RSHIFT) &&
        host->guest_special_count)
        return true;

    if (event->down)
        msx_keyboard_press(msx, (unsigned)position.row,
                           (unsigned)position.column);
    else
        msx_keyboard_release(msx, (unsigned)position.row,
                             (unsigned)position.column);
    return true;
}
