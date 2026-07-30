#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "msx.h"

/*
 * Host-independent text paste queue. Characters are translated through the
 * international MSX keyboard matrix and held for complete emulated frames so
 * firmware sees the same transitions as it would from a physical keyboard.
 */
typedef struct {
    char *text;
    size_t length;
    size_t offset;
    unsigned timer;
    unsigned held_row;
    unsigned held_column;
    bool held;
    bool held_shift;
} Paste;

void paste_init(Paste *paste);
void paste_cancel(Paste *paste, MsxMachine *msx);

/*
 * Replace any pending paste with text. The clipboard contents are reproduced
 * verbatim: no implicit Return is appended. Returns false on allocation
 * failure and leaves the existing queue untouched.
 */
bool paste_start(Paste *paste, MsxMachine *msx, const char *text);
bool paste_active(const Paste *paste);

/* Advance the queue once immediately before an emulated video frame. */
void paste_tick(Paste *paste, MsxMachine *msx);
