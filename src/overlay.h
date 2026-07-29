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
    OVERLAY_EXTENSIONS,
    OVERLAY_ADVANCED,
    OVERLAY_SECTION_COUNT
} OverlaySection;

typedef enum {
    OVERLAY_STATE_MENU = 0,
    OVERLAY_STATE_CONFIRM,
    OVERLAY_STATE_MACHINE,
    OVERLAY_STATE_SUNRISE_SETUP,
    OVERLAY_STATE_SD_MAPPER_SETUP,
    OVERLAY_STATE_MODEL_LIST,
    OVERLAY_STATE_MODEL_EDIT,
    OVERLAY_STATE_MODEL_TEXT,
    OVERLAY_STATE_MODEL_DELETE
} OverlayState;

typedef enum {
    OVERLAY_DIALOG_NONE = 0,
    OVERLAY_DIALOG_CARTRIDGE_1,
    OVERLAY_DIALOG_CARTRIDGE_2,
    OVERLAY_DIALOG_CASSETTE,
    OVERLAY_DIALOG_DRIVE_A,
    OVERLAY_DIALOG_DRIVE_B,
    OVERLAY_DIALOG_SUNRISE_ROM,
    OVERLAY_DIALOG_IDE_IMAGE,
    OVERLAY_DIALOG_SD_MAPPER_ROM,
    OVERLAY_DIALOG_SD_CARD_A,
    OVERLAY_DIALOG_SD_CARD_B,
    OVERLAY_DIALOG_BIOS,
    OVERLAY_DIALOG_LOGO,
    OVERLAY_DIALOG_SUBROM,
    OVERLAY_DIALOG_DISK_ROM,
    OVERLAY_DIALOG_MODEL_BIOS,
    OVERLAY_DIALOG_MODEL_LOGO,
    OVERLAY_DIALOG_MODEL_SUBROM,
    OVERLAY_DIALOG_MODEL_DISK_ROM
} OverlayDialogTarget;

typedef struct {
    bool visible;
    bool dirty;
    OverlaySection section;
    OverlayState state;
    int row;

    Config saved;
    Config *config;
    ModelCatalog *models;
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

    int sunrise_setup_row;
    char pending_sunrise_rom_path[PATH_MAX];
    char pending_ide_image_path[PATH_MAX];

    int sd_mapper_setup_row;
    char pending_sd_mapper_rom_path[PATH_MAX];
    char pending_sd_card_path[MSX_SD_MAPPER_CARDS][PATH_MAX];
    bool pending_sd_mapper_ram;
    bool pending_sd_mapper_alternate_driver;

    int model_editor_row;
    int model_edit_index;
    int model_edit_field;
    int model_text_field;
    ModelDefinition model_edit;
    char model_text[PATH_MAX];
    char model_editor_error[192];
} Overlay;

void overlay_init(Overlay *overlay, Config *config,
                  ModelCatalog *models, Display *display,
                  MsxMachine *msx);
bool overlay_handle_event(Overlay *overlay, const SDL_Event *event);
void overlay_tick(Overlay *overlay);
void overlay_render_cassette_scope(const Overlay *overlay);
void overlay_render(const Overlay *overlay);
