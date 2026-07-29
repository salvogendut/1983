#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "config.h"
#include "display.h"
#include "models.h"
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
    OVERLAY_STATE_CONFIRM,
    OVERLAY_STATE_MACHINE
} OverlayState;

typedef enum {
    OVERLAY_DIALOG_NONE = 0,
    OVERLAY_DIALOG_CARTRIDGE_1,
    OVERLAY_DIALOG_CARTRIDGE_2,
    OVERLAY_DIALOG_BIOS,
    OVERLAY_DIALOG_LOGO,
    OVERLAY_DIALOG_SUBROM,
    OVERLAY_DIALOG_DISK_ROM
} OverlayDialogTarget;

typedef struct {
    bool visible;
    bool dirty;
    OverlaySection section;
    OverlayState state;
    int row;

    Config saved;
    Config *config;
    const ModelCatalog *models;
    Display *display;
    MsxMachine *msx;

    OverlayDialogTarget dialog_target;
    char dialog_path[PATH_MAX];
    bool dialog_ready;
    bool dialog_failed;
    char dialog_error[256];

    int machine_row;
    size_t pending_model_index;
    MsxModel pending_model;
    char pending_bios_path[PATH_MAX];
    char pending_logo_path[PATH_MAX];
    char pending_subrom_path[PATH_MAX];
    char pending_disk_rom_path[PATH_MAX];
    char pending_firmware_dir[PATH_MAX];
} Overlay;

void overlay_init(Overlay *overlay, Config *config,
                  const ModelCatalog *models, Display *display,
                  MsxMachine *msx);
bool overlay_handle_event(Overlay *overlay, const SDL_Event *event);
void overlay_tick(Overlay *overlay);
void overlay_render(const Overlay *overlay);
