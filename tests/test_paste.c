#include "paste.h"

#include <assert.h>

static void assert_released(const MsxMachine *msx) {
    for (unsigned row = 0; row < MSX_KEYBOARD_ROWS; ++row)
        assert(msx_keyboard_read_row(msx, row) == 0xff);
}

static void advance_to_press(Paste *paste, MsxMachine *msx) {
    unsigned attempts = 0;

    while (!paste->held && paste_active(paste)) {
        paste_tick(paste, msx);
        assert(++attempts < 16);
    }
    assert(paste->held);
}

static void advance_to_release(Paste *paste, MsxMachine *msx) {
    unsigned attempts = 0;

    while (paste->held) {
        paste_tick(paste, msx);
        assert(++attempts < 16);
    }
    assert_released(msx);
}

static void test_timing_and_case(void) {
    MsxMachine msx;
    Paste paste;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    paste_init(&paste);
    assert(paste_start(&paste, &msx, "Aa"));

    for (unsigned frame = 0; frame < 3; ++frame) {
        paste_tick(&paste, &msx);
        assert_released(&msx);
    }
    paste_tick(&paste, &msx);
    assert(msx_keyboard_read_row(&msx, 2) == 0xbf);
    assert(msx_keyboard_read_row(&msx, 6) == 0xfe);
    assert(paste.held);

    paste_tick(&paste, &msx);
    paste_tick(&paste, &msx);
    assert(paste.held);
    assert(msx_keyboard_read_row(&msx, 2) == 0xbf);
    paste_tick(&paste, &msx);
    assert(!paste.held);
    assert_released(&msx);

    paste_tick(&paste, &msx);
    assert_released(&msx);
    paste_tick(&paste, &msx);
    assert(paste.held);
    assert(msx_keyboard_read_row(&msx, 2) == 0xbf);
    assert(msx_keyboard_read_row(&msx, 6) == 0xff);

    advance_to_release(&paste, &msx);
    assert(!paste_active(&paste));
    paste_cancel(&paste, &msx);
    msx_destroy(&msx);
}

static void test_international_symbols(void) {
    static const struct {
        const char *text;
        unsigned row;
        unsigned column;
        bool shift;
    } cases[] = {
        { "0", 0, 0, false },
        { "8", 1, 0, false },
        { ")", 0, 0, true },
        { "*", 1, 0, true },
        { "@", 0, 2, true },
        { "^", 0, 6, true },
        { "=", 1, 3, false },
        { "+", 1, 3, true },
        { "\\", 1, 4, false },
        { "|", 1, 4, true },
        { "'", 2, 0, false },
        { "\"", 2, 0, true },
        { "`", 2, 1, false },
        { "~", 2, 1, true },
        { "/", 2, 4, false },
        { "?", 2, 4, true },
    };
    MsxMachine msx;
    Paste paste;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    paste_init(&paste);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(paste_start(&paste, &msx, cases[i].text));
        advance_to_press(&paste, &msx);
        assert(msx_keyboard_read_row(&msx, cases[i].row) ==
               (u8)~(1u << cases[i].column));
        assert(msx_keyboard_read_row(&msx, 6) ==
               (cases[i].shift ? 0xfe : 0xff));
        advance_to_release(&paste, &msx);
        assert(!paste_active(&paste));
    }
    paste_cancel(&paste, &msx);
    msx_destroy(&msx);
}

static void test_line_endings_and_unsupported_text(void) {
    MsxMachine msx;
    Paste paste;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    paste_init(&paste);
    assert(paste_start(&paste, &msx, "\r\n\xff\t"));

    advance_to_press(&paste, &msx);
    assert(msx_keyboard_read_row(&msx, 7) == 0x7f);
    advance_to_release(&paste, &msx);
    advance_to_press(&paste, &msx);
    assert(msx_keyboard_read_row(&msx, 7) == 0xf7);
    advance_to_release(&paste, &msx);
    assert(!paste_active(&paste));

    assert(paste_start(&paste, &msx, ""));
    assert(!paste_active(&paste));
    paste_cancel(&paste, &msx);
    msx_destroy(&msx);
}

static void test_replace_and_cancel_release_keys(void) {
    MsxMachine msx;
    Paste paste;

    msx_init(&msx, MSX_MODEL_GENERIC_MSX1, MSX_REGION_PAL, 64);
    paste_init(&paste);
    assert(paste_start(&paste, &msx, "A"));
    advance_to_press(&paste, &msx);
    assert(msx_keyboard_read_row(&msx, 6) == 0xfe);

    assert(paste_start(&paste, &msx, "b"));
    assert_released(&msx);
    advance_to_press(&paste, &msx);
    assert(msx_keyboard_read_row(&msx, 2) == 0x7f);
    paste_cancel(&paste, &msx);
    assert_released(&msx);
    assert(!paste_active(&paste));
    msx_destroy(&msx);
}

int main(void) {
    test_timing_and_case();
    test_international_symbols();
    test_line_endings_and_unsupported_text();
    test_replace_and_cancel_release_keys();
    return 0;
}
