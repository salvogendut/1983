#include "paste.h"

#include <stdlib.h>
#include <string.h>

#define SHIFT_ROW 6u
#define SHIFT_COLUMN 0u
#define INITIAL_DELAY_FRAMES 3u
#define HOLD_FRAMES 2u
#define GAP_FRAMES 1u

typedef struct {
    unsigned char row;
    unsigned char column;
    bool shift;
    bool valid;
} PasteKey;

/*
 * Printable ASCII positions from openMSX's international Unicode map.
 * Control characters useful in pasted text are included explicitly.
 */
static const PasteKey keymap[128] = {
    ['\b'] = { 7, 5, false, true },
    ['\t'] = { 7, 3, false, true },
    ['\n'] = { 7, 7, false, true },
    [' ']  = { 8, 0, false, true },

    ['0'] = { 0, 0, false, true },
    ['1'] = { 0, 1, false, true },
    ['2'] = { 0, 2, false, true },
    ['3'] = { 0, 3, false, true },
    ['4'] = { 0, 4, false, true },
    ['5'] = { 0, 5, false, true },
    ['6'] = { 0, 6, false, true },
    ['7'] = { 0, 7, false, true },
    ['8'] = { 1, 0, false, true },
    ['9'] = { 1, 1, false, true },

    ['a'] = { 2, 6, false, true },
    ['b'] = { 2, 7, false, true },
    ['c'] = { 3, 0, false, true },
    ['d'] = { 3, 1, false, true },
    ['e'] = { 3, 2, false, true },
    ['f'] = { 3, 3, false, true },
    ['g'] = { 3, 4, false, true },
    ['h'] = { 3, 5, false, true },
    ['i'] = { 3, 6, false, true },
    ['j'] = { 3, 7, false, true },
    ['k'] = { 4, 0, false, true },
    ['l'] = { 4, 1, false, true },
    ['m'] = { 4, 2, false, true },
    ['n'] = { 4, 3, false, true },
    ['o'] = { 4, 4, false, true },
    ['p'] = { 4, 5, false, true },
    ['q'] = { 4, 6, false, true },
    ['r'] = { 4, 7, false, true },
    ['s'] = { 5, 0, false, true },
    ['t'] = { 5, 1, false, true },
    ['u'] = { 5, 2, false, true },
    ['v'] = { 5, 3, false, true },
    ['w'] = { 5, 4, false, true },
    ['x'] = { 5, 5, false, true },
    ['y'] = { 5, 6, false, true },
    ['z'] = { 5, 7, false, true },

    ['A'] = { 2, 6, true, true },
    ['B'] = { 2, 7, true, true },
    ['C'] = { 3, 0, true, true },
    ['D'] = { 3, 1, true, true },
    ['E'] = { 3, 2, true, true },
    ['F'] = { 3, 3, true, true },
    ['G'] = { 3, 4, true, true },
    ['H'] = { 3, 5, true, true },
    ['I'] = { 3, 6, true, true },
    ['J'] = { 3, 7, true, true },
    ['K'] = { 4, 0, true, true },
    ['L'] = { 4, 1, true, true },
    ['M'] = { 4, 2, true, true },
    ['N'] = { 4, 3, true, true },
    ['O'] = { 4, 4, true, true },
    ['P'] = { 4, 5, true, true },
    ['Q'] = { 4, 6, true, true },
    ['R'] = { 4, 7, true, true },
    ['S'] = { 5, 0, true, true },
    ['T'] = { 5, 1, true, true },
    ['U'] = { 5, 2, true, true },
    ['V'] = { 5, 3, true, true },
    ['W'] = { 5, 4, true, true },
    ['X'] = { 5, 5, true, true },
    ['Y'] = { 5, 6, true, true },
    ['Z'] = { 5, 7, true, true },

    ['-']  = { 1, 2, false, true },
    ['=']  = { 1, 3, false, true },
    ['\\'] = { 1, 4, false, true },
    ['[']  = { 1, 5, false, true },
    [']']  = { 1, 6, false, true },
    [';']  = { 1, 7, false, true },
    ['\''] = { 2, 0, false, true },
    ['`']  = { 2, 1, false, true },
    [',']  = { 2, 2, false, true },
    ['.']  = { 2, 3, false, true },
    ['/']  = { 2, 4, false, true },

    [')'] = { 0, 0, true, true },
    ['!'] = { 0, 1, true, true },
    ['@'] = { 0, 2, true, true },
    ['#'] = { 0, 3, true, true },
    ['$'] = { 0, 4, true, true },
    ['%'] = { 0, 5, true, true },
    ['^'] = { 0, 6, true, true },
    ['&'] = { 0, 7, true, true },
    ['*'] = { 1, 0, true, true },
    ['('] = { 1, 1, true, true },
    ['_'] = { 1, 2, true, true },
    ['+'] = { 1, 3, true, true },
    ['|'] = { 1, 4, true, true },
    ['{'] = { 1, 5, true, true },
    ['}'] = { 1, 6, true, true },
    [':'] = { 1, 7, true, true },
    ['"'] = { 2, 0, true, true },
    ['~'] = { 2, 1, true, true },
    ['<'] = { 2, 2, true, true },
    ['>'] = { 2, 3, true, true },
    ['?'] = { 2, 4, true, true },
};

static void release_held(Paste *paste, MsxMachine *msx) {
    if (!paste->held)
        return;
    msx_keyboard_release(msx, paste->held_row, paste->held_column);
    if (paste->held_shift)
        msx_keyboard_release(msx, SHIFT_ROW, SHIFT_COLUMN);
    paste->held = false;
    paste->held_shift = false;
}

static void clear_text(Paste *paste) {
    free(paste->text);
    paste->text = NULL;
    paste->length = 0;
    paste->offset = 0;
    paste->timer = 0;
}

void paste_init(Paste *paste) {
    if (paste)
        memset(paste, 0, sizeof(*paste));
}

void paste_cancel(Paste *paste, MsxMachine *msx) {
    if (!paste)
        return;
    if (msx)
        release_held(paste, msx);
    clear_text(paste);
    paste->held = false;
    paste->held_shift = false;
}

bool paste_start(Paste *paste, MsxMachine *msx, const char *text) {
    char *copy;
    size_t length;

    if (!paste || !msx || !text)
        return false;
    length = strlen(text);
    copy = malloc(length + 1);
    if (!copy)
        return false;
    memcpy(copy, text, length + 1);

    paste_cancel(paste, msx);
    paste->text = copy;
    paste->length = length;
    paste->timer = INITIAL_DELAY_FRAMES;
    return true;
}

bool paste_active(const Paste *paste) {
    return paste && paste->text && paste->offset < paste->length;
}

void paste_tick(Paste *paste, MsxMachine *msx) {
    const PasteKey *key;
    unsigned char character;

    if (!paste || !msx || !paste_active(paste))
        return;
    if (paste->timer) {
        --paste->timer;
        return;
    }
    if (paste->held) {
        release_held(paste, msx);
        ++paste->offset;
        if (!paste_active(paste)) {
            clear_text(paste);
            return;
        }
        paste->timer = GAP_FRAMES;
        return;
    }

    while (paste_active(paste)) {
        character = (unsigned char)paste->text[paste->offset];
        if (character < 128 && keymap[character].valid)
            break;
        ++paste->offset;
    }
    if (!paste_active(paste)) {
        clear_text(paste);
        return;
    }

    key = &keymap[(unsigned char)paste->text[paste->offset]];
    if (key->shift)
        msx_keyboard_press(msx, SHIFT_ROW, SHIFT_COLUMN);
    msx_keyboard_press(msx, key->row, key->column);
    paste->held_row = key->row;
    paste->held_column = key->column;
    paste->held_shift = key->shift;
    paste->held = true;
    paste->timer = HOLD_FRAMES;
}
