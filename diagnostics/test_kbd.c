#include "kbd.h"

#include <assert.h>
#include <string.h>

static const SDL_Scancode international_matrix
    [MSX_KEYBOARD_ROWS][MSX_KEYBOARD_COLUMNS] = {
    {
        SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2,
        SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
        SDL_SCANCODE_6, SDL_SCANCODE_7,
    },
    {
        SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_MINUS,
        SDL_SCANCODE_EQUALS, SDL_SCANCODE_BACKSLASH,
        SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
        SDL_SCANCODE_SEMICOLON,
    },
    {
        SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE,
        SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
        SDL_SCANCODE_RCTRL, SDL_SCANCODE_A, SDL_SCANCODE_B,
    },
    {
        SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E,
        SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
        SDL_SCANCODE_I, SDL_SCANCODE_J,
    },
    {
        SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_M,
        SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P,
        SDL_SCANCODE_Q, SDL_SCANCODE_R,
    },
    {
        SDL_SCANCODE_S, SDL_SCANCODE_T, SDL_SCANCODE_U,
        SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
        SDL_SCANCODE_Y, SDL_SCANCODE_Z,
    },
    {
        SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL, SDL_SCANCODE_LALT,
        SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_RALT, SDL_SCANCODE_F1,
        SDL_SCANCODE_F2, SDL_SCANCODE_F3,
    },
    {
        SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_ESCAPE,
        SDL_SCANCODE_TAB, SDL_SCANCODE_F8, SDL_SCANCODE_BACKSPACE,
        SDL_SCANCODE_F7, SDL_SCANCODE_RETURN,
    },
    {
        SDL_SCANCODE_SPACE, SDL_SCANCODE_HOME, SDL_SCANCODE_INSERT,
        SDL_SCANCODE_DELETE, SDL_SCANCODE_LEFT, SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT,
    },
    {
        SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_KP_PLUS,
        SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_KP_0,
        SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2,
        SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4,
    },
    {
        SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7,
        SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9,
        SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_COMMA,
        SDL_SCANCODE_KP_PERIOD,
    },
};

static bool is_guest_function(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_F1:
        case SDL_SCANCODE_F2:
        case SDL_SCANCODE_F3:
        case SDL_SCANCODE_F4:
        case SDL_SCANCODE_F5:
        case SDL_SCANCODE_F7:
        case SDL_SCANCODE_F8:
            return true;
        default:
            return false;
    }
}

static bool send_key(KbdHost *host, MsxMachine *msx,
                     SDL_Scancode scancode, bool down, SDL_Keymod mod) {
    SDL_KeyboardEvent event;

    memset(&event, 0, sizeof(event));
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.down = down;
    event.scancode = scancode;
    event.mod = mod;
    return kbd_handle(host, msx, &event);
}

static bool send_repeat_key_down(KbdHost *host, MsxMachine *msx,
                                 SDL_Scancode scancode, SDL_Keymod mod) {
    SDL_KeyboardEvent event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.down = true;
    event.repeat = true;
    event.scancode = scancode;
    event.mod = mod;
    return kbd_handle(host, msx, &event);
}

static void test_all_matrix_positions(void) {
    MsxMachine msx;
    KbdHost host;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    for (unsigned row = 0; row < MSX_KEYBOARD_ROWS; ++row) {
        for (unsigned column = 0; column < MSX_KEYBOARD_COLUMNS; ++column) {
            SDL_Scancode scancode = international_matrix[row][column];
            SDL_Keymod mod = is_guest_function(scancode)
                           ? SDL_KMOD_SHIFT : SDL_KMOD_NONE;

            kbd_release_all(&host, &msx);
            kbd_init(&host);
            assert(send_key(&host, &msx, scancode, true, mod));
            for (unsigned check = 0; check < MSX_KEYBOARD_ROWS; ++check) {
                u8 expected = check == row
                            ? (u8)~(1u << column) : 0xff;
                assert(msx_keyboard_read_row(&msx, check) == expected);
            }
            assert(send_key(&host, &msx, scancode, false, SDL_KMOD_NONE));
            assert(msx_keyboard_read_row(&msx, row) == 0xff);
        }
    }
}

static void test_rollover_and_aliases(void) {
    MsxMachine msx;
    KbdHost host;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    kbd_init(&host);

    assert(send_key(&host, &msx, SDL_SCANCODE_A, true, SDL_KMOD_NONE));
    assert(send_key(&host, &msx, SDL_SCANCODE_B, true, SDL_KMOD_NONE));
    assert(send_key(&host, &msx, SDL_SCANCODE_C, true, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 2) == 0x3f);
    assert(msx_keyboard_read_row(&msx, 3) == 0xfe);
    assert(send_key(&host, &msx, SDL_SCANCODE_A, true, SDL_KMOD_NONE));
    assert(send_key(&host, &msx, SDL_SCANCODE_A, false, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 2) == 0x7f);

    assert(send_key(&host, &msx, SDL_SCANCODE_LSHIFT,
                    true, SDL_KMOD_SHIFT));
    assert(send_key(&host, &msx, SDL_SCANCODE_RSHIFT,
                    true, SDL_KMOD_SHIFT));
    assert(msx_keyboard_read_row(&msx, 6) == 0xfe);
    assert(send_key(&host, &msx, SDL_SCANCODE_LSHIFT,
                    false, SDL_KMOD_SHIFT));
    assert(msx_keyboard_read_row(&msx, 6) == 0xfe);
    assert(send_key(&host, &msx, SDL_SCANCODE_RSHIFT,
                    false, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 6) == 0xff);

    assert(send_key(&host, &msx, SDL_SCANCODE_BACKSLASH,
                    true, SDL_KMOD_NONE));
    assert(send_key(&host, &msx, SDL_SCANCODE_INTERNATIONAL3,
                    true, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 1) == 0xef);
    assert(send_key(&host, &msx, SDL_SCANCODE_BACKSLASH,
                    false, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 1) == 0xef);
    assert(send_key(&host, &msx, SDL_SCANCODE_INTERNATIONAL3,
                    false, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 1) == 0xff);

    assert(send_key(&host, &msx, SDL_SCANCODE_KP_ENTER,
                    true, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 10) == 0xbf);
    assert(send_key(&host, &msx, SDL_SCANCODE_KP_ENTER,
                    false, SDL_KMOD_NONE));
}

static void test_shifted_guest_functions(void) {
    MsxMachine msx;
    KbdHost host;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    kbd_init(&host);

    /* Unmodified function keys remain frontend shortcuts. */
    assert(!send_key(&host, &msx, SDL_SCANCODE_F4,
                     true, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 7) == 0xff);

    assert(send_key(&host, &msx, SDL_SCANCODE_LSHIFT,
                    true, SDL_KMOD_SHIFT));
    assert(msx_keyboard_read_row(&msx, 6) == 0xfe);
    assert(send_key(&host, &msx, SDL_SCANCODE_F4,
                    true, SDL_KMOD_SHIFT));
    assert(msx_keyboard_read_row(&msx, 6) == 0xff);
    assert(msx_keyboard_read_row(&msx, 7) == 0xfe);

    /* Repeated host key-down events must not unbalance matrix references. */
    assert(send_key(&host, &msx, SDL_SCANCODE_F4,
                    true, SDL_KMOD_SHIFT));
    assert(send_key(&host, &msx, SDL_SCANCODE_F4,
                    false, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 7) == 0xff);
    assert(msx_keyboard_read_row(&msx, 6) == 0xfe);
    assert(send_key(&host, &msx, SDL_SCANCODE_LSHIFT,
                    false, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 6) == 0xff);

    assert(send_key(&host, &msx, SDL_SCANCODE_F7,
                    true, SDL_KMOD_SHIFT));
    assert(msx_keyboard_read_row(&msx, 7) == 0xbf);
    assert(send_key(&host, &msx, SDL_SCANCODE_F7,
                    false, SDL_KMOD_NONE));
    assert(send_key(&host, &msx, SDL_SCANCODE_F8,
                    true, SDL_KMOD_SHIFT));
    assert(msx_keyboard_read_row(&msx, 7) == 0xef);
    assert(send_key(&host, &msx, SDL_SCANCODE_F8,
                    false, SDL_KMOD_NONE));
}

static void test_focus_cleanup_and_ppi_reads(void) {
    MsxMachine msx;
    KbdHost host;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    kbd_init(&host);
    assert(send_key(&host, &msx, SDL_SCANCODE_0,
                    true, SDL_KMOD_NONE));
    assert(send_key(&host, &msx, SDL_SCANCODE_7,
                    true, SDL_KMOD_NONE));
    msx_io_write(&msx, 0xaa, 0xf0);
    assert(msx_io_read(&msx, 0xa9) == 0x7e);

    kbd_release_all(&host, &msx);
    assert(msx_io_read(&msx, 0xa9) == 0xff);
    for (unsigned row = 0; row < MSX_KEYBOARD_ROWS; ++row)
        assert(msx_keyboard_read_row(&msx, row) == 0xff);
}

static void test_auto_repeat_is_ignored(void) {
    MsxMachine msx;
    KbdHost host;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    kbd_init(&host);

    /* Repeats of a held key are harmless no-ops. */
    assert(send_key(&host, &msx, SDL_SCANCODE_A, true, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 2) == 0xbf);
    assert(send_repeat_key_down(&host, &msx, SDL_SCANCODE_A,
                                SDL_KMOD_NONE));
    assert(send_repeat_key_down(&host, &msx, SDL_SCANCODE_A,
                                SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 2) == 0xbf);
    assert(send_key(&host, &msx, SDL_SCANCODE_A, false, SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 2) == 0xff);

    /*
     * The Ctrl+V paste shortcut releases the whole chord through
     * kbd_release_all. Repeats of the still-held physical keys must not
     * leak back into the guest matrix while the paste queue is typing.
     */
    assert(send_key(&host, &msx, SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL));
    assert(send_key(&host, &msx, SDL_SCANCODE_V, true, SDL_KMOD_CTRL));
    assert(msx_keyboard_read_row(&msx, 6) == 0xfd);
    assert(msx_keyboard_read_row(&msx, 5) == 0xf7);
    kbd_release_all(&host, &msx);
    assert(send_repeat_key_down(&host, &msx, SDL_SCANCODE_LCTRL,
                                SDL_KMOD_CTRL));
    assert(send_repeat_key_down(&host, &msx, SDL_SCANCODE_V,
                                SDL_KMOD_CTRL));
    assert(msx_keyboard_read_row(&msx, 6) == 0xff);
    assert(msx_keyboard_read_row(&msx, 5) == 0xff);
    assert(send_key(&host, &msx, SDL_SCANCODE_V, false, SDL_KMOD_CTRL));
    assert(send_key(&host, &msx, SDL_SCANCODE_LCTRL, false,
                    SDL_KMOD_NONE));
    assert(msx_keyboard_read_row(&msx, 6) == 0xff);
    assert(msx_keyboard_read_row(&msx, 5) == 0xff);
    msx_destroy(&msx);
}

int main(void) {
    test_all_matrix_positions();
    test_rollover_and_aliases();
    test_shifted_guest_functions();
    test_focus_cleanup_and_ppi_reads();
    test_auto_repeat_is_ignored();
    return 0;
}
