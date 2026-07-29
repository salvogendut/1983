#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "config.h"
#include "display.h"
#include "msx.h"

typedef enum {
    OVERLAY_GENERAL = 0,
    OVERLAY_MEDIA,
    OVERLAY_AUDIO,
    OVERLAY_EXTENSIONS,
    OVERLAY_ADVANCED,
    OVERLAY_SECTION_COUNT
} OverlaySection;

typedef enum {
    OVERLAY_STATE_MENU = 0,
    OVERLAY_STATE_CONFIRM
} OverlayState;

typedef struct {
    bool visible;
    bool dirty;
    OverlaySection section;
    OverlayState state;
    int row;

    Config saved;
    Config *config;
    Display *display;
    MsxMachine *msx;

    int dialog_slot;
    char dialog_path[PATH_MAX];
    bool dialog_ready;
    bool dialog_failed;
    char dialog_error[256];
} Overlay;

void overlay_init(Overlay *overlay, Config *config, Display *display,
                  MsxMachine *msx);
bool overlay_handle_event(Overlay *overlay, const SDL_Event *event);
void overlay_tick(Overlay *overlay);
void overlay_render(const Overlay *overlay);
