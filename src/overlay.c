#include "overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "leds.h"
#include "notify.h"
#include "ui.h"

#ifndef PROG_GIT_COMMIT
#define PROG_GIT_COMMIT "unknown"
#endif

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

#define OVERLAY_LINE_H 18
#define OVERLAY_LABEL_X 28
#define OVERLAY_VALUE_X 188
#define OVERLAY_FIRST_Y 48
#define OVERLAY_RENDER_SCALE 1.5f
#define MODEL_EDITOR_FIELDS 7
#define MODEL_EDITOR_VISIBLE_ROWS 15

enum {
    GENERAL_MACHINE = 0,
    GENERAL_VIDEO_STANDARD,
    GENERAL_RAM,
    GENERAL_VRAM,
    GENERAL_PSG_VOLUME,
    GENERAL_MAIN_INPUT,
    GENERAL_JOY_PORT_A,
    GENERAL_JOY_PORT_B,
    GENERAL_EXTRA_HARDWARE,
    GENERAL_TINKER,
    GENERAL_ROWS
};

enum {
    MEGAFLASH_SETUP_FIRMWARE = 0,
    MEGAFLASH_SETUP_CARD_A,
    MEGAFLASH_SETUP_CARD_B,
    MEGAFLASH_SETUP_CONNECT,
    MEGAFLASH_SETUP_ROWS
};

enum {
    EXTENSION_SUNRISE_IDE = 0,
    EXTENSION_SD_MAPPER,
    EXTENSION_MEGAFLASH,
    EXTENSION_TCPIP_UNAPI,
    EXTENSION_KONAMI_SCC,
    EXTENSION_MSX_MUSIC,
    EXTENSION_SECOND_FLOPPY,
    EXTENSION_ROWS
};

enum {
    ADVANCED_MODEL_EDITOR = 0,
    ADVANCED_RTC_PERSISTENCE,
    ADVANCED_FLOPPY_IMAGE_ACCESS,
    ADVANCED_IDE_IMAGE_ACCESS,
    ADVANCED_SD_IMAGE_ACCESS,
    ADVANCED_SD_MAPPER_RAM,
    ADVANCED_SD_MAPPER_DRIVER,
    ADVANCED_SMOOTHING,
    ADVANCED_REAL_CRT,
    ADVANCED_CRT_SCANLINES,
    ADVANCED_GIF_RESOLUTION,
    ADVANCED_GIF_FPS,
    ADVANCED_GIF_ENCODER,
    ADVANCED_CASSETTE_AUDIBLE,
    ADVANCED_CASSETTE_VISUAL,
    ADVANCED_NOTIFICATIONS,
    ADVANCED_DEBUG,
    ADVANCED_VERSION,
    ADVANCED_ROWS
};

enum {
    MODEL_FIELD_ID = 0,
    MODEL_FIELD_NAME,
    MODEL_FIELD_HARDWARE,
    MODEL_FIELD_BIOS,
    MODEL_FIELD_LOGO,
    MODEL_FIELD_SUBROM,
    MODEL_FIELD_DISK_ROM
};

enum {
    SD_MAPPER_SETUP_FIRMWARE = 0,
    SD_MAPPER_SETUP_CARD_A,
    SD_MAPPER_SETUP_CARD_B,
    SD_MAPPER_SETUP_RAM,
    SD_MAPPER_SETUP_DRIVER,
    SD_MAPPER_SETUP_CONNECT,
    SD_MAPPER_SETUP_ROWS
};

enum {
    SUNRISE_SETUP_FIRMWARE = 0,
    SUNRISE_SETUP_DISK,
    SUNRISE_SETUP_CONNECT,
    SUNRISE_SETUP_ROWS
};

static const char *section_name(OverlaySection section) {
    switch (section) {
        case OVERLAY_GENERAL:    return "General";
        case OVERLAY_MEDIA:      return "Media";
        case OVERLAY_EXTENSIONS: return "Extensions";
        case OVERLAY_ADVANCED:   return "Advanced";
        case OVERLAY_SECTION_COUNT: break;
    }
    return "";
}

static bool section_available(const Overlay *overlay,
                              OverlaySection section) {
    if (section == OVERLAY_EXTENSIONS)
        return overlay->config->extra_hardware;
    if (section == OVERLAY_ADVANCED)
        return overlay->config->tinker;
    return true;
}

static int section_rows(const Overlay *overlay,
                        OverlaySection section) {
    switch (section) {
        case OVERLAY_GENERAL:    return GENERAL_ROWS;
        case OVERLAY_MEDIA:
            return 6 +
                   (overlay->config->second_drive ? 1 : 0) +
                   (overlay->config->sunrise_ide ? 1 : 0) +
                   (overlay->config->sd_mapper ? 2 : 0) +
                   (overlay->config->megaflash ? 2 : 0);
        case OVERLAY_EXTENSIONS: return EXTENSION_ROWS;
        case OVERLAY_ADVANCED:   return ADVANCED_ROWS;
        case OVERLAY_SECTION_COUNT: break;
    }
    return 0;
}

static const char *toggle_name(bool enabled) {
    return enabled ? "On" : "Off";
}

static const char *input_port_name(InputPort port) {
    return port == INPUT_PORT_B ? "Joy Port B" : "Joy Port A";
}

static const char *joy_port_device_name(JoyPortDevice device) {
    return device == JOY_PORT_MOUSE ? "Mouse" : "Joystick";
}

static const char *ide_mode_name(AtaImageMode mode) {
    return mode == ATA_IMAGE_READ_WRITE ? "Read/write" : "Read-only";
}

static const char *floppy_mode_name(FloppyImageMode mode) {
    return mode == FLOPPY_IMAGE_READ_WRITE
         ? "Read/write" : "Read-only";
}

static const char *sd_mode_name(SdImageMode mode) {
    return mode == SD_IMAGE_READ_WRITE
         ? "Read/write" : "Read-only";
}

static const char *path_basename(const char *path);

static int media_floppy_b_row(const Config *config) {
    return config->second_drive ? 6 : -1;
}

static int media_ide_row(const Config *config) {
    return config->sunrise_ide
         ? 6 + (config->second_drive ? 1 : 0) : -1;
}

static int media_sd_a_row(const Config *config) {
    if (!config->sd_mapper)
        return -1;
    return 6 + (config->second_drive ? 1 : 0) +
           (config->sunrise_ide ? 1 : 0);
}

static int media_sd_b_row(const Config *config) {
    int row = media_sd_a_row(config);

    return row < 0 ? -1 : row + 1;
}

static int media_megaflash_sd_a_row(const Config *config) {
    if (!config->megaflash)
        return -1;
    return 6 + (config->second_drive ? 1 : 0) +
           (config->sunrise_ide ? 1 : 0) +
           (config->sd_mapper ? 2 : 0);
}

static int media_megaflash_sd_b_row(const Config *config) {
    int row = media_megaflash_sd_a_row(config);

    return row < 0 ? -1 : row + 1;
}

static int cartridge_extension_slot(const Config *config,
                                    const char *name) {
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        const char *owner =
            config_cartridge_slot_owner(config, slot);

        if (owner && strcmp(owner, name) == 0)
            return (int)slot;
    }
    return -1;
}

static void reconcile_extension_slots(Overlay *overlay) {
    msx_reassign_extension_slots(
        overlay->msx,
        cartridge_extension_slot(overlay->config, "Sunrise IDE"),
        cartridge_extension_slot(overlay->config, "SD Mapper V2"),
        cartridge_extension_slot(
            overlay->config, "MegaFlashROM SCC+ SD"));
}

static void cartridge_extension_text(const Config *config,
                                     const char *name, bool enabled,
                                     char *value, size_t value_size) {
    if (!enabled) {
        snprintf(value, value_size, "Off");
        return;
    }
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        const char *owner =
            config_cartridge_slot_owner(config, slot);

        if (owner && strcmp(owner, name) == 0) {
            snprintf(value, value_size,
                     "On (Cartridge %u, device stub)", slot + 1);
            return;
        }
    }
    snprintf(value, value_size, "Off (no free cartridge slot)");
}

static void sunrise_extension_text(const Overlay *overlay,
                                   char *value, size_t value_size) {
    const Config *config = overlay->config;
    int slot = cartridge_extension_slot(config, "Sunrise IDE");

    if (!config->sunrise_ide) {
        snprintf(value, value_size, "%s",
                 config->sunrise_rom_path[0]
                 ? "Off (configured)" : "Off (setup required)");
    } else if (slot < 0) {
        snprintf(value, value_size, "Off (no free cartridge slot)");
    } else if (!msx_sunrise_connected(overlay->msx)) {
        snprintf(value, value_size,
                 "On (Cartridge %d, ROM not loaded)", slot + 1);
    } else {
        snprintf(value, value_size, "On (Cartridge %d)", slot + 1);
    }
}

static bool sd_mapper_rom_file_is_valid(const char *path);

static void sd_mapper_extension_text(const Overlay *overlay,
                                     char *value, size_t value_size) {
    const Config *config = overlay->config;
    int slot = cartridge_extension_slot(config, "SD Mapper V2");

    if (!config->sd_mapper) {
        snprintf(value, value_size, "%s",
                 sd_mapper_rom_file_is_valid(
                     config->sd_mapper_rom_path)
                 ? "Off (configured)" : "Off (setup required)");
    } else if (slot < 0) {
        snprintf(value, value_size, "Off (no free cartridge slot)");
    } else if (!msx_sd_mapper_connected(overlay->msx)) {
        snprintf(value, value_size,
                 "On (Cartridge %d, ROM not loaded)", slot + 1);
    } else {
        snprintf(value, value_size,
                 "On (Cartridge %d, %s)", slot + 1,
                 config->sd_mapper_ram
                 ? "512 KB RAM" : "storage only");
    }
}

static bool megaflash_rom_file_is_valid(const char *path);

static void megaflash_extension_text(const Overlay *overlay,
                                     char *value, size_t value_size) {
    const Config *config = overlay->config;
    int slot = cartridge_extension_slot(
        config, "MegaFlashROM SCC+ SD");

    if (!config->megaflash) {
        snprintf(value, value_size, "%s",
                 megaflash_rom_file_is_valid(
                     config->megaflash_rom_path)
                 ? "Off (configured)" : "Off (setup required)");
    } else if (slot < 0) {
        snprintf(value, value_size, "Off (no free cartridge slot)");
    } else if (!msx_megaflash_connected(overlay->msx)) {
        snprintf(value, value_size,
                 "On (Cartridge %d, flash not loaded)", slot + 1);
    } else {
        snprintf(value, value_size,
                 "On (Cartridge %d, 512 KB RAM)", slot + 1);
    }
}

static void ide_image_text(const Overlay *overlay,
                           char *value, size_t value_size) {
    const char *path = overlay->config->ide_image_path;

    if (msx_sunrise_disk_mounted(overlay->msx)) {
        const char *state =
            msx_sunrise_disk_has_error(overlay->msx)
            ? ", I/O error" :
            msx_sunrise_disk_dirty(overlay->msx)
            ? ", dirty" : "";

        snprintf(value, value_size, "%s [%s%s]",
                 path_basename(path),
                 msx_sunrise_disk_writable(overlay->msx)
                 ? "read/write" : "read-only", state);
    }
    else if (path[0])
        snprintf(value, value_size, "%s [not mounted, %s]",
                 path_basename(path),
                 overlay->config->ide_image_mode ==
                     ATA_IMAGE_READ_WRITE
                 ? "read/write" : "read-only");
    else
        snprintf(value, value_size, "[not mounted]");
}

static void cassette_text(const Overlay *overlay,
                          char *value, size_t value_size) {
    const char *path = overlay->config->cassette_path;

    if (msx_cassette_mounted(overlay->msx)) {
        u64 position = msx_cassette_position_ms(overlay->msx) / 1000u;
        u64 duration = msx_cassette_duration_ms(overlay->msx) / 1000u;
        const char *state =
            msx_cassette_rolling(overlay->msx) ? "playing" :
            msx_cassette_at_end(overlay->msx) ? "end" : "stopped";

        snprintf(value, value_size,
                 "%s [%s, %s %llu:%02llu/%llu:%02llu]",
                 path_basename(path),
                 cassette_file_type_name(
                     msx_cassette_file_type(overlay->msx)),
                 state,
                 (unsigned long long)(position / 60u),
                 (unsigned long long)(position % 60u),
                 (unsigned long long)(duration / 60u),
                 (unsigned long long)(duration % 60u));
    } else if (path[0]) {
        snprintf(value, value_size, "%s [not mounted]",
                 path_basename(path));
    } else {
        snprintf(value, value_size, "[not mounted]");
    }
}

static void drive_a_text(const Overlay *overlay,
                         char *value, size_t value_size) {
    const char *path = overlay->config->drive_a_path;

    if (!msx_floppy_supported(overlay->msx)) {
        snprintf(value, value_size,
                 "[requires Philips NMS 8250]");
    } else if (msx_drive_a_mounted(overlay->msx)) {
        const char *state =
            msx_drive_a_has_error(overlay->msx)
            ? ", I/O error" :
            msx_drive_a_dirty(overlay->msx)
            ? ", dirty" : "";

        snprintf(value, value_size, "%s [%s%s]",
                 path_basename(path),
                 msx_drive_a_writable(overlay->msx)
                 ? "read/write" : "read-only", state);
    } else if (path[0]) {
        snprintf(value, value_size, "%s [not mounted, %s]",
                 path_basename(path),
                 floppy_mode_name(
                     overlay->config->floppy_image_mode));
    } else {
        snprintf(value, value_size, "[not mounted]");
    }
}

static void drive_b_text(const Overlay *overlay,
                         char *value, size_t value_size) {
    const char *path = overlay->config->drive_b_path;

    if (!msx_floppy_supported(overlay->msx)) {
        snprintf(value, value_size,
                 "[requires Philips NMS 8250]");
    } else if (msx_drive_b_mounted(overlay->msx)) {
        const char *state =
            msx_drive_b_has_error(overlay->msx)
            ? ", I/O error" :
            msx_drive_b_dirty(overlay->msx)
            ? ", dirty" : "";

        snprintf(value, value_size, "%s [%s%s]",
                 path_basename(path),
                 msx_drive_b_writable(overlay->msx)
                 ? "read/write" : "read-only", state);
    } else if (path[0]) {
        snprintf(value, value_size, "%s [not mounted, %s]",
                 path_basename(path),
                 floppy_mode_name(
                     overlay->config->floppy_image_mode));
    } else {
        snprintf(value, value_size, "[not mounted]");
    }
}

static void sd_card_text(const Overlay *overlay, unsigned card,
                         char *value, size_t value_size) {
    const char *path = overlay->config->sd_card_path[card];

    if (msx_sd_card_mounted(overlay->msx, card)) {
        const char *state =
            msx_sd_card_has_error(overlay->msx, card)
            ? ", I/O error" :
            msx_sd_card_dirty(overlay->msx, card)
            ? ", dirty" : "";

        snprintf(value, value_size, "%s [%s%s]",
                 path_basename(path),
                 msx_sd_card_writable(overlay->msx, card)
                 ? "read/write" : "read-only", state);
    } else if (path[0]) {
        snprintf(value, value_size, "%s [not mounted, %s]",
                 path_basename(path),
                 sd_mode_name(overlay->config->sd_image_mode));
    } else {
        snprintf(value, value_size, "[not mounted]");
    }
}

static void megaflash_card_text(const Overlay *overlay, unsigned card,
                                char *value, size_t value_size) {
    const char *path =
        overlay->config->megaflash_card_path[card];

    if (msx_megaflash_card_mounted(overlay->msx, card)) {
        const char *state =
            msx_megaflash_card_has_error(overlay->msx, card)
            ? ", I/O error" :
            msx_megaflash_card_dirty(overlay->msx, card)
            ? ", dirty" : "";

        snprintf(value, value_size, "%s [%s%s]",
                 path_basename(path),
                 msx_megaflash_card_writable(overlay->msx, card)
                 ? "read/write" : "read-only", state);
    } else if (path[0]) {
        snprintf(value, value_size, "%s [not mounted, %s]",
                 path_basename(path),
                 sd_mode_name(overlay->config->sd_image_mode));
    } else {
        snprintf(value, value_size, "[not mounted]");
    }
}

static const char *notification_name(NotifyMode mode) {
    switch (mode) {
        case NOTIFY_MODE_OFF:     return "Off";
        case NOTIFY_MODE_CONSOLE: return "Console";
        case NOTIFY_MODE_SCREEN:  return "Screen";
    }
    return "Screen";
}

static const char *path_basename(const char *path) {
    const char *slash;
    const char *backslash;

    if (!path || !path[0])
        return "";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
    return slash ? slash + 1 : path;
}

static void cartridge_text(const Overlay *overlay, unsigned slot,
                           char *value, size_t value_size) {
    const char *owner =
        config_cartridge_slot_owner(overlay->config, slot);
    const char *path = overlay->config->cartridge_path[slot];
    const MsxCartridge *cartridge =
        msx_get_cartridge(overlay->msx, slot);

    if (owner)
        snprintf(value, value_size, "[reserved by %s]", owner);
    else if (cartridge && cartridge->loaded)
        snprintf(value, value_size, "%s", path_basename(path));
    else if (path[0])
        snprintf(value, value_size, "%s [not loaded]",
                 path_basename(path));
    else
        snprintf(value, value_size, "[not mounted]");
}

static void mapper_text(const Overlay *overlay, unsigned slot,
                        char *value, size_t value_size) {
    const char *owner =
        config_cartridge_slot_owner(overlay->config, slot);
    MsxCartridgeMapper requested =
        overlay->config->cartridge_mapper[slot];
    const MsxCartridge *cartridge =
        msx_get_cartridge(overlay->msx, slot);

    if (owner)
        snprintf(value, value_size, "[unavailable: %s]", owner);
    else if (requested == MSX_CART_MAPPER_AUTO &&
        cartridge && cartridge->loaded)
        snprintf(value, value_size, "Auto (%s)",
                 msx_cartridge_mapper_display_name(cartridge->mapper));
    else
        snprintf(value, value_size, "%s",
                 msx_cartridge_mapper_display_name(requested));
}

static void machine_text(const Overlay *overlay,
                         char *value, size_t value_size) {
    const char *bios = overlay->config->bios_path;
    const ModelDefinition *definition =
        model_catalog_find(overlay->models,
                           overlay->config->machine_id);
    const char *name =
        definition ? definition->name :
        msx_model_name(overlay->config->model);

    if (overlay->msx->bios_loaded)
        snprintf(value, value_size, "%s - %s",
                 name,
                 path_basename(bios));
    else if (bios[0])
        snprintf(value, value_size, "%s - %s [not loaded]",
                 name,
                 path_basename(bios));
    else
        snprintf(value, value_size, "%s - [no BIOS]",
                 name);
}

static void item_text(const Overlay *overlay, int row,
                      char *label, size_t label_size,
                      char *value, size_t value_size) {
    const Config *config = overlay->config;
    const MsxMachine *msx = overlay->msx;

    label[0] = '\0';
    value[0] = '\0';
    switch (overlay->section) {
        case OVERLAY_GENERAL:
            switch (row) {
                case GENERAL_MACHINE:
                    snprintf(label, label_size, "Machine");
                    machine_text(overlay, value, value_size);
                    break;
                case GENERAL_VIDEO_STANDARD:
                    snprintf(label, label_size, "Video standard");
                    snprintf(value, value_size, "%s",
                             msx_region_name(config->region));
                    break;
                case GENERAL_RAM:
                    snprintf(label, label_size, "RAM");
                    snprintf(value, value_size, "%d KB", config->memory_kb);
                    break;
                case GENERAL_VRAM:
                    snprintf(label, label_size, "VRAM");
                    snprintf(value, value_size, "%d KB (%s)",
                             msx->profile->vram_kb, msx_vdp_name(msx));
                    break;
                case GENERAL_PSG_VOLUME:
                    snprintf(label, label_size, "PSG volume");
                    snprintf(value, value_size, "%d%%",
                             config->audio_volume);
                    break;
                case GENERAL_MAIN_INPUT:
                    snprintf(label, label_size, "Main Input");
                    snprintf(value, value_size, "%s",
                             input_port_name(config->main_input));
                    break;
                case GENERAL_JOY_PORT_A:
                    snprintf(label, label_size, "Joy Port A");
                    snprintf(
                        value, value_size, "%s",
                        joy_port_device_name(
                            config->joy_port_device[0]));
                    break;
                case GENERAL_JOY_PORT_B:
                    snprintf(label, label_size, "Joy Port B");
                    snprintf(
                        value, value_size, "%s",
                        joy_port_device_name(
                            config->joy_port_device[1]));
                    break;
                case GENERAL_EXTRA_HARDWARE:
                    snprintf(label, label_size, "Extra Hardware");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->extra_hardware));
                    break;
                case GENERAL_TINKER:
                    snprintf(label, label_size, "Tinker");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->tinker));
                    break;
            }
            break;
        case OVERLAY_MEDIA:
            if (row == 0) {
                snprintf(label, label_size, "Cartridge 1");
                cartridge_text(overlay, 0, value, value_size);
            } else if (row == 1) {
                snprintf(label, label_size, "Cart 1 mapper");
                mapper_text(overlay, 0, value, value_size);
            } else if (row == 2) {
                snprintf(label, label_size, "Cartridge 2");
                cartridge_text(overlay, 1, value, value_size);
            } else if (row == 3) {
                snprintf(label, label_size, "Cart 2 mapper");
                mapper_text(overlay, 1, value, value_size);
            } else if (row == 4) {
                snprintf(label, label_size, "Cassette");
                cassette_text(overlay, value, value_size);
            } else if (row == 5) {
                snprintf(label, label_size, "Floppy A");
                drive_a_text(overlay, value, value_size);
            } else if (row == media_floppy_b_row(config)) {
                snprintf(label, label_size, "Floppy B");
                drive_b_text(overlay, value, value_size);
            } else if (row == media_ide_row(config)) {
                snprintf(label, label_size, "IDE hard disk");
                ide_image_text(overlay, value, value_size);
            } else if (row == media_sd_a_row(config)) {
                snprintf(label, label_size, "SD Card A");
                sd_card_text(overlay, 0, value, value_size);
            } else if (row == media_sd_b_row(config)) {
                snprintf(label, label_size, "SD Card B");
                sd_card_text(overlay, 1, value, value_size);
            } else if (row == media_megaflash_sd_a_row(config)) {
                snprintf(label, label_size, "MegaFlash SD A");
                megaflash_card_text(
                    overlay, 0, value, value_size);
            } else if (row == media_megaflash_sd_b_row(config)) {
                snprintf(label, label_size, "MegaFlash SD B");
                megaflash_card_text(
                    overlay, 1, value, value_size);
            }
            break;
        case OVERLAY_EXTENSIONS:
            switch (row) {
                case EXTENSION_SUNRISE_IDE:
                    snprintf(label, label_size, "Sunrise IDE");
                    sunrise_extension_text(
                        overlay, value, value_size);
                    break;
                case EXTENSION_SD_MAPPER:
                    snprintf(label, label_size, "SD Mapper V2");
                    sd_mapper_extension_text(
                        overlay, value, value_size);
                    break;
                case EXTENSION_MEGAFLASH:
                    snprintf(label, label_size,
                             "MegaFlashROM SCC+SD");
                    megaflash_extension_text(
                        overlay, value, value_size);
                    break;
                case EXTENSION_TCPIP_UNAPI:
                    snprintf(label, label_size,
                             "MSX TCP/IP UNAPI");
                    if (!config->tcpip_unapi) {
                        snprintf(value, value_size, "Off");
                    } else if (unapinet_guest_driver_active(
                                   overlay->unapinet)) {
                        snprintf(value, value_size, "On (TSR active)");
                    } else {
                        snprintf(value, value_size, "On (awaiting TSR)");
                    }
                    break;
                case EXTENSION_KONAMI_SCC:
                    snprintf(label, label_size, "Konami SCC");
                    cartridge_extension_text(
                        config, "Konami SCC", config->scc,
                        value, value_size);
                    break;
                case EXTENSION_MSX_MUSIC:
                    snprintf(label, label_size, "MSX-MUSIC");
                    cartridge_extension_text(
                        config, "MSX-MUSIC", config->msx_music,
                        value, value_size);
                    break;
                case EXTENSION_SECOND_FLOPPY:
                    snprintf(label, label_size, "Second floppy");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->second_drive));
                    break;
            }
            break;
        case OVERLAY_ADVANCED:
            switch (row) {
                case ADVANCED_MODEL_EDITOR:
                    snprintf(label, label_size,
                             "Machine model editor");
                    snprintf(value, value_size, "%zu models",
                             overlay->models->count);
                    break;
                case ADVANCED_RTC_PERSISTENCE:
                    snprintf(label, label_size,
                             "RTC persistence");
                    if (!overlay->msx->profile->rtc) {
                        snprintf(value, value_size, "Unavailable");
                    } else if (msx_rtc_persistence_has_error(
                                   overlay->msx)) {
                        snprintf(value, value_size, "%s (I/O warning)",
                                 toggle_name(config->rtc_persistence));
                    } else {
                        snprintf(value, value_size, "%s",
                                 toggle_name(config->rtc_persistence));
                    }
                    break;
                case ADVANCED_FLOPPY_IMAGE_ACCESS:
                    snprintf(label, label_size,
                             "Floppy access mode");
                    snprintf(value, value_size, "%s",
                             floppy_mode_name(
                                 config->floppy_image_mode));
                    break;
                case ADVANCED_IDE_IMAGE_ACCESS:
                    snprintf(label, label_size,
                             "IDE access mode");
                    snprintf(value, value_size, "%s",
                             ide_mode_name(config->ide_image_mode));
                    break;
                case ADVANCED_SD_IMAGE_ACCESS:
                    snprintf(label, label_size,
                             "SD access mode");
                    snprintf(value, value_size, "%s",
                             sd_mode_name(config->sd_image_mode));
                    break;
                case ADVANCED_SD_MAPPER_RAM:
                    snprintf(label, label_size,
                             "SD Mapper 512KB RAM");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->sd_mapper_ram));
                    break;
                case ADVANCED_SD_MAPPER_DRIVER:
                    snprintf(label, label_size,
                             "SD Mapper driver");
                    snprintf(value, value_size, "%s",
                             config->sd_mapper_alternate_driver
                             ? "Alternate" : "Primary");
                    break;
                case ADVANCED_SMOOTHING:
                    snprintf(label, label_size, "Smoothing");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->smoothing));
                    break;
                case ADVANCED_REAL_CRT:
                    snprintf(label, label_size, "Real CRT");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->real_crt));
                    break;
                case ADVANCED_CRT_SCANLINES:
                    snprintf(label, label_size, "CRT scanlines");
                    snprintf(value, value_size,
                             config->real_crt ? "%d%%" : "%d%% (inactive)",
                             config->crt_scanlines);
                    break;
                case ADVANCED_GIF_RESOLUTION:
                    snprintf(label, label_size, "GIF resolution");
                    snprintf(value, value_size, "%dx%d",
                             config->gif_width,
                             (config->gif_width * 3) / 4);
                    break;
                case ADVANCED_GIF_FPS:
                    snprintf(label, label_size, "GIF frame rate");
                    snprintf(value, value_size, "%d fps",
                             config->gif_fps);
                    break;
                case ADVANCED_GIF_ENCODER:
                    snprintf(label, label_size, "GIF encoder");
                    snprintf(value, value_size, "%s",
                             config->gif_ffmpeg
                                 ? "FFmpeg optimize" : "built-in");
                    break;
                case ADVANCED_CASSETTE_AUDIBLE:
                    snprintf(label, label_size,
                             "Tape Audio Monitor");
                    snprintf(value, value_size, "%s",
                             toggle_name(
                                 config->cassette_audible_monitor));
                    break;
                case ADVANCED_CASSETTE_VISUAL:
                    snprintf(label, label_size,
                             "Tape Visual Monitor");
                    snprintf(value, value_size, "%s",
                             toggle_name(
                                 config->cassette_visual_monitor));
                    break;
                case ADVANCED_NOTIFICATIONS:
                    snprintf(label, label_size, "Notifications");
                    snprintf(value, value_size, "%s",
                             notification_name(config->notifications));
                    break;
                case ADVANCED_DEBUG:
                    snprintf(label, label_size, "Debug overlay");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->debug));
                    break;
                case ADVANCED_VERSION:
                    snprintf(label, label_size, "Version");
                    snprintf(value, value_size, "%s (git %s)",
                             PACKAGE_VERSION, PROG_GIT_COMMIT);
                    break;
            }
            break;
        case OVERLAY_SECTION_COUNT:
            break;
    }
}

static void configure_leds(const Config *config, const MsxMachine *msx) {
    leds_set_enabled(LED_POWER, true);
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        const char *owner =
            config_cartridge_slot_owner(config, slot);

        leds_set_cartridge(
            slot, LED_CARTRIDGE_STANDARD,
            owner != NULL || msx_get_cartridge(msx, slot)->loaded);
    }
    leds_set_enabled(LED_CAPS, true);
    leds_set_enabled(LED_KANA, true);
    leds_set_enabled(LED_FDC_A, true);
    leds_set_enabled(LED_FDC_B, config->second_drive);
    leds_set_enabled(LED_TAPE, true);
    leds_set_enabled(LED_IDE, config->sunrise_ide);
    leds_set_enabled(LED_SD_A,
                     config->sd_mapper || config->megaflash);
    leds_set_enabled(LED_SD_B,
                     config->sd_mapper || config->megaflash);
    leds_set_enabled(LED_NETWORK, config->tcpip_unapi);
    leds_set_state(LED_POWER, true);
    leds_set_state(LED_CAPS, msx->caps_led);
    leds_set_state(LED_KANA, msx->kana_led);
}

static const char *selected_model_name(const Overlay *overlay) {
    const ModelDefinition *definition =
        model_catalog_find(overlay->models,
                           overlay->config->machine_id);

    return definition ? definition->name :
           msx_model_name(overlay->config->model);
}

static bool sync_rtc_persistence(Overlay *overlay) {
    char path[PATH_MAX];

    if (config_rtc_path(
            overlay->config, path, sizeof(path)) != 0) {
        notify_post("RTC persistence path is too long");
        return false;
    }
    if (msx_set_rtc_persistence(
            overlay->msx, path, rtc_host_seconds()) != 0) {
        notify_post("RTC persistence warning: %s",
                    msx_rtc_persistence_error(overlay->msx));
        /*
         * A corrupt file is ignored in favour of a live host-seeded
         * clock, but the requested path remains attached so the next
         * safe flush can replace it. A flush/path failure leaves the
         * old attachment in place and must block the configuration
         * transition.
         */
        return strcmp(msx_rtc_persistence_path(overlay->msx),
                      path) == 0;
    }
    return true;
}

static void apply_config(Overlay *overlay) {
    Config *config = overlay->config;
    MsxMachine *msx = overlay->msx;
    bool machine_changed =
        msx->profile->model != config->model ||
        msx->region != config->region ||
        msx->ram_kb != config->memory_kb;

    config_normalize(config);
    if (machine_changed) {
        if (msx_set_rtc_persistence(
                msx, "", rtc_host_seconds()) != 0) {
            notify_post("Could not save RTC CMOS: %s",
                        msx_rtc_persistence_error(msx));
            return;
        }
        msx_configure(msx, config->model, config->region,
                      config->memory_kb);
        config->memory_kb = msx->ram_kb;
        display_prepare_scaffold(overlay->display, msx);
        display_set_title(overlay->display, msx,
                          selected_model_name(overlay));
    }
    (void)sync_rtc_persistence(overlay);
    display_set_smoothing(overlay->display, config->smoothing);
    display_set_crt(overlay->display, config->real_crt,
                    config->crt_scanlines);
    psg_set_volume(&msx->psg, config->audio_volume);
    msx_set_cassette_audible_monitor(
        msx, config->tinker &&
             config->cassette_audible_monitor);
    msx_sd_mapper_set_ram_enabled(msx, config->sd_mapper_ram);
    msx_sd_mapper_set_alternate_driver(
        msx, config->sd_mapper_alternate_driver);
    if (overlay->unapinet &&
        unapinet_enabled(overlay->unapinet) != config->tcpip_unapi &&
        !unapinet_set_enabled(
            overlay->unapinet, config->tcpip_unapi)) {
        config->tcpip_unapi = false;
        notify_post("Could not enable MSX TCP/IP UNAPI: %s",
                    unapinet_error(overlay->unapinet));
    }
    notify_set_mode(config->notifications);
    configure_leds(config, msx);
}

static void restore_cartridges(Overlay *overlay) {
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        const char *current = overlay->config->cartridge_path[slot];
        const char *saved = overlay->saved.cartridge_path[slot];
        const char *current_owner =
            config_cartridge_slot_owner(overlay->config, slot);
        const char *saved_owner =
            config_cartridge_slot_owner(&overlay->saved, slot);
        bool changed =
            strcmp(current, saved) != 0 ||
            overlay->config->cartridge_mapper[slot] !=
                overlay->saved.cartridge_mapper[slot] ||
            (current_owner != NULL) != (saved_owner != NULL) ||
            (current_owner && saved_owner &&
             strcmp(current_owner, saved_owner) != 0);

        if (!changed)
            continue;
        if (saved_owner || !saved[0]) {
            msx_eject_cartridge(overlay->msx, slot);
        } else if (msx_load_cartridge_slot(
                       overlay->msx, slot, saved,
                       overlay->saved.cartridge_mapper[slot]) != 0) {
            msx_eject_cartridge(overlay->msx, slot);
            notify_post("Could not restore cartridge %u: %s",
                        slot + 1, path_basename(saved));
        }
    }
}

static void restore_firmware(Overlay *overlay) {
    const Config *current = overlay->config;
    const Config *saved = &overlay->saved;
    bool changed =
        current->model != saved->model ||
        strcmp(current->machine_id, saved->machine_id) != 0 ||
        strcmp(current->bios_path, saved->bios_path) != 0 ||
        strcmp(current->logo_path, saved->logo_path) != 0 ||
        strcmp(current->subrom_path, saved->subrom_path) != 0 ||
        strcmp(current->disk_rom_path, saved->disk_rom_path) != 0;

    if (!changed)
        return;
    if (!saved->bios_path[0]) {
        msx_eject_firmware(overlay->msx);
    } else if (msx_load_firmware_set(
                   overlay->msx, saved->bios_path,
                   saved->logo_path,
                   saved->subrom_path, saved->disk_rom_path) != 0) {
        msx_eject_firmware(overlay->msx);
        notify_post("Could not restore firmware for %s",
                    msx_model_name(saved->model));
    }
}

static bool restore_sunrise(Overlay *overlay) {
    const Config *current = overlay->config;
    const Config *saved = &overlay->saved;
    int saved_slot =
        cartridge_extension_slot(saved, "Sunrise IDE");
    bool changed =
        current->sunrise_ide != saved->sunrise_ide ||
        strcmp(current->sunrise_rom_path,
               saved->sunrise_rom_path) != 0 ||
        strcmp(current->ide_image_path,
               saved->ide_image_path) != 0 ||
        current->ide_image_mode != saved->ide_image_mode ||
        msx_sunrise_slot(overlay->msx) !=
            (saved->sunrise_ide ? saved_slot : -1) ||
        (msx_sunrise_disk_mounted(overlay->msx) &&
         msx_sunrise_disk_writable(overlay->msx) !=
             (saved->ide_image_mode == ATA_IMAGE_READ_WRITE));

    if (!changed)
        return true;
    if (msx_eject_sunrise_ide(overlay->msx) != 0) {
        notify_post("Could not restore Sunrise IDE: %s",
                    msx_sunrise_disk_error(overlay->msx));
        return false;
    }
    if (!saved->sunrise_ide || saved_slot < 0)
        return true;
    if (msx_load_sunrise_ide(
            overlay->msx, (unsigned)saved_slot,
            saved->sunrise_rom_path) != 0) {
        notify_post("Could not restore Sunrise IDE ROM");
        return false;
    }
    if (saved->ide_image_path[0] &&
        msx_mount_sunrise_disk_mode(
            overlay->msx, saved->ide_image_path,
            saved->ide_image_mode) != 0) {
        notify_post("Could not restore Sunrise IDE disk: %s",
                    msx_sunrise_disk_error(overlay->msx));
        return false;
    }
    return true;
}

static bool restore_sd_mapper(Overlay *overlay) {
    const Config *current = overlay->config;
    const Config *saved = &overlay->saved;
    int saved_slot =
        cartridge_extension_slot(saved, "SD Mapper V2");
    bool changed =
        current->sd_mapper != saved->sd_mapper ||
        strcmp(current->sd_mapper_rom_path,
               saved->sd_mapper_rom_path) != 0 ||
        strcmp(current->sd_card_path[0],
               saved->sd_card_path[0]) != 0 ||
        strcmp(current->sd_card_path[1],
               saved->sd_card_path[1]) != 0 ||
        current->sd_image_mode != saved->sd_image_mode ||
        current->sd_mapper_ram != saved->sd_mapper_ram ||
        current->sd_mapper_alternate_driver !=
            saved->sd_mapper_alternate_driver ||
        msx_sd_mapper_slot(overlay->msx) !=
            (saved->sd_mapper ? saved_slot : -1);

    for (unsigned card = 0;
         card < MSX_SD_MAPPER_CARDS && !changed; ++card) {
        if (msx_sd_card_mounted(overlay->msx, card) &&
            msx_sd_card_writable(overlay->msx, card) !=
                (saved->sd_image_mode == SD_IMAGE_READ_WRITE))
            changed = true;
    }
    if (!changed)
        return true;
    if (msx_eject_sd_mapper(overlay->msx) != 0) {
        notify_post("Could not restore SD Mapper V2: %s",
                    msx_sd_card_has_error(overlay->msx, 0)
                    ? msx_sd_card_error(overlay->msx, 0)
                    : msx_sd_card_error(overlay->msx, 1));
        return false;
    }
    if (!saved->sd_mapper || saved_slot < 0)
        return true;
    if (msx_load_sd_mapper(
            overlay->msx, (unsigned)saved_slot,
            saved->sd_mapper_rom_path) != 0) {
        notify_post("Could not restore SD Mapper V2 ROM");
        return false;
    }
    msx_sd_mapper_set_ram_enabled(
        overlay->msx, saved->sd_mapper_ram);
    msx_sd_mapper_set_alternate_driver(
        overlay->msx, saved->sd_mapper_alternate_driver);
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card) {
        if (!saved->sd_card_path[card][0])
            continue;
        if (msx_mount_sd_card(
                overlay->msx, card,
                saved->sd_card_path[card],
                saved->sd_image_mode) != 0) {
            notify_post("Could not restore SD Card %c: %s",
                        'A' + (int)card,
                        msx_sd_card_error(overlay->msx, card));
            return false;
        }
    }
    return true;
}

static bool megaflash_transaction_paths(
    Overlay *overlay, const char *initial_path,
    char *state_path, size_t state_path_size,
    char *pending_path, size_t pending_path_size) {
    Config state_config = *overlay->config;

    state_config.megaflash = true;
    snprintf(state_config.megaflash_rom_path,
             sizeof(state_config.megaflash_rom_path), "%s",
             initial_path);
    if (config_megaflash_state_path(
            &state_config, state_path, state_path_size) != 0) {
        notify_post("MegaFlash persistent state path is too long");
        return false;
    }
    if (config_megaflash_pending_state_path(
            &state_config, pending_path, pending_path_size) != 0) {
        notify_post("MegaFlash pending state path is too long");
        return false;
    }
    return true;
}

static void discard_pending_megaflash_state(Overlay *overlay) {
    if (overlay->megaflash_state_pending &&
        overlay->megaflash_pending_state_path[0])
        (void)remove(overlay->megaflash_pending_state_path);
    overlay->megaflash_state_pending = false;
    overlay->megaflash_pending_state_path[0] = '\0';
    overlay->megaflash_final_state_path[0] = '\0';
}

static void remember_pending_megaflash_state(
    Overlay *overlay, const char *pending_path,
    const char *state_path) {
    if (overlay->megaflash_state_pending &&
        strcmp(overlay->megaflash_pending_state_path,
               pending_path) != 0)
        (void)remove(overlay->megaflash_pending_state_path);
    overlay->megaflash_state_pending = true;
    snprintf(overlay->megaflash_pending_state_path,
             sizeof(overlay->megaflash_pending_state_path), "%s",
             pending_path);
    snprintf(overlay->megaflash_final_state_path,
             sizeof(overlay->megaflash_final_state_path), "%s",
             state_path);
}

static bool prepare_pending_megaflash_state(
    Overlay *overlay, const char *initial_path,
    const char *pending_path, const char *state_path) {
    if (!pending_path[0]) {
        discard_pending_megaflash_state(overlay);
        return true;
    }
    if (msx_prepare_megaflash_state(
            initial_path, pending_path) != 0) {
        notify_post("Could not prepare MegaFlash persistent state");
        return false;
    }
    remember_pending_megaflash_state(
        overlay, pending_path, state_path);
    return true;
}

static bool restore_megaflash(Overlay *overlay) {
    const Config *current = overlay->config;
    const Config *saved = &overlay->saved;
    int saved_slot =
        cartridge_extension_slot(saved, "MegaFlashROM SCC+ SD");
    bool changed =
        current->megaflash != saved->megaflash ||
        strcmp(current->megaflash_rom_path,
               saved->megaflash_rom_path) != 0 ||
        strcmp(current->megaflash_card_path[0],
               saved->megaflash_card_path[0]) != 0 ||
        strcmp(current->megaflash_card_path[1],
               saved->megaflash_card_path[1]) != 0 ||
        current->sd_image_mode != saved->sd_image_mode ||
        msx_megaflash_slot(overlay->msx) !=
            (saved->megaflash ? saved_slot : -1);
    char state_path[PATH_MAX];

    for (unsigned card = 0;
         card < MSX_MEGAFLASH_CARDS && !changed; ++card) {
        if (msx_megaflash_card_mounted(overlay->msx, card) &&
            msx_megaflash_card_writable(overlay->msx, card) !=
                (saved->sd_image_mode == SD_IMAGE_READ_WRITE))
            changed = true;
    }
    if (!changed)
        return true;
    if (msx_eject_megaflash(overlay->msx) != 0) {
        notify_post("Could not restore MegaFlashROM: %s",
                    msx_megaflash_flash_error(overlay->msx));
        return false;
    }
    if (!saved->megaflash || saved_slot < 0)
        return true;
    if (config_megaflash_state_path(
            saved, state_path, sizeof(state_path)) != 0)
        return false;
    if ((state_path[0]
         ? msx_load_megaflash_persistent(
               overlay->msx, (unsigned)saved_slot,
               saved->megaflash_rom_path, state_path)
         : msx_load_megaflash(
               overlay->msx, (unsigned)saved_slot,
               saved->megaflash_rom_path)) != 0) {
        notify_post("Could not restore MegaFlashROM image: %s",
                    msx_megaflash_flash_error(overlay->msx));
        return false;
    }
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        if (!saved->megaflash_card_path[card][0])
            continue;
        if (msx_mount_megaflash_card(
                overlay->msx, card,
                saved->megaflash_card_path[card],
                saved->sd_image_mode) != 0) {
            notify_post(
                "Could not restore MegaFlash SD %c: %s",
                'A' + (int)card,
                msx_megaflash_card_error(overlay->msx, card));
            return false;
        }
    }
    return true;
}

static void restore_cassette(Overlay *overlay) {
    const char *current = overlay->config->cassette_path;
    const char *saved = overlay->saved.cassette_path;
    bool should_be_mounted = saved[0] != '\0';
    bool changed =
        strcmp(current, saved) != 0 ||
        msx_cassette_mounted(overlay->msx) != should_be_mounted;

    if (!changed)
        return;
    if (!should_be_mounted) {
        msx_eject_cassette(overlay->msx);
    } else if (msx_load_cassette(overlay->msx, saved) != 0) {
        msx_eject_cassette(overlay->msx);
        notify_post("Could not restore cassette: %s",
                    path_basename(saved));
    }
}

static bool restore_floppies(Overlay *overlay) {
    const Config *saved = &overlay->saved;

    if (msx_eject_drive_a(overlay->msx) != 0) {
        notify_post("Could not restore Floppy A: %s",
                    msx_drive_a_error(overlay->msx));
        return false;
    }
    if (msx_eject_drive_b(overlay->msx) != 0) {
        notify_post("Could not restore Floppy B: %s",
                    msx_drive_b_error(overlay->msx));
        return false;
    }
    if (saved->model != MSX_MODEL_PHILIPS_NMS8250)
        return true;
    if (saved->drive_a_path[0] &&
        msx_mount_drive_a(
            overlay->msx, saved->drive_a_path,
            saved->floppy_image_mode) != 0) {
        notify_post("Could not restore Floppy A image: %s",
                    msx_drive_a_error(overlay->msx));
        return false;
    }
    if (saved->second_drive && saved->drive_b_path[0] &&
        msx_mount_drive_b(
            overlay->msx, saved->drive_b_path,
            saved->floppy_image_mode) != 0) {
        notify_post("Could not restore Floppy B image: %s",
                    msx_drive_b_error(overlay->msx));
        return false;
    }
    return true;
}

static void close_overlay(Overlay *overlay, bool save) {
    if (overlay->state == OVERLAY_STATE_MODEL_TEXT &&
        overlay->display && overlay->display->window)
        SDL_StopTextInput(overlay->display->window);
    if (save) {
        apply_config(overlay);
        if (overlay->dirty) {
            if (overlay->megaflash_state_pending &&
                msx_megaflash_connected(overlay->msx) &&
                msx_flush_megaflash(overlay->msx) != 0) {
                notify_post("Could not stage MegaFlash state: %s",
                            msx_megaflash_flash_error(overlay->msx));
                return;
            }
            if (config_save(overlay->config) != 0) {
                notify_post("Could not save settings");
                if (overlay->megaflash_state_pending)
                    return;
            } else if (overlay->megaflash_state_pending) {
                if (msx_commit_megaflash_state(
                        overlay->msx,
                        overlay->megaflash_pending_state_path,
                        overlay->megaflash_final_state_path) != 0) {
                    if (config_save(&overlay->saved) != 0)
                        notify_post(
                            "Could not restore settings after "
                            "MegaFlash state commit failed");
                    else
                        notify_post(
                            "Could not commit MegaFlash state; "
                            "settings were not saved");
                    return;
                }
                overlay->megaflash_state_pending = false;
                overlay->megaflash_pending_state_path[0] = '\0';
                overlay->megaflash_final_state_path[0] = '\0';
                notify_post("Settings saved");
            } else {
                notify_post("Settings saved");
            }
        }
    } else {
        if (!restore_megaflash(overlay))
            return;
        discard_pending_megaflash_state(overlay);
        if (!restore_sd_mapper(overlay))
            return;
        if (!restore_sunrise(overlay))
            return;
        restore_cartridges(overlay);
        restore_cassette(overlay);
        restore_firmware(overlay);
        *overlay->config = overlay->saved;
        apply_config(overlay);
        if (!restore_floppies(overlay))
            return;
    }
    overlay->visible = false;
    overlay->dirty = false;
    overlay->state = OVERLAY_STATE_MENU;
}

static void change_section(Overlay *overlay, int direction) {
    int section = (int)overlay->section;
    do {
        section += direction;
        if (section < 0)
            section = OVERLAY_SECTION_COUNT - 1;
        if (section >= OVERLAY_SECTION_COUNT)
            section = 0;
    } while (!section_available(overlay, (OverlaySection)section));
    overlay->section = (OverlaySection)section;
    overlay->row = 0;
}

static void change_notification_mode(Config *config) {
    config->notifications = (NotifyMode)(config->notifications + 1);
    if (config->notifications > NOTIFY_MODE_CONSOLE)
        config->notifications = NOTIFY_MODE_OFF;
    notify_set_mode(config->notifications);
}

static int cycle_gif_width(int width) {
    switch (width) {
        case 720: return 540;
        case 540: return 360;
        case 360: return 240;
        case 240: return 180;
        default:  return 720;
    }
}

static int cycle_gif_fps(int fps) {
    switch (fps) {
        case 25: return 20;
        case 20: return 10;
        case 10: return 5;
        default: return 25;
    }
}

static void copy_dirname(char *destination, size_t destination_size,
                         const char *path) {
    char *slash;
    char *backslash;

    snprintf(destination, destination_size, "%s", path ? path : "");
    slash = strrchr(destination, '/');
    backslash = strrchr(destination, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
    if (!slash) {
        destination[0] = '\0';
    } else if (slash == destination) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
}

static void rom_dialog_callback(void *userdata,
                                const char * const *files,
                                int filter) {
    Overlay *overlay = userdata;

    (void)filter;
    if (!files) {
        snprintf(overlay->dialog_error, sizeof(overlay->dialog_error),
                 "%s", SDL_GetError());
        SDL_MemoryBarrierRelease();
        overlay->dialog_failed = true;
    } else if (files[0]) {
        snprintf(overlay->dialog_path, sizeof(overlay->dialog_path),
                 "%s", files[0]);
        SDL_MemoryBarrierRelease();
        overlay->dialog_ready = true;
    } else {
        overlay->dialog_path[0] = '\0';
        SDL_MemoryBarrierRelease();
        overlay->dialog_ready = true;
    }
}

static void open_cartridge_dialog(Overlay *overlay, unsigned slot) {
    static const SDL_DialogFileFilter filters[] = {
        { "MSX cartridge ROMs", "rom;ROM;mx1;MX1;mx2;MX2" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;
    const char *owner =
        config_cartridge_slot_owner(overlay->config, slot);

    if (owner) {
        notify_post("Cartridge slot %u is reserved by %s",
                    slot + 1, owner);
        return;
    }
    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target =
        slot == 0 ? OVERLAY_DIALOG_CARTRIDGE_1
                  : OVERLAY_DIALOG_CARTRIDGE_2;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_cassette_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "MSX cassette images", "cas;CAS" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = OVERLAY_DIALOG_CASSETTE;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_drive_a_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "Raw MSX floppy images", "dsk;DSK" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (!msx_floppy_supported(overlay->msx)) {
        notify_post("Floppy A requires the Philips NMS 8250 model");
        return;
    }
    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = OVERLAY_DIALOG_DRIVE_A;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_drive_b_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "Raw MSX floppy images", "dsk;DSK" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (!overlay->config->second_drive) {
        notify_post("Enable the second floppy in Advanced first");
        return;
    }
    if (!msx_floppy_supported(overlay->msx)) {
        notify_post("Floppy B requires the Philips NMS 8250 model");
        return;
    }
    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = OVERLAY_DIALOG_DRIVE_B;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_sunrise_rom_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "128 KB Sunrise IDE ROM", "rom;ROM" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = OVERLAY_DIALOG_SUNRISE_ROM;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    notify_post("Select the 128 KB Sunrise IDE kernel ROM");
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_sd_mapper_rom_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "128/256 KB SD Mapper V2 ROM", "rom;ROM" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = OVERLAY_DIALOG_SD_MAPPER_ROM;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    notify_post("Select the SD Mapper V2 driver ROM");
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_sd_card_dialog(Overlay *overlay, unsigned card) {
    static const SDL_DialogFileFilter filters[] = {
        { "Raw SD card images", "img;IMG;dsk;DSK;sd;SD" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (card >= MSX_SD_MAPPER_CARDS ||
        overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    if (overlay->state != OVERLAY_STATE_SD_MAPPER_SETUP &&
        !msx_sd_mapper_connected(overlay->msx)) {
        notify_post("Connect SD Mapper V2 before inserting a card");
        return;
    }
    overlay->dialog_target =
        card ? OVERLAY_DIALOG_SD_CARD_B :
               OVERLAY_DIALOG_SD_CARD_A;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_megaflash_rom_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "MegaFlashROM SCC+ SD flash image", "rom;ROM;flash;FLASH" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = OVERLAY_DIALOG_MEGAFLASH_ROM;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    notify_post("Select a MegaFlashROM image up to 8 MiB");
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_megaflash_card_dialog(
    Overlay *overlay, unsigned card) {
    static const SDL_DialogFileFilter filters[] = {
        { "Raw SD card images", "img;IMG;dsk;DSK;sd;SD" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (card >= MSX_MEGAFLASH_CARDS ||
        overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    if (overlay->state != OVERLAY_STATE_MEGAFLASH_SETUP &&
        !msx_megaflash_connected(overlay->msx)) {
        notify_post(
            "Connect MegaFlashROM SCC+ SD before inserting a card");
        return;
    }
    overlay->dialog_target =
        card ? OVERLAY_DIALOG_MEGAFLASH_SD_B :
               OVERLAY_DIALOG_MEGAFLASH_SD_A;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void open_ide_image_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "Raw IDE disk images", "img;IMG;dsk;DSK;hdd;HDD" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (overlay->state != OVERLAY_STATE_SUNRISE_SETUP &&
        !msx_sunrise_connected(overlay->msx)) {
        notify_post("Connect Sunrise IDE before mounting a disk");
        return;
    }
    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = OVERLAY_DIALOG_IDE_IMAGE;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static char *model_firmware_field(Overlay *overlay,
                                  OverlayDialogTarget target) {
    switch (target) {
        case OVERLAY_DIALOG_MODEL_BIOS:
            return overlay->model_edit.bios_path;
        case OVERLAY_DIALOG_MODEL_LOGO:
            return overlay->model_edit.logo_path;
        case OVERLAY_DIALOG_MODEL_SUBROM:
            return overlay->model_edit.subrom_path;
        case OVERLAY_DIALOG_MODEL_DISK_ROM:
            return overlay->model_edit.disk_rom_path;
        default:
            break;
    }
    return NULL;
}

static void open_model_firmware_dialog(Overlay *overlay,
                                       OverlayDialogTarget target) {
    static const SDL_DialogFileFilter bios_filters[] = {
        { "32 KB MSX BIOS ROM", "rom;ROM" },
        { "All files", "*" },
    };
    static const SDL_DialogFileFilter extension_filters[] = {
        { "16 KB MSX firmware ROM", "rom;ROM" },
        { "All files", "*" },
    };
    const SDL_DialogFileFilter *filters =
        target == OVERLAY_DIALOG_MODEL_BIOS
        ? bios_filters : extension_filters;
    char *field = model_firmware_field(overlay, target);
    char location[PATH_MAX];

    if (!field || overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    location[0] = '\0';
    if (field[0])
        copy_dirname(location, sizeof(location), field);
    if (!location[0])
        snprintf(location, sizeof(location), "%s",
                 overlay->config->last_media_dir);
    overlay->dialog_target = target;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(rom_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2,
                           location[0] ? location : NULL, false);
}

static bool firmware_file_has_size(const char *path, long expected_size) {
    FILE *file;
    long size;

    if (!path || !path[0])
        return false;
    file = fopen(path, "rb");
    if (!file)
        return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    size = ftell(file);
    fclose(file);
    return size == expected_size;
}

static bool sd_mapper_rom_file_is_valid(const char *path) {
    return firmware_file_has_size(
               path, MSX_SD_MAPPER_DRIVER_SIZE) ||
           firmware_file_has_size(
               path, MSX_SD_MAPPER_ROM_SIZE);
}

static bool megaflash_rom_file_is_valid(const char *path) {
    FILE *file;
    long size;

    if (!path || !path[0])
        return false;
    file = fopen(path, "rb");
    if (!file)
        return false;
    size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    fclose(file);
    return size > 0 &&
           size <= (long)MSX_MEGAFLASH_FLASH_SIZE;
}

static bool sd_image_file_is_valid(const char *path) {
    SdCard card;
    bool valid;

    sd_card_init(&card);
    valid = sd_card_mount(
        &card, path, SD_IMAGE_READ_ONLY) == 0;
    sd_card_destroy(&card);
    return valid;
}

static bool ide_image_file_is_valid(const char *path) {
    AtaDevice ata;
    bool valid;

    ata_init(&ata);
    valid = ata_mount(&ata, path) == 0;
    ata_destroy(&ata);
    return valid;
}

static void select_machine(Overlay *overlay) {
    const ModelDefinition *definition =
        &overlay->models->entries[overlay->machine_row];
    const MsxProfile *profile = msx_profile(definition->hardware);
    Config *config = overlay->config;
    Config target_config = *config;
    char target_rtc_path[PATH_MAX];
    int target_ram = msx_default_ram_kb(definition->hardware);
    bool rtc_changed;

    target_config.model = definition->hardware;
    snprintf(target_config.machine_id, sizeof(target_config.machine_id),
             "%s", definition->id);
    target_config.memory_kb = target_ram;
    if (config_rtc_path(
            &target_config, target_rtc_path,
            sizeof(target_rtc_path)) != 0) {
        notify_post("RTC persistence path is too long for %s",
                    definition->name);
        return;
    }
    rtc_changed = strcmp(
        msx_rtc_persistence_path(overlay->msx), target_rtc_path) != 0;

    if (!firmware_file_has_size(definition->bios_path, MSX_BIOS_SIZE)) {
        notify_post("Invalid BIOS for %s; edit the machine model",
                    definition->name);
        return;
    }
    if (definition->logo_path[0] &&
        !firmware_file_has_size(definition->logo_path, MSX_LOGO_SIZE)) {
        notify_post("Invalid logo ROM for %s; edit the machine model",
                    definition->name);
        return;
    }
    if ((profile->requires_subrom || definition->subrom_path[0]) &&
        !firmware_file_has_size(
            definition->subrom_path, MSX_SUBROM_SIZE)) {
        notify_post("Invalid Sub-ROM for %s; edit the machine model",
                    definition->name);
        return;
    }
    if (definition->disk_rom_path[0] &&
        !firmware_file_has_size(
            definition->disk_rom_path, MSX_DISK_ROM_SIZE)) {
        notify_post("Invalid disk ROM for %s; edit the machine model",
                    definition->name);
        return;
    }
    if (rtc_changed && msx_set_rtc_persistence(
            overlay->msx, "", rtc_host_seconds()) != 0) {
        notify_post("Could not save RTC CMOS: %s",
                    msx_rtc_persistence_error(overlay->msx));
        return;
    }
    if (msx_load_firmware_set(
            overlay->msx, definition->bios_path,
            definition->logo_path, definition->subrom_path,
            definition->disk_rom_path) != 0) {
        if (rtc_changed)
            (void)sync_rtc_persistence(overlay);
        notify_post("Could not load %s firmware; check ROM sizes",
                    definition->name);
        return;
    }

    config->model = definition->hardware;
    snprintf(config->machine_id, sizeof(config->machine_id),
              "%s", definition->id);
    config->memory_kb = target_ram;
    snprintf(config->bios_path, sizeof(config->bios_path), "%s",
             definition->bios_path);
    snprintf(config->logo_path, sizeof(config->logo_path), "%s",
             definition->logo_path);
    snprintf(config->subrom_path, sizeof(config->subrom_path), "%s",
             definition->subrom_path);
    snprintf(config->disk_rom_path, sizeof(config->disk_rom_path), "%s",
             definition->disk_rom_path);
    overlay->dirty = true;
    overlay->state = OVERLAY_STATE_MENU;
    apply_config(overlay);
    display_prepare_scaffold(overlay->display, overlay->msx);
    display_set_title(overlay->display, overlay->msx,
                      definition->name);
    notify_post("%s firmware loaded: %s", definition->name,
                 path_basename(config->bios_path));
}

static void change_cartridge_mapper(Overlay *overlay, unsigned slot) {
    MsxCartridgeMapper previous =
        overlay->config->cartridge_mapper[slot];
    MsxCartridgeMapper next =
        (MsxCartridgeMapper)(previous + 1);
    const MsxCartridge *cartridge;
    const char *owner =
        config_cartridge_slot_owner(overlay->config, slot);

    if (owner) {
        notify_post("Cartridge slot %u is reserved by %s",
                    slot + 1, owner);
        return;
    }
    if (next >= MSX_CART_MAPPER_COUNT)
        next = MSX_CART_MAPPER_AUTO;
    cartridge = msx_get_cartridge(overlay->msx, slot);
    if (cartridge && cartridge->loaded &&
        msx_set_cartridge_mapper(overlay->msx, slot, next) != 0) {
        notify_post("Cartridge %u is too large for %s",
                    slot + 1,
                    msx_cartridge_mapper_display_name(next));
        return;
    }
    overlay->config->cartridge_mapper[slot] = next;
    overlay->dirty = true;
    if (cartridge && cartridge->loaded) {
        const MsxCartridge *updated =
            msx_get_cartridge(overlay->msx, slot);
        if (next == MSX_CART_MAPPER_AUTO)
            notify_post("Cartridge %u mapper: Auto (%s)",
                        slot + 1,
                        msx_cartridge_mapper_display_name(updated->mapper));
        else
            notify_post("Cartridge %u mapper: %s", slot + 1,
                        msx_cartridge_mapper_display_name(next));
    }
}

static void model_editor_unique_id(const ModelCatalog *catalog,
                                   const char *base,
                                   char *id, size_t id_size) {
    int suffix = 1;

    snprintf(id, id_size, "%s", base);
    while (model_catalog_find(catalog, id)) {
        ++suffix;
        snprintf(id, id_size, "%.*s-%d",
                 (int)id_size - 12, base, suffix);
    }
}

static void begin_model_editor(Overlay *overlay) {
    overlay->model_editor_row = (int)model_catalog_index(
        overlay->models, overlay->config->machine_id);
    if (overlay->model_editor_row >= (int)overlay->models->count)
        overlay->model_editor_row = 0;
    overlay->model_editor_error[0] = '\0';
    overlay->state = OVERLAY_STATE_MODEL_LIST;
}

static void begin_model_edit(Overlay *overlay, int index,
                             bool duplicate) {
    memset(&overlay->model_edit, 0, sizeof(overlay->model_edit));
    overlay->model_edit_index = -1;
    overlay->model_edit_field = MODEL_FIELD_ID;
    overlay->model_editor_error[0] = '\0';

    if (index >= 0 && index < (int)overlay->models->count) {
        overlay->model_edit = overlay->models->entries[index];
        if (!duplicate) {
            overlay->model_edit_index = index;
        } else {
            char id[MODEL_ID_MAX];
            char name[MODEL_NAME_MAX];

            snprintf(name, sizeof(name), "%.*s copy",
                     (int)sizeof(name) - 6,
                     overlay->model_edit.name);
            snprintf(overlay->model_edit.name,
                     sizeof(overlay->model_edit.name), "%s", name);
            snprintf(id, sizeof(id), "%s-copy",
                     overlay->model_edit.id);
            model_editor_unique_id(overlay->models, id,
                                   overlay->model_edit.id,
                                   sizeof(overlay->model_edit.id));
        }
    } else {
        model_editor_unique_id(overlay->models, "new-model",
                               overlay->model_edit.id,
                               sizeof(overlay->model_edit.id));
        snprintf(overlay->model_edit.name,
                 sizeof(overlay->model_edit.name), "New model");
        overlay->model_edit.hardware = MSX_MODEL_GENERIC_MSX1;
    }
    overlay->state = OVERLAY_STATE_MODEL_EDIT;
}

static char *model_edit_text_field(Overlay *overlay, int field,
                                   size_t *capacity) {
    if (capacity)
        *capacity = 0;
    switch (field) {
        case MODEL_FIELD_ID:
            if (capacity)
                *capacity = sizeof(overlay->model_edit.id);
            return overlay->model_edit.id;
        case MODEL_FIELD_NAME:
            if (capacity)
                *capacity = sizeof(overlay->model_edit.name);
            return overlay->model_edit.name;
        case MODEL_FIELD_BIOS:
            if (capacity)
                *capacity = sizeof(overlay->model_edit.bios_path);
            return overlay->model_edit.bios_path;
        case MODEL_FIELD_LOGO:
            if (capacity)
                *capacity = sizeof(overlay->model_edit.logo_path);
            return overlay->model_edit.logo_path;
        case MODEL_FIELD_SUBROM:
            if (capacity)
                *capacity = sizeof(overlay->model_edit.subrom_path);
            return overlay->model_edit.subrom_path;
        case MODEL_FIELD_DISK_ROM:
            if (capacity)
                *capacity = sizeof(overlay->model_edit.disk_rom_path);
            return overlay->model_edit.disk_rom_path;
        default:
            break;
    }
    return NULL;
}

static void begin_model_text_edit(Overlay *overlay, int field) {
    char *source = model_edit_text_field(overlay, field, NULL);

    if (!source)
        return;
    snprintf(overlay->model_text, sizeof(overlay->model_text),
             "%s", source);
    overlay->model_text_field = field;
    overlay->state = OVERLAY_STATE_MODEL_TEXT;
    if (overlay->display && overlay->display->window)
        SDL_StartTextInput(overlay->display->window);
}

static void finish_model_text_edit(Overlay *overlay, bool commit) {
    if (commit) {
        size_t capacity;
        char *destination = model_edit_text_field(
            overlay, overlay->model_text_field, &capacity);

        if (destination && capacity)
            snprintf(destination, capacity, "%s",
                     overlay->model_text);
    }
    if (overlay->display && overlay->display->window)
        SDL_StopTextInput(overlay->display->window);
    overlay->state = OVERLAY_STATE_MODEL_EDIT;
}

static void pop_utf8_character(char *text) {
    size_t length = strlen(text);

    if (!length)
        return;
    do {
        --length;
    } while (length &&
             (((unsigned char)text[length] & 0xc0) == 0x80));
    text[length] = '\0';
}

static bool validate_model_edit(Overlay *overlay) {
    ModelDefinition checked = overlay->model_edit;
    size_t replaced =
        overlay->model_edit_index >= 0
        ? (size_t)overlay->model_edit_index : (size_t)-1;

    if (!model_definition_validate(
            overlay->models, &checked, replaced, false,
            overlay->model_editor_error,
            sizeof(overlay->model_editor_error)))
        return false;
    if (overlay->model_edit_index >= 0) {
        const ModelDefinition *original =
            &overlay->models->entries[overlay->model_edit_index];

        if (strcmp(checked.bios_path, original->bios_path) == 0)
            checked.bios_path[0] = '\0';
        if (strcmp(checked.logo_path, original->logo_path) == 0)
            checked.logo_path[0] = '\0';
        if (strcmp(checked.subrom_path, original->subrom_path) == 0)
            checked.subrom_path[0] = '\0';
        if (strcmp(checked.disk_rom_path,
                   original->disk_rom_path) == 0)
            checked.disk_rom_path[0] = '\0';
    }
    return model_definition_validate(
        overlay->models, &checked, replaced, true,
        overlay->model_editor_error,
        sizeof(overlay->model_editor_error));
}

static bool install_model_catalog(Overlay *overlay,
                                  ModelCatalog *updated,
                                  const char *selected_id) {
    char edit_path[PATH_MAX];

    snprintf(edit_path, sizeof(edit_path), "%s",
             overlay->models->edit_path);
    if (!edit_path[0])
        model_catalog_user_path(edit_path, sizeof(edit_path));
    if (model_catalog_save(updated, edit_path) != 0) {
        snprintf(overlay->model_editor_error,
                 sizeof(overlay->model_editor_error),
                 "Could not save the per-user catalogue");
        return false;
    }
    if (model_catalog_load(updated, edit_path) != 0) {
        snprintf(overlay->model_editor_error,
                 sizeof(overlay->model_editor_error),
                 "Saved catalogue could not be reloaded");
        return false;
    }
    snprintf(updated->edit_path, sizeof(updated->edit_path),
             "%s", edit_path);
    *overlay->models = *updated;
    overlay->model_editor_row = (int)model_catalog_index(
        overlay->models, selected_id);
    if (overlay->model_editor_row >= (int)overlay->models->count)
        overlay->model_editor_row = 0;
    overlay->model_editor_error[0] = '\0';
    return true;
}

static void save_model_edit(Overlay *overlay) {
    ModelCatalog *updated;
    char old_id[MODEL_ID_MAX] = "";
    bool current_renamed = false;

    if (!validate_model_edit(overlay))
        return;
    if (overlay->model_edit_index < 0 &&
        overlay->models->count >= MODEL_CATALOG_MAX) {
        snprintf(overlay->model_editor_error,
                 sizeof(overlay->model_editor_error),
                 "The catalogue is full");
        return;
    }
    updated = malloc(sizeof(*updated));
    if (!updated) {
        snprintf(overlay->model_editor_error,
                 sizeof(overlay->model_editor_error),
                 "Not enough memory to save the catalogue");
        return;
    }
    *updated = *overlay->models;
    if (overlay->model_edit_index >= 0) {
        snprintf(old_id, sizeof(old_id), "%s",
                 updated->entries[overlay->model_edit_index].id);
        updated->entries[overlay->model_edit_index] =
            overlay->model_edit;
    } else {
        updated->entries[updated->count++] = overlay->model_edit;
    }
    if (install_model_catalog(
            overlay, updated, overlay->model_edit.id)) {
        current_renamed =
            old_id[0] &&
            strcmp(old_id, overlay->model_edit.id) != 0 &&
            strcmp(overlay->config->machine_id, old_id) == 0;
        if (current_renamed) {
            snprintf(overlay->config->machine_id,
                     sizeof(overlay->config->machine_id),
                     "%s", overlay->model_edit.id);
            overlay->dirty = true;
        }
        overlay->state = OVERLAY_STATE_MODEL_LIST;
        notify_post("Machine catalogue saved; select a model to apply it");
    }
    free(updated);
}

static void delete_model_edit(Overlay *overlay) {
    ModelCatalog *updated;
    char selected_id[MODEL_ID_MAX];
    size_t index = (size_t)overlay->model_editor_row;

    if (overlay->models->count <= 1 ||
        index >= overlay->models->count)
        return;
    updated = malloc(sizeof(*updated));
    if (!updated) {
        snprintf(overlay->model_editor_error,
                 sizeof(overlay->model_editor_error),
                 "Not enough memory to save the catalogue");
        overlay->state = OVERLAY_STATE_MODEL_LIST;
        return;
    }
    *updated = *overlay->models;
    for (size_t i = index + 1; i < updated->count; ++i)
        updated->entries[i - 1] = updated->entries[i];
    --updated->count;
    if (index >= updated->count)
        index = updated->count - 1;
    snprintf(selected_id, sizeof(selected_id), "%s",
             updated->entries[index].id);
    if (install_model_catalog(overlay, updated, selected_id)) {
        overlay->state = OVERLAY_STATE_MODEL_LIST;
        notify_post("Machine model deleted");
    } else {
        overlay->state = OVERLAY_STATE_MODEL_LIST;
    }
    free(updated);
}

static bool toggle_cartridge_extension(Overlay *overlay,
                                       bool *enabled,
                                       const char *name) {
    Config before = *overlay->config;

    if (*enabled) {
        *enabled = false;
        reconcile_extension_slots(overlay);
        notify_post("%s disconnected", name);
        return true;
    }
    if (config_cartridge_extension_count(overlay->config) >=
        MSX_CARTRIDGE_SLOTS) {
        notify_post("Cannot connect %s: both cartridge slots are in use",
                    name);
        return false;
    }
    *enabled = true;
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        const char *old_owner =
            config_cartridge_slot_owner(&before, slot);
        const char *new_owner =
            config_cartridge_slot_owner(overlay->config, slot);

        if (old_owner || !new_owner)
            continue;
        if (overlay->config->cartridge_path[slot][0] ||
            msx_get_cartridge(overlay->msx, slot)->loaded) {
            msx_eject_cartridge(overlay->msx, slot);
            overlay->config->cartridge_path[slot][0] = '\0';
            notify_post("%s connected in cartridge slot %u; "
                        "mounted cartridge ejected",
                        new_owner, slot + 1);
        } else {
            notify_post("%s connected in cartridge slot %u",
                        new_owner, slot + 1);
        }
    }
    reconcile_extension_slots(overlay);
    return true;
}

static void begin_extension_setup(Overlay *overlay, bool editing) {
    overlay->extension_setup_editing = editing;
    snprintf(overlay->extension_setup_media_dir,
             sizeof(overlay->extension_setup_media_dir), "%s",
             overlay->config->last_media_dir);
}

static void cancel_extension_setup(Overlay *overlay) {
    snprintf(overlay->config->last_media_dir,
             sizeof(overlay->config->last_media_dir), "%s",
             overlay->extension_setup_media_dir);
    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        overlay->dialog_discard = true;
    overlay->state = OVERLAY_STATE_MENU;
}

static void begin_sunrise_setup(Overlay *overlay, bool editing) {
    begin_extension_setup(overlay, editing);
    overlay->sunrise_setup_row = SUNRISE_SETUP_FIRMWARE;
    snprintf(overlay->pending_sunrise_rom_path,
             sizeof(overlay->pending_sunrise_rom_path), "%s",
             overlay->config->sunrise_rom_path);
    snprintf(overlay->pending_ide_image_path,
             sizeof(overlay->pending_ide_image_path), "%s",
             overlay->config->ide_image_path);
    overlay->state = OVERLAY_STATE_SUNRISE_SETUP;
}

static bool validate_sunrise_settings(const char *rom_path,
                                      const char *image_path) {
    if (!firmware_file_has_size(rom_path, MSX_SUNRISE_ROM_SIZE)) {
        notify_post("Sunrise IDE needs an exact 128 KB ROM");
        return false;
    }
    if (image_path && image_path[0] &&
        !ide_image_file_is_valid(image_path)) {
        notify_post("Sunrise IDE needs a raw 512-byte-sector disk image");
        return false;
    }
    return true;
}

static void store_sunrise_settings(Config *config,
                                   const char *rom_path,
                                   const char *image_path) {
    snprintf(config->sunrise_rom_path,
             sizeof(config->sunrise_rom_path), "%s", rom_path);
    snprintf(config->ide_image_path,
             sizeof(config->ide_image_path), "%s", image_path);
}

static bool connect_sunrise(Overlay *overlay, const char *rom_path,
                            const char *image_path,
                            AtaImageMode image_mode) {
    Config *config = overlay->config;
    char selected_rom[PATH_MAX];
    char selected_image[PATH_MAX];
    int slot;

    snprintf(selected_rom, sizeof(selected_rom), "%s",
             rom_path ? rom_path : "");
    snprintf(selected_image, sizeof(selected_image), "%s",
             image_path ? image_path : "");
    if (!validate_sunrise_settings(selected_rom, selected_image))
        return false;
    if (!toggle_cartridge_extension(
            overlay, &config->sunrise_ide, "Sunrise IDE"))
        return false;
    slot = cartridge_extension_slot(config, "Sunrise IDE");
    if (slot < 0 || msx_load_sunrise_ide(
            overlay->msx, (unsigned)slot, selected_rom) != 0) {
        config->sunrise_ide = false;
        reconcile_extension_slots(overlay);
        notify_post("Could not load Sunrise IDE ROM: %s",
                    path_basename(selected_rom));
        return false;
    }
    if (selected_image[0] &&
        msx_mount_sunrise_disk_mode(
            overlay->msx, selected_image, image_mode) != 0) {
        char mount_error[192];

        snprintf(mount_error, sizeof(mount_error), "%s",
                 msx_sunrise_disk_error(overlay->msx));
        (void)msx_eject_sunrise_ide(overlay->msx);
        config->sunrise_ide = false;
        reconcile_extension_slots(overlay);
        notify_post("Could not mount IDE image: %s",
                    mount_error[0] ? mount_error :
                    path_basename(selected_image));
        return false;
    }
    store_sunrise_settings(config, selected_rom, selected_image);
    config->ide_image_mode = image_mode;
    if (selected_image[0])
        notify_post("Sunrise IDE connected with %s disk: %s",
                    image_mode == ATA_IMAGE_READ_WRITE
                    ? "read/write" : "read-only",
                    path_basename(selected_image));
    else
        notify_post("Sunrise IDE connected without a disk");
    return true;
}

static void finish_sunrise_setup(Overlay *overlay) {
    Config *config = overlay->config;
    const char *rom_path = overlay->pending_sunrise_rom_path;
    const char *image_path = overlay->pending_ide_image_path;

    if (overlay->extension_setup_editing) {
        bool changed =
            strcmp(config->sunrise_rom_path, rom_path) != 0 ||
            strcmp(config->ide_image_path, image_path) != 0;

        if (!validate_sunrise_settings(rom_path, image_path))
            return;
        if (changed && config->sunrise_ide &&
            msx_replace_sunrise_ide(
                overlay->msx, rom_path, image_path,
                config->ide_image_mode) != 0) {
            notify_post("Could not apply Sunrise IDE settings");
            return;
        }
        if (changed) {
            store_sunrise_settings(config, rom_path, image_path);
            overlay->dirty = true;
            apply_config(overlay);
            notify_post("Sunrise IDE settings updated%s",
                        config->sunrise_ide ? " and reconnected" : "");
        }
        overlay->state = OVERLAY_STATE_MENU;
        return;
    }
    if (!connect_sunrise(
            overlay, rom_path, image_path, config->ide_image_mode))
        return;
    overlay->dirty = true;
    overlay->state = OVERLAY_STATE_MENU;
    apply_config(overlay);
}

static bool disconnect_sunrise(Overlay *overlay) {
    if (msx_eject_sunrise_ide(overlay->msx) != 0) {
        notify_post("Could not disconnect Sunrise IDE: %s",
                    msx_sunrise_disk_error(overlay->msx));
        return false;
    }
    (void)toggle_cartridge_extension(
        overlay, &overlay->config->sunrise_ide, "Sunrise IDE");
    return true;
}

static void begin_sd_mapper_setup(Overlay *overlay, bool editing) {
    Config *config = overlay->config;

    begin_extension_setup(overlay, editing);
    overlay->sd_mapper_setup_row = SD_MAPPER_SETUP_FIRMWARE;
    snprintf(overlay->pending_sd_mapper_rom_path,
             sizeof(overlay->pending_sd_mapper_rom_path), "%s",
             config->sd_mapper_rom_path);
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card) {
        snprintf(overlay->pending_sd_card_path[card],
                 sizeof(overlay->pending_sd_card_path[card]), "%s",
                 config->sd_card_path[card]);
    }
    overlay->pending_sd_mapper_ram = config->sd_mapper_ram;
    overlay->pending_sd_mapper_alternate_driver =
        config->sd_mapper_alternate_driver;
    overlay->state = OVERLAY_STATE_SD_MAPPER_SETUP;
}

static bool validate_sd_mapper_settings(
    const char *rom_path, const char *card_a_path,
    const char *card_b_path) {
    const char *cards[MSX_SD_MAPPER_CARDS] = {
        card_a_path ? card_a_path : "",
        card_b_path ? card_b_path : ""
    };

    if (!sd_mapper_rom_file_is_valid(rom_path)) {
        notify_post("SD Mapper V2 needs an exact 128 or 256 KB ROM");
        return false;
    }
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card) {
        if (cards[card][0] && !sd_image_file_is_valid(cards[card])) {
            notify_post("SD Card %c must use complete 512-byte sectors",
                        'A' + (int)card);
            return false;
        }
    }
    return true;
}

static void store_sd_mapper_settings(
    Config *config, const char *rom_path,
    const char *card_a_path, const char *card_b_path,
    bool mapper_ram, bool alternate_driver) {
    const char *cards[MSX_SD_MAPPER_CARDS] = {
        card_a_path ? card_a_path : "",
        card_b_path ? card_b_path : ""
    };

    snprintf(config->sd_mapper_rom_path,
             sizeof(config->sd_mapper_rom_path), "%s", rom_path);
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card)
        snprintf(config->sd_card_path[card],
                 sizeof(config->sd_card_path[card]), "%s", cards[card]);
    config->sd_mapper_ram = mapper_ram;
    config->sd_mapper_alternate_driver = alternate_driver;
}

static bool connect_sd_mapper(
    Overlay *overlay, const char *rom_path,
    const char *card_a_path, const char *card_b_path,
    SdImageMode image_mode, bool mapper_ram,
    bool alternate_driver) {
    Config *config = overlay->config;
    const char *cards[MSX_SD_MAPPER_CARDS] = {
        card_a_path ? card_a_path : "",
        card_b_path ? card_b_path : ""
    };
    char selected_rom[PATH_MAX];
    int slot;

    snprintf(selected_rom, sizeof(selected_rom), "%s",
             rom_path ? rom_path : "");
    if (!validate_sd_mapper_settings(
            selected_rom, cards[0], cards[1]))
        return false;
    if (!toggle_cartridge_extension(
            overlay, &config->sd_mapper, "SD Mapper V2"))
        return false;
    slot = cartridge_extension_slot(config, "SD Mapper V2");
    if (slot < 0 || msx_load_sd_mapper(
            overlay->msx, (unsigned)slot, selected_rom) != 0) {
        config->sd_mapper = false;
        reconcile_extension_slots(overlay);
        notify_post("Could not load SD Mapper V2 ROM: %s",
                    path_basename(selected_rom));
        return false;
    }
    msx_sd_mapper_set_ram_enabled(overlay->msx, mapper_ram);
    msx_sd_mapper_set_alternate_driver(
        overlay->msx, alternate_driver);
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card) {
        if (!cards[card][0])
            continue;
        if (msx_mount_sd_card(
                overlay->msx, card, cards[card], image_mode) == 0)
            continue;
        {
            char error[SD_CARD_ERROR_MAX];

            snprintf(error, sizeof(error), "%s",
                     msx_sd_card_error(overlay->msx, card));
            (void)msx_eject_sd_mapper(overlay->msx);
            config->sd_mapper = false;
            reconcile_extension_slots(overlay);
            notify_post("Could not mount SD Card %c: %s",
                        'A' + (int)card,
                        error[0] ? error :
                        path_basename(cards[card]));
            return false;
        }
    }
    store_sd_mapper_settings(
        config, selected_rom, cards[0], cards[1],
        mapper_ram, alternate_driver);
    config->sd_image_mode = image_mode;
    notify_post("SD Mapper V2 connected in cartridge slot %d",
                slot + 1);
    return true;
}

static void finish_sd_mapper_setup(Overlay *overlay) {
    Config *config = overlay->config;
    const char *rom_path = overlay->pending_sd_mapper_rom_path;
    const char *card_a_path = overlay->pending_sd_card_path[0];
    const char *card_b_path = overlay->pending_sd_card_path[1];

    if (overlay->extension_setup_editing) {
        bool changed =
            strcmp(config->sd_mapper_rom_path, rom_path) != 0 ||
            strcmp(config->sd_card_path[0], card_a_path) != 0 ||
            strcmp(config->sd_card_path[1], card_b_path) != 0 ||
            config->sd_mapper_ram != overlay->pending_sd_mapper_ram ||
            config->sd_mapper_alternate_driver !=
                overlay->pending_sd_mapper_alternate_driver;

        if (!validate_sd_mapper_settings(
                rom_path, card_a_path, card_b_path))
            return;
        if (changed && config->sd_mapper &&
            msx_replace_sd_mapper(
                overlay->msx, rom_path, card_a_path, card_b_path,
                config->sd_image_mode,
                overlay->pending_sd_mapper_ram,
                overlay->pending_sd_mapper_alternate_driver) != 0) {
            notify_post("Could not apply SD Mapper V2 settings");
            return;
        }
        if (changed) {
            store_sd_mapper_settings(
                config, rom_path, card_a_path, card_b_path,
                overlay->pending_sd_mapper_ram,
                overlay->pending_sd_mapper_alternate_driver);
            overlay->dirty = true;
            apply_config(overlay);
            notify_post("SD Mapper V2 settings updated%s",
                        config->sd_mapper ? " and reconnected" : "");
        }
        overlay->state = OVERLAY_STATE_MENU;
        return;
    }
    if (!connect_sd_mapper(
            overlay, rom_path, card_a_path, card_b_path,
            config->sd_image_mode,
            overlay->pending_sd_mapper_ram,
            overlay->pending_sd_mapper_alternate_driver))
        return;
    overlay->dirty = true;
    overlay->state = OVERLAY_STATE_MENU;
    apply_config(overlay);
}

static bool disconnect_sd_mapper(Overlay *overlay) {
    if (msx_eject_sd_mapper(overlay->msx) != 0) {
        const char *error =
            msx_sd_card_has_error(overlay->msx, 0)
            ? msx_sd_card_error(overlay->msx, 0)
            : msx_sd_card_error(overlay->msx, 1);

        notify_post("Could not disconnect SD Mapper V2: %s",
                    error);
        return false;
    }
    (void)toggle_cartridge_extension(
        overlay, &overlay->config->sd_mapper, "SD Mapper V2");
    return true;
}

static void begin_megaflash_setup(Overlay *overlay, bool editing) {
    Config *config = overlay->config;

    begin_extension_setup(overlay, editing);
    overlay->megaflash_setup_row = MEGAFLASH_SETUP_FIRMWARE;
    snprintf(overlay->pending_megaflash_rom_path,
             sizeof(overlay->pending_megaflash_rom_path), "%s",
             config->megaflash_rom_path);
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        snprintf(overlay->pending_megaflash_card_path[card],
                 sizeof(overlay->pending_megaflash_card_path[card]),
                 "%s", config->megaflash_card_path[card]);
    }
    overlay->state = OVERLAY_STATE_MEGAFLASH_SETUP;
}

static bool validate_megaflash_settings(
    const char *rom_path, const char *card_a_path,
    const char *card_b_path) {
    const char *cards[MSX_MEGAFLASH_CARDS] = {
        card_a_path ? card_a_path : "",
        card_b_path ? card_b_path : ""
    };

    if (!megaflash_rom_file_is_valid(rom_path)) {
        notify_post("MegaFlashROM SCC+ SD needs an image up to 8 MiB");
        return false;
    }
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        if (cards[card][0] && !sd_image_file_is_valid(cards[card])) {
            notify_post("MegaFlash SD %c must use complete 512-byte sectors",
                        'A' + (int)card);
            return false;
        }
    }
    return true;
}

static void store_megaflash_settings(
    Config *config, const char *rom_path,
    const char *card_a_path, const char *card_b_path) {
    const char *cards[MSX_MEGAFLASH_CARDS] = {
        card_a_path ? card_a_path : "",
        card_b_path ? card_b_path : ""
    };

    snprintf(config->megaflash_rom_path,
             sizeof(config->megaflash_rom_path), "%s", rom_path);
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card)
        snprintf(config->megaflash_card_path[card],
                 sizeof(config->megaflash_card_path[card]),
                 "%s", cards[card]);
}

static bool connect_megaflash(
    Overlay *overlay, const char *rom_path,
    const char *card_a_path, const char *card_b_path,
    SdImageMode image_mode) {
    Config *config = overlay->config;
    const char *cards[MSX_MEGAFLASH_CARDS] = {
        card_a_path ? card_a_path : "",
        card_b_path ? card_b_path : ""
    };
    char state_path[PATH_MAX];
    const char *load_state_path;
    int slot;

    if (!validate_megaflash_settings(
            rom_path, cards[0], cards[1]))
        return false;
    if (!toggle_cartridge_extension(
            overlay, &config->megaflash,
            "MegaFlashROM SCC+ SD"))
        return false;
    slot = cartridge_extension_slot(
        config, "MegaFlashROM SCC+ SD");
    if (config_megaflash_state_path(
            config, state_path, sizeof(state_path)) != 0) {
        config->megaflash = false;
        reconcile_extension_slots(overlay);
        notify_post("MegaFlash persistent state path is too long");
        return false;
    }
    load_state_path = overlay->megaflash_state_pending
        ? overlay->megaflash_pending_state_path : state_path;
    if (slot < 0 ||
        (load_state_path[0]
         ? msx_load_megaflash_persistent(
               overlay->msx, (unsigned)slot,
               rom_path, load_state_path)
         : msx_load_megaflash(
               overlay->msx, (unsigned)slot,
               rom_path)) != 0) {
        config->megaflash = false;
        reconcile_extension_slots(overlay);
        notify_post("Could not load MegaFlashROM image: %s",
                    msx_megaflash_flash_error(overlay->msx));
        return false;
    }
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        if (!cards[card][0])
            continue;
        if (msx_mount_megaflash_card(
                overlay->msx, card, cards[card], image_mode) == 0)
            continue;
        {
            char error[SD_CARD_ERROR_MAX];

            snprintf(error, sizeof(error), "%s",
                     msx_megaflash_card_error(
                         overlay->msx, card));
            (void)msx_eject_megaflash(overlay->msx);
            config->megaflash = false;
            reconcile_extension_slots(overlay);
            notify_post("Could not mount MegaFlash SD %c: %s",
                        'A' + (int)card,
                        error[0] ? error :
                        path_basename(cards[card]));
            return false;
        }
    }
    store_megaflash_settings(
        config, rom_path, cards[0], cards[1]);
    config->sd_image_mode = image_mode;
    notify_post(
        "MegaFlashROM SCC+ SD connected in cartridge slot %d",
        slot + 1);
    return true;
}

static void finish_megaflash_setup(Overlay *overlay) {
    Config *config = overlay->config;
    const char *rom_path = overlay->pending_megaflash_rom_path;
    const char *card_a_path = overlay->pending_megaflash_card_path[0];
    const char *card_b_path = overlay->pending_megaflash_card_path[1];
    bool setup_reseed_prepared = false;

    if (overlay->extension_setup_editing) {
        bool image_changed =
            strcmp(config->megaflash_rom_path, rom_path) != 0;
        bool reseed_pending =
            strcmp(overlay->saved.megaflash_rom_path, rom_path) != 0;
        bool changed =
            image_changed ||
            strcmp(config->megaflash_card_path[0], card_a_path) != 0 ||
            strcmp(config->megaflash_card_path[1], card_b_path) != 0;
        char state_path[PATH_MAX];
        char pending_state_path[PATH_MAX];
        const char *replacement_state_path;

        if (!validate_megaflash_settings(
                rom_path, card_a_path, card_b_path))
            return;
        if (!megaflash_transaction_paths(
                overlay, rom_path, state_path, sizeof(state_path),
                pending_state_path, sizeof(pending_state_path)))
            return;
        replacement_state_path = reseed_pending && pending_state_path[0]
            ? pending_state_path : state_path;
        if (changed && config->megaflash) {
            if (msx_replace_megaflash(
                    overlay->msx, rom_path, replacement_state_path,
                    image_changed && reseed_pending,
                    card_a_path, card_b_path,
                    config->sd_image_mode) != 0) {
                notify_post("Could not apply MegaFlashROM settings");
                return;
            }
        } else if (changed && image_changed && reseed_pending &&
                   !prepare_pending_megaflash_state(
                       overlay, rom_path, pending_state_path,
                       state_path)) {
            return;
        }
        if (changed) {
            if (reseed_pending && pending_state_path[0]) {
                remember_pending_megaflash_state(
                    overlay, pending_state_path, state_path);
            } else {
                discard_pending_megaflash_state(overlay);
            }
        }
        if (changed) {
            store_megaflash_settings(
                config, rom_path, card_a_path, card_b_path);
            overlay->dirty = true;
            apply_config(overlay);
            notify_post("MegaFlashROM settings updated%s",
                        config->megaflash ? " and reconnected" : "");
        }
        overlay->state = OVERLAY_STATE_MENU;
        return;
    }
    {
        char state_path[PATH_MAX];
        char pending_state_path[PATH_MAX];
        bool reseed_pending =
            strcmp(overlay->saved.megaflash_rom_path, rom_path) != 0;

        if (!megaflash_transaction_paths(
                overlay, rom_path, state_path, sizeof(state_path),
                pending_state_path, sizeof(pending_state_path)))
            return;
        if (reseed_pending &&
            !prepare_pending_megaflash_state(
                overlay, rom_path, pending_state_path, state_path))
            return;
        setup_reseed_prepared =
            reseed_pending && pending_state_path[0];
        if (!reseed_pending)
            discard_pending_megaflash_state(overlay);
    }
    if (!connect_megaflash(
            overlay, rom_path, card_a_path, card_b_path,
            config->sd_image_mode)) {
        if (setup_reseed_prepared)
            discard_pending_megaflash_state(overlay);
        return;
    }
    overlay->dirty = true;
    overlay->state = OVERLAY_STATE_MENU;
    apply_config(overlay);
}

static bool disconnect_megaflash(Overlay *overlay) {
    if (msx_eject_megaflash(overlay->msx) != 0) {
        const char *error =
            msx_megaflash_flash_has_error(overlay->msx)
            ? msx_megaflash_flash_error(overlay->msx) :
            msx_megaflash_card_has_error(overlay->msx, 0)
            ? msx_megaflash_card_error(overlay->msx, 0)
            : msx_megaflash_card_error(overlay->msx, 1);

        notify_post(
            "Could not disconnect MegaFlashROM safely: %s",
            error);
        return false;
    }
    (void)toggle_cartridge_extension(
        overlay, &overlay->config->megaflash,
        "MegaFlashROM SCC+ SD");
    return true;
}

static bool set_sd_image_mode(Overlay *overlay, SdImageMode mode) {
    Config *config = overlay->config;

    if (config->sd_image_mode == mode)
        return true;
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card) {
        if (!msx_sd_card_mounted(overlay->msx, card))
            continue;
        if (msx_mount_sd_card(
                overlay->msx, card,
                config->sd_card_path[card], mode) == 0)
            continue;
        notify_post("Could not switch SD Card %c access: %s",
                    'A' + (int)card,
                    msx_sd_card_error(overlay->msx, card));
        for (unsigned rollback = 0; rollback < card; ++rollback) {
            if (msx_sd_card_mounted(overlay->msx, rollback))
                (void)msx_mount_sd_card(
                    overlay->msx, rollback,
                    config->sd_card_path[rollback],
                    config->sd_image_mode);
        }
        return false;
    }
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        if (!msx_megaflash_card_mounted(overlay->msx, card))
            continue;
        if (msx_mount_megaflash_card(
                overlay->msx, card,
                config->megaflash_card_path[card], mode) == 0)
            continue;
        notify_post(
            "Could not switch MegaFlash SD %c access: %s",
            'A' + (int)card,
            msx_megaflash_card_error(overlay->msx, card));
        for (unsigned rollback = 0;
             rollback < MSX_SD_MAPPER_CARDS; ++rollback) {
            if (msx_sd_card_mounted(overlay->msx, rollback))
                (void)msx_mount_sd_card(
                    overlay->msx, rollback,
                    config->sd_card_path[rollback],
                    config->sd_image_mode);
        }
        for (unsigned rollback = 0; rollback < card; ++rollback) {
            if (msx_megaflash_card_mounted(
                    overlay->msx, rollback))
                (void)msx_mount_megaflash_card(
                    overlay->msx, rollback,
                    config->megaflash_card_path[rollback],
                    config->sd_image_mode);
        }
        return false;
    }
    config->sd_image_mode = mode;
    overlay->dirty = true;
    notify_post("SD image access set to %s",
                sd_mode_name(mode));
    return true;
}

static bool set_ide_image_mode(Overlay *overlay,
                               AtaImageMode mode) {
    Config *config = overlay->config;

    if (config->ide_image_mode == mode)
        return true;
    if (msx_sunrise_disk_mounted(overlay->msx) &&
        msx_mount_sunrise_disk_mode(
            overlay->msx, config->ide_image_path, mode) != 0) {
        notify_post("Could not switch IDE image access: %s",
                    msx_sunrise_disk_error(overlay->msx));
        return false;
    }
    config->ide_image_mode = mode;
    overlay->dirty = true;
    notify_post("IDE image access set to %s",
                ide_mode_name(mode));
    return true;
}

static bool set_floppy_image_mode(Overlay *overlay,
                                  FloppyImageMode mode) {
    Config *config = overlay->config;
    FloppyImageMode old_mode = config->floppy_image_mode;
    bool drive_a_changed = false;

    if (config->floppy_image_mode == mode)
        return true;
    if (msx_drive_a_mounted(overlay->msx) &&
        msx_mount_drive_a(
            overlay->msx, config->drive_a_path, mode) != 0) {
        notify_post("Could not switch Floppy A access: %s",
                    msx_drive_a_error(overlay->msx));
        return false;
    }
    drive_a_changed = msx_drive_a_mounted(overlay->msx);
    if (msx_drive_b_mounted(overlay->msx) &&
        msx_mount_drive_b(
            overlay->msx, config->drive_b_path, mode) != 0) {
        if (drive_a_changed &&
            msx_mount_drive_a(
                overlay->msx, config->drive_a_path,
                old_mode) != 0)
            notify_post("Could not restore Floppy A access: %s",
                        msx_drive_a_error(overlay->msx));
        notify_post("Could not switch Floppy B access: %s",
                    msx_drive_b_error(overlay->msx));
        return false;
    }
    config->floppy_image_mode = mode;
    overlay->dirty = true;
    notify_post("Floppy image access set to %s",
                floppy_mode_name(mode));
    return true;
}

static bool toggle_second_floppy(Overlay *overlay) {
    Config *config = overlay->config;

    if (config->second_drive) {
        if (msx_eject_drive_b(overlay->msx) != 0) {
            notify_post("Could not disable Floppy B: %s",
                        msx_drive_b_error(overlay->msx));
            return false;
        }
        config->second_drive = false;
        overlay->dirty = true;
        notify_post("Second floppy disabled");
        return true;
    }
    if (!msx_floppy_supported(overlay->msx)) {
        notify_post("Second floppy requires the Philips NMS 8250 model");
        return false;
    }
    config->second_drive = true;
    overlay->dirty = true;
    if (config->drive_b_path[0] &&
        msx_mount_drive_b(
            overlay->msx, config->drive_b_path,
            config->floppy_image_mode) != 0) {
        notify_post("Second floppy enabled; saved image not mounted: %s",
                    msx_drive_b_error(overlay->msx));
    } else {
        notify_post("Second floppy enabled");
    }
    return true;
}

static void edit_extension_settings(Overlay *overlay) {
    if (overlay->section != OVERLAY_EXTENSIONS)
        return;
    switch (overlay->row) {
        case EXTENSION_SUNRISE_IDE:
            begin_sunrise_setup(overlay, true);
            break;
        case EXTENSION_SD_MAPPER:
            begin_sd_mapper_setup(overlay, true);
            break;
        case EXTENSION_MEGAFLASH:
            begin_megaflash_setup(overlay, true);
            break;
        default:
            break;
    }
}

static void activate_item(Overlay *overlay) {
    Config *config = overlay->config;

    switch (overlay->section) {
        case OVERLAY_GENERAL:
            switch (overlay->row) {
                case GENERAL_MACHINE:
                    overlay->machine_row = (int)model_catalog_index(
                        overlay->models, config->machine_id);
                    overlay->state = OVERLAY_STATE_MACHINE;
                    return;
                case GENERAL_VIDEO_STANDARD:
                    config->region =
                        config->region == MSX_REGION_PAL
                        ? MSX_REGION_NTSC : MSX_REGION_PAL;
                    break;
                case GENERAL_RAM:
                    config->memory_kb =
                        msx_next_ram_kb(config->model,
                                        config->memory_kb, 1);
                    break;
                case GENERAL_VRAM:
                    notify_post("VRAM follows the selected MSX generation");
                    return;
                case GENERAL_PSG_VOLUME:
                    config->audio_volume += 10;
                    if (config->audio_volume > 100)
                        config->audio_volume = 0;
                    break;
                case GENERAL_MAIN_INPUT:
                    config->main_input =
                        config->main_input == INPUT_PORT_A
                        ? INPUT_PORT_B : INPUT_PORT_A;
                    break;
                case GENERAL_JOY_PORT_A:
                    config->joy_port_device[0] =
                        config->joy_port_device[0] ==
                            JOY_PORT_JOYSTICK
                        ? JOY_PORT_MOUSE : JOY_PORT_JOYSTICK;
                    break;
                case GENERAL_JOY_PORT_B:
                    config->joy_port_device[1] =
                        config->joy_port_device[1] ==
                            JOY_PORT_JOYSTICK
                        ? JOY_PORT_MOUSE : JOY_PORT_JOYSTICK;
                    break;
                case GENERAL_EXTRA_HARDWARE:
                    config->extra_hardware =
                        !config->extra_hardware;
                    break;
                case GENERAL_TINKER:
                    config->tinker = !config->tinker;
                    break;
            }
            break;
        case OVERLAY_MEDIA: {
            if (overlay->row == 0) {
                open_cartridge_dialog(overlay, 0);
                return;
            }
            if (overlay->row == 1) {
                change_cartridge_mapper(overlay, 0);
                return;
            }
            if (overlay->row == 2) {
                open_cartridge_dialog(overlay, 1);
                return;
            }
            if (overlay->row == 3) {
                change_cartridge_mapper(overlay, 1);
                return;
            }
            if (overlay->row == 4) {
                open_cassette_dialog(overlay);
                return;
            }
            if (overlay->row == 5) {
                open_drive_a_dialog(overlay);
                return;
            }
            if (overlay->row == media_floppy_b_row(config)) {
                open_drive_b_dialog(overlay);
                return;
            }
            if (overlay->row == media_ide_row(config)) {
                open_ide_image_dialog(overlay);
                return;
            }
            if (overlay->row == media_sd_a_row(config)) {
                open_sd_card_dialog(overlay, 0);
                return;
            }
            if (overlay->row == media_sd_b_row(config)) {
                open_sd_card_dialog(overlay, 1);
                return;
            }
            if (overlay->row == media_megaflash_sd_a_row(config)) {
                open_megaflash_card_dialog(overlay, 0);
                return;
            }
            if (overlay->row == media_megaflash_sd_b_row(config)) {
                open_megaflash_card_dialog(overlay, 1);
                return;
            }
            return;
        }
        case OVERLAY_EXTENSIONS:
            switch (overlay->row) {
                case EXTENSION_SUNRISE_IDE:
                    if (config->sunrise_ide) {
                        if (!disconnect_sunrise(overlay))
                            return;
                    } else if (firmware_file_has_size(
                                   config->sunrise_rom_path,
                                   MSX_SUNRISE_ROM_SIZE) &&
                               (!config->ide_image_path[0] ||
                                ide_image_file_is_valid(
                                    config->ide_image_path))) {
                        if (!connect_sunrise(
                                overlay,
                                config->sunrise_rom_path,
                                config->ide_image_path,
                                config->ide_image_mode))
                            return;
                    } else {
                        begin_sunrise_setup(overlay, false);
                        return;
                    }
                    break;
                case EXTENSION_SD_MAPPER:
                    if (config->sd_mapper) {
                        if (!disconnect_sd_mapper(overlay))
                            return;
                    } else if (sd_mapper_rom_file_is_valid(
                                   config->sd_mapper_rom_path) &&
                               (!config->sd_card_path[0][0] ||
                                sd_image_file_is_valid(
                                    config->sd_card_path[0])) &&
                               (!config->sd_card_path[1][0] ||
                                sd_image_file_is_valid(
                                    config->sd_card_path[1]))) {
                        if (!connect_sd_mapper(
                                overlay,
                                config->sd_mapper_rom_path,
                                config->sd_card_path[0],
                                config->sd_card_path[1],
                                config->sd_image_mode,
                                config->sd_mapper_ram,
                                config->sd_mapper_alternate_driver))
                            return;
                    } else {
                        begin_sd_mapper_setup(overlay, false);
                        return;
                    }
                    break;
                case EXTENSION_MEGAFLASH:
                    if (config->megaflash) {
                        if (!disconnect_megaflash(overlay))
                            return;
                    } else if (megaflash_rom_file_is_valid(
                                   config->megaflash_rom_path) &&
                               (!config->megaflash_card_path[0][0] ||
                                sd_image_file_is_valid(
                                    config->
                                        megaflash_card_path[0])) &&
                               (!config->megaflash_card_path[1][0] ||
                                sd_image_file_is_valid(
                                    config->
                                        megaflash_card_path[1]))) {
                        if (!connect_megaflash(
                                overlay,
                                config->megaflash_rom_path,
                                config->megaflash_card_path[0],
                                config->megaflash_card_path[1],
                                config->sd_image_mode))
                            return;
                    } else {
                        begin_megaflash_setup(overlay, false);
                        return;
                    }
                    break;
                case EXTENSION_TCPIP_UNAPI:
                    config->tcpip_unapi = !config->tcpip_unapi;
                    notify_post("MSX TCP/IP UNAPI bridge %s",
                                config->tcpip_unapi
                                ? "enabled; run UNAPINET.COM in Nextor"
                                : "disabled");
                    break;
                case EXTENSION_KONAMI_SCC:
                    if (!toggle_cartridge_extension(
                            overlay, &config->scc,
                            "Konami SCC"))
                        return;
                    break;
                case EXTENSION_MSX_MUSIC:
                    if (!toggle_cartridge_extension(
                            overlay, &config->msx_music,
                            "MSX-MUSIC"))
                        return;
                    break;
                case EXTENSION_SECOND_FLOPPY:
                    if (!toggle_second_floppy(overlay))
                        return;
                    break;
            }
            break;
        case OVERLAY_ADVANCED:
            switch (overlay->row) {
                case ADVANCED_MODEL_EDITOR:
                    begin_model_editor(overlay);
                    return;
                case ADVANCED_RTC_PERSISTENCE: {
                    bool previous = config->rtc_persistence;

                    if (!overlay->msx->profile->rtc) {
                        notify_post(
                            "The selected machine has no RTC");
                        return;
                    }
                    config->rtc_persistence = !previous;
                    if (!sync_rtc_persistence(overlay)) {
                        config->rtc_persistence = previous;
                        return;
                    }
                    notify_post("RTC persistence %s",
                                config->rtc_persistence
                                ? "enabled" : "disabled");
                    break;
                }
                case ADVANCED_FLOPPY_IMAGE_ACCESS:
                    if (!set_floppy_image_mode(
                            overlay,
                            config->floppy_image_mode ==
                                FLOPPY_IMAGE_READ_ONLY
                            ? FLOPPY_IMAGE_READ_WRITE :
                              FLOPPY_IMAGE_READ_ONLY))
                        return;
                    break;
                case ADVANCED_IDE_IMAGE_ACCESS:
                    if (!set_ide_image_mode(
                            overlay,
                            config->ide_image_mode ==
                                ATA_IMAGE_READ_ONLY
                            ? ATA_IMAGE_READ_WRITE :
                              ATA_IMAGE_READ_ONLY))
                        return;
                    break;
                case ADVANCED_SD_IMAGE_ACCESS:
                    if (!set_sd_image_mode(
                            overlay,
                            config->sd_image_mode ==
                                SD_IMAGE_READ_ONLY
                            ? SD_IMAGE_READ_WRITE :
                              SD_IMAGE_READ_ONLY))
                        return;
                    break;
                case ADVANCED_SD_MAPPER_RAM:
                    config->sd_mapper_ram =
                        !config->sd_mapper_ram;
                    msx_sd_mapper_set_ram_enabled(
                        overlay->msx, config->sd_mapper_ram);
                    if (msx_sd_mapper_connected(overlay->msx))
                        msx_reset(overlay->msx);
                    notify_post("SD Mapper 512 KB RAM %s",
                                config->sd_mapper_ram
                                ? "enabled" : "disabled");
                    break;
                case ADVANCED_SD_MAPPER_DRIVER:
                    config->sd_mapper_alternate_driver =
                        !config->sd_mapper_alternate_driver;
                    msx_sd_mapper_set_alternate_driver(
                        overlay->msx,
                        config->sd_mapper_alternate_driver);
                    if (msx_sd_mapper_connected(overlay->msx))
                        msx_reset(overlay->msx);
                    notify_post("SD Mapper %s driver selected",
                                config->sd_mapper_alternate_driver
                                ? "alternate" : "primary");
                    break;
                case ADVANCED_SMOOTHING:
                    config->smoothing = !config->smoothing;
                    break;
                case ADVANCED_REAL_CRT:
                    config->real_crt = !config->real_crt;
                    break;
                case ADVANCED_CRT_SCANLINES:
                    config->crt_scanlines += 5;
                    if (config->crt_scanlines > 95)
                        config->crt_scanlines = 0;
                    break;
                case ADVANCED_GIF_RESOLUTION:
                    config->gif_width = cycle_gif_width(config->gif_width);
                    break;
                case ADVANCED_GIF_FPS:
                    config->gif_fps = cycle_gif_fps(config->gif_fps);
                    break;
                case ADVANCED_GIF_ENCODER:
                    config->gif_ffmpeg = !config->gif_ffmpeg;
                    break;
                case ADVANCED_CASSETTE_AUDIBLE:
                    config->cassette_audible_monitor =
                        !config->cassette_audible_monitor;
                    break;
                case ADVANCED_CASSETTE_VISUAL:
                    config->cassette_visual_monitor =
                        !config->cassette_visual_monitor;
                    break;
                case ADVANCED_NOTIFICATIONS:
                    change_notification_mode(config);
                    break;
                case ADVANCED_DEBUG:
                    config->debug = !config->debug;
                    break;
                case ADVANCED_VERSION:
                    return;
            }
            break;
        case OVERLAY_SECTION_COUNT:
            return;
    }
    overlay->dirty = true;
    apply_config(overlay);
}

void overlay_init(Overlay *overlay, Config *config,
                  ModelCatalog *models, Display *display,
                  MsxMachine *msx, UnapiNet *unapinet) {
    memset(overlay, 0, sizeof(*overlay));
    overlay->dialog_target = OVERLAY_DIALOG_NONE;
    overlay->config = config;
    overlay->models = models;
    overlay->display = display;
    overlay->msx = msx;
    overlay->unapinet = unapinet;
    apply_config(overlay);
}

bool overlay_handle_event(Overlay *overlay, const SDL_Event *event) {
    SDL_Keycode key;

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_F9) {
        if (!overlay->visible) {
            overlay->visible = true;
            overlay->dirty = false;
            overlay->state = OVERLAY_STATE_MENU;
            overlay->section = OVERLAY_GENERAL;
            overlay->row = 0;
            overlay->saved = *overlay->config;
        } else {
            if (overlay->state == OVERLAY_STATE_SUNRISE_SETUP ||
                overlay->state == OVERLAY_STATE_SD_MAPPER_SETUP ||
                overlay->state == OVERLAY_STATE_MEGAFLASH_SETUP)
                cancel_extension_setup(overlay);
            close_overlay(overlay, true);
        }
        return true;
    }
    if (!overlay->visible)
        return false;
    if (event->type == SDL_EVENT_QUIT &&
        overlay->megaflash_state_pending) {
        close_overlay(overlay, true);
        return overlay->visible;
    }
    if (event->type == SDL_EVENT_QUIT)
        return false;
    if (overlay->state == OVERLAY_STATE_MODEL_TEXT) {
        if (event->type == SDL_EVENT_TEXT_INPUT) {
            size_t capacity;
            char *field = model_edit_text_field(
                overlay, overlay->model_text_field, &capacity);
            size_t length = strlen(overlay->model_text);
            const char *text = event->text.text;

            (void)field;
            while (*text && length + 1 < capacity) {
                unsigned char character = (unsigned char)*text++;
                if (character != '\r' && character != '\n')
                    overlay->model_text[length++] = (char)character;
            }
            overlay->model_text[length] = '\0';
            return true;
        }
        if (event->type != SDL_EVENT_KEY_DOWN)
            return true;
        key = event->key.key;
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            finish_model_text_edit(overlay, true);
        } else if (key == SDLK_ESCAPE) {
            finish_model_text_edit(overlay, false);
        } else if (key == SDLK_DELETE ||
                   (key == SDLK_A &&
                    (event->key.mod & SDL_KMOD_CTRL))) {
            overlay->model_text[0] = '\0';
        } else if (key == SDLK_BACKSPACE) {
            pop_utf8_character(overlay->model_text);
        }
        return true;
    }
    if (event->type != SDL_EVENT_KEY_DOWN)
        return true;

    key = event->key.key;
    if (overlay->state == OVERLAY_STATE_CONFIRM) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
            key == SDLK_Y) {
            close_overlay(overlay, true);
        } else if (key == SDLK_ESCAPE || key == SDLK_N) {
            close_overlay(overlay, false);
        }
        return true;
    }
    if (overlay->state == OVERLAY_STATE_MACHINE) {
        if (key == SDLK_UP || key == SDLK_LEFT) {
            --overlay->machine_row;
            if (overlay->machine_row < 0)
                overlay->machine_row = (int)overlay->models->count - 1;
        } else if (key == SDLK_DOWN || key == SDLK_RIGHT) {
            ++overlay->machine_row;
            if (overlay->machine_row >= (int)overlay->models->count)
                overlay->machine_row = 0;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            select_machine(overlay);
        } else if (key == SDLK_ESCAPE) {
            overlay->state = OVERLAY_STATE_MENU;
        }
        return true;
    }
    if (overlay->state == OVERLAY_STATE_SUNRISE_SETUP) {
        if (key == SDLK_UP) {
            --overlay->sunrise_setup_row;
            if (overlay->sunrise_setup_row < 0)
                overlay->sunrise_setup_row =
                    SUNRISE_SETUP_ROWS - 1;
        } else if (key == SDLK_DOWN) {
            ++overlay->sunrise_setup_row;
            if (overlay->sunrise_setup_row >=
                SUNRISE_SETUP_ROWS)
                overlay->sunrise_setup_row = 0;
        } else if (key == SDLK_RETURN ||
                   key == SDLK_KP_ENTER) {
            switch (overlay->sunrise_setup_row) {
                case SUNRISE_SETUP_FIRMWARE:
                    open_sunrise_rom_dialog(overlay);
                    break;
                case SUNRISE_SETUP_DISK:
                    open_ide_image_dialog(overlay);
                    break;
                case SUNRISE_SETUP_CONNECT:
                    finish_sunrise_setup(overlay);
                    break;
            }
        } else if (key == SDLK_DELETE) {
            if (overlay->sunrise_setup_row ==
                SUNRISE_SETUP_FIRMWARE)
                overlay->pending_sunrise_rom_path[0] = '\0';
            else if (overlay->sunrise_setup_row ==
                     SUNRISE_SETUP_DISK)
                overlay->pending_ide_image_path[0] = '\0';
        } else if (key == SDLK_ESCAPE) {
            cancel_extension_setup(overlay);
        }
        return true;
    }
    if (overlay->state == OVERLAY_STATE_SD_MAPPER_SETUP) {
        if (key == SDLK_UP) {
            --overlay->sd_mapper_setup_row;
            if (overlay->sd_mapper_setup_row < 0)
                overlay->sd_mapper_setup_row =
                    SD_MAPPER_SETUP_ROWS - 1;
        } else if (key == SDLK_DOWN) {
            ++overlay->sd_mapper_setup_row;
            if (overlay->sd_mapper_setup_row >=
                SD_MAPPER_SETUP_ROWS)
                overlay->sd_mapper_setup_row = 0;
        } else if (key == SDLK_RETURN ||
                   key == SDLK_KP_ENTER) {
            switch (overlay->sd_mapper_setup_row) {
                case SD_MAPPER_SETUP_FIRMWARE:
                    open_sd_mapper_rom_dialog(overlay);
                    break;
                case SD_MAPPER_SETUP_CARD_A:
                    open_sd_card_dialog(overlay, 0);
                    break;
                case SD_MAPPER_SETUP_CARD_B:
                    open_sd_card_dialog(overlay, 1);
                    break;
                case SD_MAPPER_SETUP_RAM:
                    overlay->pending_sd_mapper_ram =
                        !overlay->pending_sd_mapper_ram;
                    break;
                case SD_MAPPER_SETUP_DRIVER:
                    overlay->pending_sd_mapper_alternate_driver =
                        !overlay->
                            pending_sd_mapper_alternate_driver;
                    break;
                case SD_MAPPER_SETUP_CONNECT:
                    finish_sd_mapper_setup(overlay);
                    break;
            }
        } else if (key == SDLK_DELETE) {
            if (overlay->sd_mapper_setup_row ==
                SD_MAPPER_SETUP_FIRMWARE)
                overlay->pending_sd_mapper_rom_path[0] = '\0';
            else if (overlay->sd_mapper_setup_row ==
                     SD_MAPPER_SETUP_CARD_A)
                overlay->pending_sd_card_path[0][0] = '\0';
            else if (overlay->sd_mapper_setup_row ==
                     SD_MAPPER_SETUP_CARD_B)
                overlay->pending_sd_card_path[1][0] = '\0';
        } else if (key == SDLK_ESCAPE) {
            cancel_extension_setup(overlay);
        }
        return true;
    }
    if (overlay->state == OVERLAY_STATE_MEGAFLASH_SETUP) {
        if (key == SDLK_UP) {
            --overlay->megaflash_setup_row;
            if (overlay->megaflash_setup_row < 0)
                overlay->megaflash_setup_row =
                    MEGAFLASH_SETUP_ROWS - 1;
        } else if (key == SDLK_DOWN) {
            ++overlay->megaflash_setup_row;
            if (overlay->megaflash_setup_row >=
                MEGAFLASH_SETUP_ROWS)
                overlay->megaflash_setup_row = 0;
        } else if (key == SDLK_RETURN ||
                   key == SDLK_KP_ENTER) {
            switch (overlay->megaflash_setup_row) {
                case MEGAFLASH_SETUP_FIRMWARE:
                    open_megaflash_rom_dialog(overlay);
                    break;
                case MEGAFLASH_SETUP_CARD_A:
                    open_megaflash_card_dialog(overlay, 0);
                    break;
                case MEGAFLASH_SETUP_CARD_B:
                    open_megaflash_card_dialog(overlay, 1);
                    break;
                case MEGAFLASH_SETUP_CONNECT:
                    finish_megaflash_setup(overlay);
                    break;
            }
        } else if (key == SDLK_DELETE) {
            if (overlay->megaflash_setup_row ==
                MEGAFLASH_SETUP_FIRMWARE)
                overlay->pending_megaflash_rom_path[0] = '\0';
            else if (overlay->megaflash_setup_row ==
                     MEGAFLASH_SETUP_CARD_A)
                overlay->pending_megaflash_card_path[0][0] = '\0';
            else if (overlay->megaflash_setup_row ==
                     MEGAFLASH_SETUP_CARD_B)
                overlay->pending_megaflash_card_path[1][0] = '\0';
        } else if (key == SDLK_ESCAPE) {
            cancel_extension_setup(overlay);
        }
        return true;
    }
    if (overlay->state == OVERLAY_STATE_MODEL_LIST) {
        if (key == SDLK_UP || key == SDLK_LEFT) {
            --overlay->model_editor_row;
            if (overlay->model_editor_row < 0)
                overlay->model_editor_row =
                    (int)overlay->models->count - 1;
        } else if (key == SDLK_DOWN || key == SDLK_RIGHT) {
            ++overlay->model_editor_row;
            if (overlay->model_editor_row >=
                (int)overlay->models->count)
                overlay->model_editor_row = 0;
        } else if (key == SDLK_HOME) {
            overlay->model_editor_row = 0;
        } else if (key == SDLK_END && overlay->models->count) {
            overlay->model_editor_row =
                (int)overlay->models->count - 1;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            begin_model_edit(overlay, overlay->model_editor_row,
                             false);
        } else if (key == SDLK_INSERT || key == SDLK_A) {
            if (overlay->models->count >= MODEL_CATALOG_MAX) {
                snprintf(overlay->model_editor_error,
                         sizeof(overlay->model_editor_error),
                         "The catalogue is full");
            } else {
                begin_model_edit(overlay, -1, false);
            }
        } else if (key == SDLK_D) {
            if (overlay->models->count >= MODEL_CATALOG_MAX) {
                snprintf(overlay->model_editor_error,
                         sizeof(overlay->model_editor_error),
                         "The catalogue is full");
            } else {
                begin_model_edit(overlay,
                                 overlay->model_editor_row, true);
            }
        } else if (key == SDLK_DELETE) {
            const ModelDefinition *definition =
                &overlay->models->entries[
                    overlay->model_editor_row];

            if (overlay->models->count <= 1) {
                snprintf(overlay->model_editor_error,
                         sizeof(overlay->model_editor_error),
                         "The catalogue must keep at least one model");
            } else if (strcmp(
                           definition->id,
                           overlay->config->machine_id) == 0) {
                snprintf(overlay->model_editor_error,
                         sizeof(overlay->model_editor_error),
                         "Select another machine before deleting this one");
            } else {
                overlay->state = OVERLAY_STATE_MODEL_DELETE;
            }
        } else if (key == SDLK_ESCAPE) {
            overlay->state = OVERLAY_STATE_MENU;
        }
        return true;
    }
    if (overlay->state == OVERLAY_STATE_MODEL_EDIT) {
        if (key == SDLK_UP) {
            --overlay->model_edit_field;
            if (overlay->model_edit_field < 0)
                overlay->model_edit_field =
                    MODEL_EDITOR_FIELDS - 1;
        } else if (key == SDLK_DOWN) {
            ++overlay->model_edit_field;
            if (overlay->model_edit_field >= MODEL_EDITOR_FIELDS)
                overlay->model_edit_field = 0;
        } else if ((key == SDLK_LEFT || key == SDLK_RIGHT) &&
                   overlay->model_edit_field ==
                       MODEL_FIELD_HARDWARE) {
            int direction = key == SDLK_RIGHT ? 1 : -1;
            int hardware =
                (int)overlay->model_edit.hardware + direction;

            if (hardware < 0)
                hardware = MSX_MODEL_COUNT - 1;
            if (hardware >= MSX_MODEL_COUNT)
                hardware = 0;
            overlay->model_edit.hardware = (MsxModel)hardware;
        } else if (key == SDLK_F2) {
            save_model_edit(overlay);
        } else if (key == SDLK_RETURN ||
                   key == SDLK_KP_ENTER) {
            switch (overlay->model_edit_field) {
                case MODEL_FIELD_ID:
                case MODEL_FIELD_NAME:
                    begin_model_text_edit(
                        overlay, overlay->model_edit_field);
                    break;
                case MODEL_FIELD_HARDWARE:
                    overlay->model_edit.hardware =
                        (MsxModel)(
                            (overlay->model_edit.hardware + 1) %
                            MSX_MODEL_COUNT);
                    break;
                case MODEL_FIELD_BIOS:
                    open_model_firmware_dialog(
                        overlay, OVERLAY_DIALOG_MODEL_BIOS);
                    break;
                case MODEL_FIELD_LOGO:
                    open_model_firmware_dialog(
                        overlay, OVERLAY_DIALOG_MODEL_LOGO);
                    break;
                case MODEL_FIELD_SUBROM:
                    open_model_firmware_dialog(
                        overlay, OVERLAY_DIALOG_MODEL_SUBROM);
                    break;
                case MODEL_FIELD_DISK_ROM:
                    open_model_firmware_dialog(
                        overlay, OVERLAY_DIALOG_MODEL_DISK_ROM);
                    break;
            }
        } else if (key == SDLK_E &&
                   overlay->model_edit_field !=
                       MODEL_FIELD_HARDWARE) {
            begin_model_text_edit(
                overlay, overlay->model_edit_field);
        } else if (key == SDLK_DELETE &&
                   overlay->model_edit_field >=
                       MODEL_FIELD_BIOS) {
            char *field = model_edit_text_field(
                overlay, overlay->model_edit_field, NULL);
            if (field)
                field[0] = '\0';
        } else if (key == SDLK_ESCAPE) {
            overlay->model_editor_error[0] = '\0';
            overlay->state = OVERLAY_STATE_MODEL_LIST;
        }
        return true;
    }
    if (overlay->state == OVERLAY_STATE_MODEL_DELETE) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
            key == SDLK_Y) {
            delete_model_edit(overlay);
        } else if (key == SDLK_ESCAPE || key == SDLK_N) {
            overlay->state = OVERLAY_STATE_MODEL_LIST;
        }
        return true;
    }

    switch (key) {
        case SDLK_LEFT:
            change_section(overlay, -1);
            break;
        case SDLK_RIGHT:
            change_section(overlay, 1);
            break;
        case SDLK_UP:
            --overlay->row;
            if (overlay->row < 0)
                overlay->row =
                    section_rows(overlay, overlay->section) - 1;
            break;
        case SDLK_DOWN:
            ++overlay->row;
            if (overlay->row >=
                section_rows(overlay, overlay->section))
                overlay->row = 0;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            activate_item(overlay);
            break;
        case SDLK_R:
            if (overlay->section == OVERLAY_MEDIA &&
                overlay->row == 4 &&
                msx_cassette_mounted(overlay->msx)) {
                msx_rewind_cassette(overlay->msx);
                notify_post("Cassette rewound");
            }
            break;
        case SDLK_SPACE:
            edit_extension_settings(overlay);
            break;
        case SDLK_DELETE:
            if (overlay->section == OVERLAY_GENERAL &&
                overlay->row == GENERAL_MACHINE) {
                notify_post(
                    "Change firmware in Advanced > Machine model editor");
            } else if (overlay->section == OVERLAY_MEDIA &&
                       (overlay->row == 0 || overlay->row == 2)) {
                unsigned slot = overlay->row == 0 ? 0 : 1;
                const char *owner =
                    config_cartridge_slot_owner(
                        overlay->config, slot);

                if (owner) {
                    notify_post("Cartridge slot %u is reserved by %s",
                                slot + 1, owner);
                } else {
                    msx_eject_cartridge(overlay->msx, slot);
                    overlay->config->cartridge_path[slot][0] = '\0';
                    configure_leds(overlay->config, overlay->msx);
                    overlay->dirty = true;
                    notify_post("Cartridge %u ejected", slot + 1);
                }
            } else if (overlay->section == OVERLAY_MEDIA &&
                       overlay->row == 4) {
                msx_eject_cassette(overlay->msx);
                overlay->config->cassette_path[0] = '\0';
                overlay->dirty = true;
                leds_set_state(LED_TAPE, false);
                notify_post("Cassette ejected");
            } else if (overlay->section == OVERLAY_MEDIA &&
                       overlay->row == 5) {
                if (msx_eject_drive_a(overlay->msx) != 0) {
                    notify_post("Could not eject Floppy A: %s",
                                msx_drive_a_error(overlay->msx));
                } else {
                    overlay->config->drive_a_path[0] = '\0';
                    overlay->dirty = true;
                    notify_post("Floppy A safely ejected");
                }
            } else if (overlay->section == OVERLAY_MEDIA &&
                       overlay->row ==
                           media_floppy_b_row(overlay->config)) {
                if (msx_eject_drive_b(overlay->msx) != 0) {
                    notify_post("Could not eject Floppy B: %s",
                                msx_drive_b_error(overlay->msx));
                } else {
                    overlay->config->drive_b_path[0] = '\0';
                    overlay->dirty = true;
                    notify_post("Floppy B safely ejected");
                }
            } else if (overlay->section == OVERLAY_MEDIA &&
                       overlay->row ==
                           media_ide_row(overlay->config) &&
                       overlay->config->sunrise_ide) {
                if (msx_eject_sunrise_disk(overlay->msx) != 0) {
                    notify_post("Could not eject IDE disk: %s",
                                msx_sunrise_disk_error(overlay->msx));
                } else {
                    overlay->config->ide_image_path[0] = '\0';
                    overlay->dirty = true;
                    notify_post("IDE disk safely ejected");
                }
            } else if (overlay->section == OVERLAY_MEDIA &&
                       (overlay->row ==
                            media_sd_a_row(overlay->config) ||
                        overlay->row ==
                            media_sd_b_row(overlay->config)) &&
                       overlay->config->sd_mapper) {
                unsigned card =
                    overlay->row ==
                        media_sd_b_row(overlay->config) ? 1u : 0u;

                if (msx_eject_sd_card(
                        overlay->msx, card) != 0) {
                    notify_post("Could not eject SD Card %c: %s",
                                'A' + (int)card,
                                msx_sd_card_error(
                                    overlay->msx, card));
                } else {
                    overlay->config->
                        sd_card_path[card][0] = '\0';
                    overlay->dirty = true;
                    notify_post("SD Card %c safely ejected",
                                'A' + (int)card);
                }
            } else if (overlay->section == OVERLAY_MEDIA &&
                       (overlay->row ==
                            media_megaflash_sd_a_row(
                                overlay->config) ||
                        overlay->row ==
                            media_megaflash_sd_b_row(
                                overlay->config)) &&
                       overlay->config->megaflash) {
                unsigned card =
                    overlay->row ==
                        media_megaflash_sd_b_row(
                            overlay->config) ? 1u : 0u;

                if (msx_eject_megaflash_card(
                        overlay->msx, card) != 0) {
                    notify_post(
                        "Could not eject MegaFlash SD %c: %s",
                        'A' + (int)card,
                        msx_megaflash_card_error(
                            overlay->msx, card));
                } else {
                    overlay->config->
                        megaflash_card_path[card][0] = '\0';
                    overlay->dirty = true;
                    notify_post(
                        "MegaFlash SD %c safely ejected",
                        'A' + (int)card);
                }
            } else if (overlay->section == OVERLAY_EXTENSIONS &&
                       overlay->row == EXTENSION_SUNRISE_IDE) {
                bool configured = overlay->config->sunrise_ide ||
                    overlay->config->sunrise_rom_path[0] ||
                    overlay->config->ide_image_path[0];

                if (!configured)
                    break;
                if (overlay->config->sunrise_ide &&
                    !disconnect_sunrise(overlay))
                    break;
                overlay->config->sunrise_rom_path[0] = '\0';
                overlay->config->ide_image_path[0] = '\0';
                overlay->dirty = true;
                apply_config(overlay);
                notify_post("Sunrise IDE settings cleared");
            } else if (overlay->section == OVERLAY_EXTENSIONS &&
                       overlay->row == EXTENSION_SD_MAPPER) {
                bool configured = overlay->config->sd_mapper ||
                    overlay->config->sd_mapper_rom_path[0] ||
                    overlay->config->sd_card_path[0][0] ||
                    overlay->config->sd_card_path[1][0] ||
                    !overlay->config->sd_mapper_ram ||
                    overlay->config->sd_mapper_alternate_driver;

                if (!configured)
                    break;
                if (overlay->config->sd_mapper &&
                    !disconnect_sd_mapper(overlay))
                    break;
                overlay->config->sd_mapper_rom_path[0] = '\0';
                for (unsigned card = 0;
                     card < MSX_SD_MAPPER_CARDS; ++card)
                    overlay->config->sd_card_path[card][0] = '\0';
                overlay->config->sd_mapper_ram = true;
                overlay->config->sd_mapper_alternate_driver = false;
                overlay->dirty = true;
                apply_config(overlay);
                notify_post("SD Mapper V2 settings cleared");
            } else if (overlay->section == OVERLAY_EXTENSIONS &&
                       overlay->row == EXTENSION_MEGAFLASH) {
                bool configured = overlay->config->megaflash ||
                    overlay->config->megaflash_rom_path[0] ||
                    overlay->config->megaflash_card_path[0][0] ||
                    overlay->config->megaflash_card_path[1][0];

                if (!configured)
                    break;
                if (overlay->config->megaflash &&
                    !disconnect_megaflash(overlay))
                    break;
                overlay->config->megaflash_rom_path[0] = '\0';
                for (unsigned card = 0;
                     card < MSX_MEGAFLASH_CARDS; ++card)
                    overlay->config->megaflash_card_path[card][0] = '\0';
                discard_pending_megaflash_state(overlay);
                overlay->dirty = true;
                apply_config(overlay);
                notify_post("MegaFlashROM settings cleared");
            }
            break;
        case SDLK_ESCAPE:
            if (overlay->dirty)
                overlay->state = OVERLAY_STATE_CONFIRM;
            else
                close_overlay(overlay, false);
            break;
        default:
            break;
    }
    return true;
}

static const char *section_hint(const Overlay *overlay) {
    switch (overlay->section) {
        case OVERLAY_GENERAL:
            return "Machine, memory, audio, input, and optional controls.";
        case OVERLAY_MEDIA:
            return "Enter loads; R rewinds tape; Delete safely ejects.";
        case OVERLAY_EXTENSIONS:
            if (overlay->row == EXTENSION_TCPIP_UNAPI) {
                if (!overlay->config->tcpip_unapi)
                    return "Enable the host bridge, then run UNAPINET.COM in the guest.";
                if (unapinet_guest_driver_active(overlay->unapinet))
                    return "UNAPINET.COM is active; standard TCP/IP UNAPI is available.";
                return "Host bridge only; run UNAPINET.COM in Nextor/MSX-DOS 2.";
            }
            return "Enter enables/disables; Space edits; Delete clears.";
        case OVERLAY_ADVANCED:
            return "Machine models, RTC/media safety, and diagnostics.";
        case OVERLAY_SECTION_COUNT:
            break;
    }
    return "";
}

void overlay_tick(Overlay *overlay) {
    OverlayDialogTarget target;
    int slot;

    if (overlay->dialog_failed) {
        bool discard = overlay->dialog_discard;

        SDL_MemoryBarrierAcquire();
        overlay->dialog_failed = false;
        overlay->dialog_discard = false;
        overlay->dialog_target = OVERLAY_DIALOG_NONE;
        if (discard)
            return;
        notify_post("File picker unavailable: %s",
                    overlay->dialog_error[0]
                    ? overlay->dialog_error : "unknown SDL error");
        return;
    }
    if (!overlay->dialog_ready)
        return;
    SDL_MemoryBarrierAcquire();
    overlay->dialog_ready = false;
    target = overlay->dialog_target;
    overlay->dialog_target = OVERLAY_DIALOG_NONE;
    if (overlay->dialog_discard) {
        overlay->dialog_discard = false;
        return;
    }
    if (!overlay->visible || target == OVERLAY_DIALOG_NONE)
        return;
    if (!overlay->dialog_path[0]) {
        if (target == OVERLAY_DIALOG_MODEL_BIOS ||
            target == OVERLAY_DIALOG_MODEL_LOGO ||
            target == OVERLAY_DIALOG_MODEL_SUBROM ||
            target == OVERLAY_DIALOG_MODEL_DISK_ROM)
            notify_post("Model firmware selection cancelled");
        else if (target == OVERLAY_DIALOG_SUNRISE_ROM)
            notify_post("Sunrise IDE ROM selection cancelled");
        else if (target == OVERLAY_DIALOG_SD_MAPPER_ROM)
            notify_post("SD Mapper V2 ROM selection cancelled");
        else if (target == OVERLAY_DIALOG_MEGAFLASH_ROM)
            notify_post("MegaFlashROM image selection cancelled");
        else if (target == OVERLAY_DIALOG_SD_CARD_A ||
                 target == OVERLAY_DIALOG_SD_CARD_B ||
                 target == OVERLAY_DIALOG_MEGAFLASH_SD_A ||
                 target == OVERLAY_DIALOG_MEGAFLASH_SD_B)
            notify_post("SD card selection cancelled");
        else if (target == OVERLAY_DIALOG_IDE_IMAGE)
            notify_post("IDE disk selection cancelled");
        else if (target == OVERLAY_DIALOG_DRIVE_A)
            notify_post("Floppy A selection cancelled");
        else if (target == OVERLAY_DIALOG_DRIVE_B)
            notify_post("Floppy B selection cancelled");
        else if (target == OVERLAY_DIALOG_CASSETTE)
            notify_post("Cassette selection cancelled");
        return;
    }

    if (target == OVERLAY_DIALOG_CASSETTE) {
        if (msx_load_cassette(
                overlay->msx, overlay->dialog_path) != 0) {
            notify_post("Could not load MSX CAS image: %s",
                        path_basename(overlay->dialog_path));
            return;
        }
        snprintf(overlay->config->cassette_path,
                 sizeof(overlay->config->cassette_path), "%s",
                 overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        notify_post("%s cassette ready: %s",
                    cassette_file_type_name(
                        msx_cassette_file_type(overlay->msx)),
                    cassette_load_command(
                        msx_cassette_file_type(overlay->msx)));
        return;
    }

    if (target == OVERLAY_DIALOG_DRIVE_A) {
        if (msx_mount_drive_a(
                overlay->msx, overlay->dialog_path,
                overlay->config->floppy_image_mode) != 0) {
            notify_post("Could not mount Floppy A image: %s",
                        msx_drive_a_error(overlay->msx));
            return;
        }
        snprintf(overlay->config->drive_a_path,
                 sizeof(overlay->config->drive_a_path), "%s",
                 overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        notify_post("Floppy A mounted %s: %s",
                    floppy_mode_name(
                        overlay->config->floppy_image_mode),
                    path_basename(overlay->dialog_path));
        return;
    }

    if (target == OVERLAY_DIALOG_DRIVE_B) {
        if (!overlay->config->second_drive)
            return;
        if (msx_mount_drive_b(
                overlay->msx, overlay->dialog_path,
                overlay->config->floppy_image_mode) != 0) {
            notify_post("Could not mount Floppy B image: %s",
                        msx_drive_b_error(overlay->msx));
            return;
        }
        snprintf(overlay->config->drive_b_path,
                 sizeof(overlay->config->drive_b_path), "%s",
                 overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        notify_post("Floppy B mounted %s: %s",
                    floppy_mode_name(
                        overlay->config->floppy_image_mode),
                    path_basename(overlay->dialog_path));
        return;
    }

    if (target == OVERLAY_DIALOG_SUNRISE_ROM) {
        if (overlay->state != OVERLAY_STATE_SUNRISE_SETUP)
            return;
        if (!firmware_file_has_size(
                overlay->dialog_path, MSX_SUNRISE_ROM_SIZE)) {
            notify_post("Sunrise IDE needs an exact 128 KB ROM");
            return;
        }
        snprintf(overlay->pending_sunrise_rom_path,
                 sizeof(overlay->pending_sunrise_rom_path), "%s",
                 overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->sunrise_setup_row = SUNRISE_SETUP_DISK;
        notify_post("Sunrise IDE firmware selected: %s",
                    path_basename(overlay->dialog_path));
        return;
    }
    if (target == OVERLAY_DIALOG_SD_MAPPER_ROM) {
        if (overlay->state != OVERLAY_STATE_SD_MAPPER_SETUP)
            return;
        if (!sd_mapper_rom_file_is_valid(
                overlay->dialog_path)) {
            notify_post(
                "SD Mapper V2 needs an exact 128 or 256 KB ROM");
            return;
        }
        snprintf(overlay->pending_sd_mapper_rom_path,
                 sizeof(overlay->pending_sd_mapper_rom_path), "%s",
                 overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->sd_mapper_setup_row =
            SD_MAPPER_SETUP_CARD_A;
        notify_post("SD Mapper V2 firmware selected: %s",
                    path_basename(overlay->dialog_path));
        return;
    }
    if (target == OVERLAY_DIALOG_MEGAFLASH_ROM) {
        if (overlay->state != OVERLAY_STATE_MEGAFLASH_SETUP)
            return;
        if (!megaflash_rom_file_is_valid(
                overlay->dialog_path)) {
            notify_post(
                "MegaFlashROM SCC+ SD needs an image up to 8 MiB");
            return;
        }
        snprintf(overlay->pending_megaflash_rom_path,
                 sizeof(overlay->pending_megaflash_rom_path),
                 "%s", overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->megaflash_setup_row =
            MEGAFLASH_SETUP_CARD_A;
        notify_post("MegaFlashROM image selected: %s",
                    path_basename(overlay->dialog_path));
        return;
    }
    if (target == OVERLAY_DIALOG_SD_CARD_A ||
        target == OVERLAY_DIALOG_SD_CARD_B) {
        unsigned card =
            target == OVERLAY_DIALOG_SD_CARD_B ? 1u : 0u;

        if (!sd_image_file_is_valid(overlay->dialog_path)) {
            notify_post(
                "SD Card %c image must use complete 512-byte sectors",
                'A' + (int)card);
            return;
        }
        if (overlay->state ==
            OVERLAY_STATE_SD_MAPPER_SETUP) {
            snprintf(overlay->pending_sd_card_path[card],
                     sizeof(
                         overlay->pending_sd_card_path[card]),
                     "%s", overlay->dialog_path);
            copy_dirname(overlay->config->last_media_dir,
                         sizeof(
                             overlay->config->last_media_dir),
                         overlay->dialog_path);
            overlay->sd_mapper_setup_row =
                card ? SD_MAPPER_SETUP_RAM :
                       SD_MAPPER_SETUP_CARD_B;
            notify_post("SD Card %c selected: %s",
                        'A' + (int)card,
                        path_basename(overlay->dialog_path));
            return;
        }
        if (msx_mount_sd_card(
                overlay->msx, card, overlay->dialog_path,
                overlay->config->sd_image_mode) != 0) {
            notify_post("Could not mount SD Card %c: %s",
                        'A' + (int)card,
                        msx_sd_card_error(
                            overlay->msx, card));
            return;
        }
        snprintf(overlay->config->sd_card_path[card],
                 sizeof(overlay->config->sd_card_path[card]),
                 "%s", overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        notify_post("SD Card %c mounted %s: %s",
                    'A' + (int)card,
                    sd_mode_name(
                        overlay->config->sd_image_mode),
                    path_basename(overlay->dialog_path));
        return;
    }
    if (target == OVERLAY_DIALOG_MEGAFLASH_SD_A ||
        target == OVERLAY_DIALOG_MEGAFLASH_SD_B) {
        unsigned card =
            target == OVERLAY_DIALOG_MEGAFLASH_SD_B ? 1u : 0u;

        if (!sd_image_file_is_valid(overlay->dialog_path)) {
            notify_post(
                "MegaFlash SD %c must use complete 512-byte sectors",
                'A' + (int)card);
            return;
        }
        if (overlay->state ==
            OVERLAY_STATE_MEGAFLASH_SETUP) {
            snprintf(
                overlay->pending_megaflash_card_path[card],
                sizeof(
                    overlay->pending_megaflash_card_path[card]),
                "%s", overlay->dialog_path);
            copy_dirname(overlay->config->last_media_dir,
                         sizeof(
                             overlay->config->last_media_dir),
                         overlay->dialog_path);
            overlay->megaflash_setup_row =
                card ? MEGAFLASH_SETUP_CONNECT :
                       MEGAFLASH_SETUP_CARD_B;
            notify_post("MegaFlash SD %c selected: %s",
                        'A' + (int)card,
                        path_basename(overlay->dialog_path));
            return;
        }
        if (msx_mount_megaflash_card(
                overlay->msx, card, overlay->dialog_path,
                overlay->config->sd_image_mode) != 0) {
            notify_post(
                "Could not mount MegaFlash SD %c: %s",
                'A' + (int)card,
                msx_megaflash_card_error(
                    overlay->msx, card));
            return;
        }
        snprintf(overlay->config->megaflash_card_path[card],
                 sizeof(
                     overlay->config->megaflash_card_path[card]),
                 "%s", overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        notify_post("MegaFlash SD %c mounted %s: %s",
                    'A' + (int)card,
                    sd_mode_name(
                        overlay->config->sd_image_mode),
                    path_basename(overlay->dialog_path));
        return;
    }
    if (target == OVERLAY_DIALOG_IDE_IMAGE) {
        if (overlay->state == OVERLAY_STATE_SUNRISE_SETUP) {
            if (!ide_image_file_is_valid(overlay->dialog_path)) {
                notify_post(
                    "IDE image must use complete 512-byte sectors");
                return;
            }
            snprintf(overlay->pending_ide_image_path,
                     sizeof(overlay->pending_ide_image_path), "%s",
                     overlay->dialog_path);
            copy_dirname(overlay->config->last_media_dir,
                         sizeof(overlay->config->last_media_dir),
                         overlay->dialog_path);
            overlay->sunrise_setup_row =
                SUNRISE_SETUP_CONNECT;
            notify_post("IDE disk selected: %s",
                        path_basename(overlay->dialog_path));
            return;
        }
        if (msx_mount_sunrise_disk_mode(
                overlay->msx, overlay->dialog_path,
                overlay->config->ide_image_mode) != 0) {
            notify_post("Could not mount IDE image: %s",
                        msx_sunrise_disk_error(overlay->msx));
            return;
        }
        snprintf(overlay->config->ide_image_path,
                 sizeof(overlay->config->ide_image_path), "%s",
                 overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        notify_post("IDE disk mounted %s: %s",
                    overlay->config->ide_image_mode ==
                        ATA_IMAGE_READ_WRITE
                    ? "read/write" : "read-only",
                    path_basename(overlay->dialog_path));
        return;
    }

    if (target == OVERLAY_DIALOG_MODEL_BIOS ||
        target == OVERLAY_DIALOG_MODEL_LOGO ||
        target == OVERLAY_DIALOG_MODEL_SUBROM ||
        target == OVERLAY_DIALOG_MODEL_DISK_ROM) {
        char *destination = model_firmware_field(overlay, target);

        if (overlay->state != OVERLAY_STATE_MODEL_EDIT)
            return;
        if (destination)
            snprintf(destination, PATH_MAX, "%s",
                     overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->model_editor_error[0] = '\0';
        return;
    }

    slot = target == OVERLAY_DIALOG_CARTRIDGE_1 ? 0 :
           target == OVERLAY_DIALOG_CARTRIDGE_2 ? 1 : -1;
    if (slot < 0)
        return;
    {
        const char *owner =
            config_cartridge_slot_owner(overlay->config,
                                        (unsigned)slot);

        if (owner) {
            notify_post("Cartridge slot %d is now reserved by %s",
                        slot + 1, owner);
            return;
        }
    }
    if (msx_load_cartridge_slot(
            overlay->msx, (unsigned)slot, overlay->dialog_path,
            overlay->config->cartridge_mapper[slot]) != 0) {
        notify_post("Could not mount cartridge %d: %s",
                    slot + 1, path_basename(overlay->dialog_path));
        return;
    }
    snprintf(overlay->config->cartridge_path[slot],
             sizeof(overlay->config->cartridge_path[slot]), "%s",
             overlay->dialog_path);
    copy_dirname(overlay->config->last_media_dir,
                 sizeof(overlay->config->last_media_dir),
                 overlay->dialog_path);
    configure_leds(overlay->config, overlay->msx);
    overlay->dirty = true;
    notify_post("Cartridge %d mounted: %s (%s)", slot + 1,
                path_basename(overlay->dialog_path),
                msx_cartridge_mapper_display_name(
                    msx_get_cartridge(overlay->msx,
                                      (unsigned)slot)->mapper));
}

static void editor_shorten(char *destination, size_t destination_size,
                           const char *text, size_t maximum) {
    size_t length = text ? strlen(text) : 0;

    if (!text || !text[0]) {
        snprintf(destination, destination_size,
                 "[not configured]");
    } else if (length <= maximum) {
        snprintf(destination, destination_size, "%s", text);
    } else if (maximum > 3) {
        snprintf(destination, destination_size, "...%s",
                 text + length - (maximum - 3));
    } else {
        snprintf(destination, destination_size, "%.*s",
                 (int)maximum, text);
    }
}

static const char *model_field_name(int field) {
    switch (field) {
        case MODEL_FIELD_ID:       return "ID";
        case MODEL_FIELD_NAME:     return "Display name";
        case MODEL_FIELD_HARDWARE: return "Hardware";
        case MODEL_FIELD_BIOS:     return "BIOS";
        case MODEL_FIELD_LOGO:     return "Logo ROM";
        case MODEL_FIELD_SUBROM:   return "Sub-ROM";
        case MODEL_FIELD_DISK_ROM: return "Disk ROM";
    }
    return "";
}

static void model_field_value(const Overlay *overlay, int field,
                              char *value, size_t value_size) {
    switch (field) {
        case MODEL_FIELD_ID:
            snprintf(value, value_size, "%s",
                     overlay->model_edit.id);
            break;
        case MODEL_FIELD_NAME:
            snprintf(value, value_size, "%s",
                     overlay->model_edit.name);
            break;
        case MODEL_FIELD_HARDWARE:
            snprintf(value, value_size, "%s (%s)",
                     msx_model_name(overlay->model_edit.hardware),
                     msx_model_config_name(
                         overlay->model_edit.hardware));
            break;
        case MODEL_FIELD_BIOS:
            editor_shorten(value, value_size,
                           overlay->model_edit.bios_path, 56);
            break;
        case MODEL_FIELD_LOGO:
            editor_shorten(value, value_size,
                           overlay->model_edit.logo_path, 56);
            break;
        case MODEL_FIELD_SUBROM:
            editor_shorten(value, value_size,
                           overlay->model_edit.subrom_path, 56);
            break;
        case MODEL_FIELD_DISK_ROM:
            editor_shorten(value, value_size,
                           overlay->model_edit.disk_rom_path, 56);
            break;
    }
}

static void render_model_list(const Overlay *overlay,
                              SDL_Renderer *renderer) {
    int first = overlay->model_editor_row -
                MODEL_EDITOR_VISIBLE_ROWS / 2;
    int last;
    const float box_x = 10.0f;
    const float box_y = 18.0f;
    const float box_w = 620.0f;
    const float box_h = 444.0f;

    if (first < 0)
        first = 0;
    if (first + MODEL_EDITOR_VISIBLE_ROWS >
        (int)overlay->models->count)
        first = (int)overlay->models->count -
                MODEL_EDITOR_VISIBLE_ROWS;
    if (first < 0)
        first = 0;
    last = first + MODEL_EDITOR_VISIBLE_ROWS;
    if (last > (int)overlay->models->count)
        last = (int)overlay->models->count;

    ui_fill_rect(renderer, 0.0f, 0.0f,
                 (float)DISPLAY_LOGICAL_W,
                 (float)DISPLAY_LOGICAL_H,
                 0, 0, 0, 150);
    ui_fill_rect(renderer, box_x, box_y, box_w, box_h,
                 12, 14, 34, 255);
    ui_draw_rect(renderer, box_x, box_y, box_w, box_h,
                 80, 100, 210);
    ui_draw_text(renderer, 188.0f, 30.0f,
                 "Machine model editor", 255, 255, 255);
    ui_draw_text(renderer, 34.0f, 52.0f,
                 "ID                Name                         Hardware",
                 130, 155, 210);

    for (int model = first; model < last; ++model) {
        const ModelDefinition *definition =
            &overlay->models->entries[model];
        bool selected = model == overlay->model_editor_row;
        char line[96];
        float y = 72.0f + (float)(model - first) * 18.0f;

        snprintf(line, sizeof(line), "%-17.17s %-28.28s %s",
                 definition->id, definition->name,
                 msx_model_config_name(definition->hardware));
        if (selected)
            ui_draw_text(renderer, 20.0f, y, ">",
                         255, 255, 70);
        ui_draw_text(renderer, 34.0f, y, line,
                     selected ? 255 : 210,
                     selected ? 255 : 210,
                     selected ? 70 : 225);
    }
    ui_draw_text(renderer, 24.0f, 360.0f,
                 "Enter edit   Insert/A add   D duplicate   Delete remove",
                 180, 190, 220);
    ui_draw_text(renderer, 24.0f, 378.0f,
                 "Up/Down choose   Home/End jump   Esc return",
                 150, 170, 205);
    {
        char path[76];
        char line[96];

        editor_shorten(path, sizeof(path),
                       overlay->models->edit_path, 62);
        snprintf(line, sizeof(line), "Saves to: %s", path);
        ui_draw_text(renderer, 24.0f, 404.0f, line,
                     120, 190, 150);
    }
    if (overlay->model_editor_error[0])
        ui_draw_text(renderer, 24.0f, 432.0f,
                     overlay->model_editor_error,
                     255, 120, 120);
}

static void render_model_edit(const Overlay *overlay,
                              SDL_Renderer *renderer) {
    const char *title =
        overlay->model_edit_index >= 0
        ? "Edit machine model" : "Add machine model";

    ui_fill_rect(renderer, 0.0f, 0.0f,
                 (float)DISPLAY_LOGICAL_W,
                 (float)DISPLAY_LOGICAL_H,
                 0, 0, 0, 150);
    ui_fill_rect(renderer, 10.0f, 34.0f, 620.0f, 392.0f,
                 12, 14, 34, 255);
    ui_draw_rect(renderer, 10.0f, 34.0f, 620.0f, 392.0f,
                 80, 100, 210);
    ui_draw_text(renderer,
                 ((float)DISPLAY_LOGICAL_W -
                  (float)strlen(title) * 8.0f) * 0.5f,
                 48.0f, title, 255, 255, 255);

    for (int field = 0; field < MODEL_EDITOR_FIELDS; ++field) {
        char value[PATH_MAX + 64];
        float y = 80.0f + (float)field * 36.0f;
        bool selected = field == overlay->model_edit_field;

        model_field_value(overlay, field, value, sizeof(value));
        if (selected)
            ui_draw_text(renderer, 18.0f, y, ">",
                         255, 255, 70);
        ui_draw_text(renderer, 32.0f, y,
                     model_field_name(field),
                     selected ? 255 : 200,
                     selected ? 255 : 200,
                     selected ? 70 : 220);
        ui_draw_text(renderer, 144.0f, y, value,
                     selected ? 255 : 210,
                     selected ? 255 : 210,
                     selected ? 70 : 225);
    }
    ui_draw_text(renderer, 22.0f, 340.0f,
                 "Enter edit/choose   E edit text   Delete clears ROM path",
                 175, 190, 220);
    ui_draw_text(renderer, 22.0f, 358.0f,
                 "Left/Right changes hardware   F2 saves   Esc cancels",
                 150, 170, 205);
    ui_draw_text(renderer, 22.0f, 382.0f,
                 "Blank optional ROM fields leave that component disconnected.",
                 120, 190, 150);
    if (overlay->model_editor_error[0])
        ui_draw_text(renderer, 22.0f, 404.0f,
                     overlay->model_editor_error,
                     255, 120, 120);
}

static void render_model_text(const Overlay *overlay,
                              SDL_Renderer *renderer) {
    char shown[80];
    char title[80];
    size_t length = strlen(overlay->model_text);

    render_model_edit(overlay, renderer);
    if (length <= 68)
        snprintf(shown, sizeof(shown), "%s",
                 overlay->model_text);
    else
        snprintf(shown, sizeof(shown), "...%s",
                 overlay->model_text + length - 65);
    if (strlen(shown) + 1 < sizeof(shown))
        strcat(shown, "_");
    snprintf(title, sizeof(title), "Edit %s",
             model_field_name(overlay->model_text_field));
    ui_fill_rect(renderer, 18.0f, 176.0f, 604.0f, 112.0f,
                 24, 26, 62, 255);
    ui_draw_rect(renderer, 18.0f, 176.0f, 604.0f, 112.0f,
                 100, 125, 230);
    ui_draw_text(renderer, 30.0f, 190.0f, title,
                 255, 255, 255);
    ui_draw_text(renderer, 30.0f, 220.0f, shown,
                 180, 240, 180);
    ui_draw_text(renderer, 30.0f, 260.0f,
                 "Enter apply   Esc cancel   Delete/Ctrl+A clear",
                 210, 210, 120);
}

static void render_model_delete(const Overlay *overlay,
                                SDL_Renderer *renderer) {
    const ModelDefinition *definition =
        &overlay->models->entries[overlay->model_editor_row];
    char line[160];

    render_model_list(overlay, renderer);
    snprintf(line, sizeof(line), "Delete %s (%s)?",
             definition->name, definition->id);
    ui_fill_rect(renderer, 100.0f, 194.0f, 440.0f, 82.0f,
                 28, 20, 38, 255);
    ui_draw_rect(renderer, 100.0f, 194.0f, 440.0f, 82.0f,
                 220, 100, 120);
    ui_draw_text(renderer,
                 ((float)DISPLAY_LOGICAL_W -
                  (float)strlen(line) * 8.0f) * 0.5f,
                 212.0f, line, 255, 220, 220);
    ui_draw_text(renderer, 180.0f, 246.0f,
                 "Enter/Y delete   Esc/N cancel",
                 230, 210, 120);
}

static void render_sunrise_setup(const Overlay *overlay,
                                 SDL_Renderer *renderer) {
    static const char *labels[SUNRISE_SETUP_ROWS] = {
        "Firmware ROM", "IDE hard disk", "Action"
    };
    char firmware[52];
    char disk[52];
    const char *values[SUNRISE_SETUP_ROWS];
    const float box_x = 28.0f;
    const float box_y = 132.0f;
    const float box_w = 584.0f;
    const float box_h = 220.0f;
    const char *title = "Sunrise IDE setup";

    if (overlay->pending_sunrise_rom_path[0])
        editor_shorten(
            firmware, sizeof(firmware),
            path_basename(overlay->pending_sunrise_rom_path), 45);
    else
        snprintf(firmware, sizeof(firmware),
                 "[required - choose 128 KiB ROM]");
    if (overlay->pending_ide_image_path[0])
        editor_shorten(
            disk, sizeof(disk),
            path_basename(overlay->pending_ide_image_path), 45);
    else
        snprintf(disk, sizeof(disk), "[optional - no disk]");
    values[SUNRISE_SETUP_FIRMWARE] = firmware;
    values[SUNRISE_SETUP_DISK] = disk;
    values[SUNRISE_SETUP_CONNECT] =
        !overlay->extension_setup_editing ? "Connect Sunrise IDE" :
        overlay->config->sunrise_ide ? "Apply and reconnect" :
        "Save settings";

    ui_fill_rect(renderer, 0.0f, 0.0f,
                 (float)DISPLAY_LOGICAL_W, (float)DISPLAY_LOGICAL_H,
                 0, 0, 0, 150);
    ui_fill_rect(renderer, box_x, box_y, box_w, box_h,
                 20, 22, 52, 255);
    ui_draw_rect(renderer, box_x, box_y, box_w, box_h,
                 90, 110, 220);
    ui_draw_text(renderer,
                 box_x + (box_w - (float)strlen(title) * 8.0f) * 0.5f,
                 box_y + 12.0f, title, 255, 255, 255);
    ui_draw_text(renderer, box_x + 22.0f, box_y + 42.0f,
                 "The firmware ROM is the controller cartridge.",
                 180, 210, 235);
    ui_draw_text(renderer, box_x + 22.0f, box_y + 60.0f,
                 "The optional disk image is its attached drive.",
                 180, 210, 235);

    for (int row = 0; row < SUNRISE_SETUP_ROWS; ++row) {
        bool selected = row == overlay->sunrise_setup_row;
        Uint8 red = selected ? 255 : 210;
        Uint8 green = selected ? 255 : 210;
        Uint8 blue = selected ? 70 : 225;
        float y = box_y + 94.0f + row * 24.0f;

        if (selected)
            ui_draw_text(renderer, box_x + 12.0f, y, ">",
                         red, green, blue);
        ui_draw_text(renderer, box_x + 28.0f, y, labels[row],
                     red, green, blue);
        ui_draw_text(renderer, box_x + 172.0f, y, values[row],
                     red, green, blue);
    }
    ui_draw_text(renderer, box_x + 54.0f, box_y + box_h - 24.0f,
                 "Up/Down choose  Enter select  Delete clear  Esc cancel",
                 160, 180, 210);
}

static void render_sd_mapper_setup(const Overlay *overlay,
                                   SDL_Renderer *renderer) {
    static const char *labels[SD_MAPPER_SETUP_ROWS] = {
        "Firmware ROM", "SD Card A", "SD Card B",
        "512 KB mapper", "Driver bank", "Action"
    };
    char firmware[52];
    char card_a[52];
    char card_b[52];
    const char *values[SD_MAPPER_SETUP_ROWS];
    const float box_x = 28.0f;
    const float box_y = 92.0f;
    const float box_w = 584.0f;
    const float box_h = 306.0f;
    const char *title = "MSX SD Mapper V2 setup";

    if (overlay->pending_sd_mapper_rom_path[0])
        editor_shorten(
            firmware, sizeof(firmware),
            path_basename(
                overlay->pending_sd_mapper_rom_path), 42);
    else
        snprintf(firmware, sizeof(firmware),
                 "[required - choose 128/256 KiB ROM]");
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card) {
        char *shown = card ? card_b : card_a;

        if (overlay->pending_sd_card_path[card][0])
            editor_shorten(
                shown, 52,
                path_basename(
                    overlay->pending_sd_card_path[card]), 42);
        else
            snprintf(shown, 52, "[optional - empty]");
    }
    values[SD_MAPPER_SETUP_FIRMWARE] = firmware;
    values[SD_MAPPER_SETUP_CARD_A] = card_a;
    values[SD_MAPPER_SETUP_CARD_B] = card_b;
    values[SD_MAPPER_SETUP_RAM] =
        toggle_name(overlay->pending_sd_mapper_ram);
    values[SD_MAPPER_SETUP_DRIVER] =
        overlay->pending_sd_mapper_alternate_driver
        ? "Alternate (SW1 on)" : "Primary (SW1 off)";
    values[SD_MAPPER_SETUP_CONNECT] =
        !overlay->extension_setup_editing ? "Connect SD Mapper V2" :
        overlay->config->sd_mapper ? "Apply and reconnect" :
        "Save settings";

    ui_fill_rect(renderer, 0.0f, 0.0f,
                 (float)DISPLAY_LOGICAL_W,
                 (float)DISPLAY_LOGICAL_H,
                 0, 0, 0, 150);
    ui_fill_rect(renderer, box_x, box_y, box_w, box_h,
                 20, 22, 52, 255);
    ui_draw_rect(renderer, box_x, box_y, box_w, box_h,
                 90, 110, 220);
    ui_draw_text(
        renderer,
        box_x + (box_w - (float)strlen(title) * 8.0f) * 0.5f,
        box_y + 12.0f, title, 255, 255, 255);
    ui_draw_text(renderer, box_x + 22.0f, box_y + 42.0f,
                 "One cartridge provides two SD cards and mapper RAM.",
                 180, 210, 235);
    ui_draw_text(renderer, box_x + 22.0f, box_y + 60.0f,
                 "Images are removable media; the ROM is the controller.",
                 180, 210, 235);

    for (int row = 0; row < SD_MAPPER_SETUP_ROWS; ++row) {
        bool selected = row == overlay->sd_mapper_setup_row;
        Uint8 red = selected ? 255 : 210;
        Uint8 green = selected ? 255 : 210;
        Uint8 blue = selected ? 70 : 225;
        float y = box_y + 94.0f + row * 27.0f;

        if (selected)
            ui_draw_text(renderer, box_x + 12.0f, y, ">",
                         red, green, blue);
        ui_draw_text(renderer, box_x + 28.0f, y, labels[row],
                     red, green, blue);
        ui_draw_text(renderer, box_x + 172.0f, y, values[row],
                     red, green, blue);
    }
    ui_draw_text(renderer, box_x + 42.0f,
                 box_y + box_h - 24.0f,
                 "Up/Down choose  Enter select  Delete clear  Esc cancel",
                 160, 180, 210);
}

static void render_megaflash_setup(const Overlay *overlay,
                                   SDL_Renderer *renderer) {
    static const char *labels[MEGAFLASH_SETUP_ROWS] = {
        "Initial flash", "SD Card A", "SD Card B", "Action"
    };
    char firmware[52];
    char card_a[52];
    char card_b[52];
    const char *values[MEGAFLASH_SETUP_ROWS];
    const float box_x = 28.0f;
    const float box_y = 92.0f;
    const float box_w = 584.0f;
    const float box_h = 270.0f;
    const char *title = "MegaFlashROM SCC+ SD setup";

    if (overlay->pending_megaflash_rom_path[0])
        editor_shorten(
            firmware, sizeof(firmware),
            path_basename(
                overlay->pending_megaflash_rom_path), 42);
    else
        snprintf(firmware, sizeof(firmware),
                 "[required - choose flash image]");
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        char *shown = card ? card_b : card_a;

        if (overlay->pending_megaflash_card_path[card][0])
            editor_shorten(
                shown, 52,
                path_basename(
                    overlay->pending_megaflash_card_path[card]), 42);
        else
            snprintf(shown, 52, "[optional - empty]");
    }
    values[MEGAFLASH_SETUP_FIRMWARE] = firmware;
    values[MEGAFLASH_SETUP_CARD_A] = card_a;
    values[MEGAFLASH_SETUP_CARD_B] = card_b;
    values[MEGAFLASH_SETUP_CONNECT] =
        !overlay->extension_setup_editing ? "Connect MegaFlashROM" :
        overlay->config->megaflash ? "Apply and reconnect" :
        "Save settings";

    ui_fill_rect(renderer, 0.0f, 0.0f,
                 (float)DISPLAY_LOGICAL_W,
                 (float)DISPLAY_LOGICAL_H,
                 0, 0, 0, 150);
    ui_fill_rect(renderer, box_x, box_y, box_w, box_h,
                 20, 22, 52, 255);
    ui_draw_rect(renderer, box_x, box_y, box_w, box_h,
                 90, 110, 220);
    ui_draw_text(
        renderer,
        box_x + (box_w - (float)strlen(title) * 8.0f) * 0.5f,
        box_y + 12.0f, title, 255, 255, 255);
    ui_draw_text(renderer, box_x + 22.0f, box_y + 42.0f,
                 "8 MiB flash, SCC-I, PSG, mapper RAM, and two SD slots.",
                 180, 210, 235);
    ui_draw_text(renderer, box_x + 22.0f, box_y + 60.0f,
                 "Guest flash writes use an atomic private state file.",
                 180, 210, 235);

    for (int row = 0; row < MEGAFLASH_SETUP_ROWS; ++row) {
        bool selected = row == overlay->megaflash_setup_row;
        Uint8 red = selected ? 255 : 210;
        Uint8 green = selected ? 255 : 210;
        Uint8 blue = selected ? 70 : 225;
        float y = box_y + 94.0f + row * 29.0f;

        if (selected)
            ui_draw_text(renderer, box_x + 12.0f, y, ">",
                         red, green, blue);
        ui_draw_text(renderer, box_x + 28.0f, y, labels[row],
                     red, green, blue);
        ui_draw_text(renderer, box_x + 172.0f, y, values[row],
                     red, green, blue);
    }
    ui_draw_text(renderer, box_x + 42.0f,
                 box_y + box_h - 24.0f,
                 "Up/Down choose  Enter select  Delete clear  Esc cancel",
                 160, 180, 210);
}

void overlay_render_cassette_scope(const Overlay *overlay) {
    enum { SCOPE_SAMPLES = 608 };
    SDL_Renderer *renderer;
    SDL_FPoint points[SCOPE_SAMPLES];
    s16 samples[SCOPE_SAMPLES];
    size_t sample_count;
    u64 position;
    u64 duration;
    char status[96];
    const float panel_x = 10.0f;
    const float panel_y = 408.0f;
    const float panel_w = 620.0f;
    const float panel_h = 62.0f;
    const float plot_x = panel_x + 6.0f;
    const float plot_y = panel_y + 23.0f;
    const float plot_w = panel_w - 12.0f;
    const float plot_h = panel_h - 29.0f;
    const float center_y = plot_y + plot_h * 0.5f;

    if (!overlay || !overlay->display || !overlay->msx ||
        overlay->visible || !overlay->config->tinker ||
        !overlay->config->cassette_visual_monitor ||
        !msx_cassette_rolling(overlay->msx))
        return;
    renderer = overlay->display->renderer;
    position = msx_cassette_position_ms(overlay->msx) / 1000u;
    duration = msx_cassette_duration_ms(overlay->msx) / 1000u;
    snprintf(
        status, sizeof(status),
        "CAS %s  %02llu:%02llu / %02llu:%02llu  %s  AUDIO %s",
        cassette_file_type_name(
            msx_cassette_file_type(overlay->msx)),
        (unsigned long long)(position / 60u),
        (unsigned long long)(position % 60u),
        (unsigned long long)(duration / 60u),
        (unsigned long long)(duration % 60u),
        cassette_load_command(
            msx_cassette_file_type(overlay->msx)),
        overlay->config->cassette_audible_monitor ? "ON" : "OFF");

    ui_fill_rect(renderer, panel_x, panel_y, panel_w, panel_h,
                 0, 0, 0, 180);
    ui_draw_rect(renderer, panel_x, panel_y, panel_w, panel_h,
                 235, 145, 45);
    ui_draw_text(renderer, panel_x + 6.0f, panel_y + 6.0f,
                 status, 255, 205, 110);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 135, 145, 165, 120);
    SDL_RenderLine(renderer, plot_x, center_y,
                   plot_x + plot_w, center_y);

    sample_count = msx_cassette_waveform_copy(
        overlay->msx, samples, SDL_arraysize(samples));
    if (sample_count >= 2) {
        float amplitude = plot_h * 0.5f - 2.0f;

        for (size_t i = 0; i < sample_count; ++i) {
            points[i].x = plot_x +
                (float)i * plot_w / (float)(sample_count - 1u);
            points[i].y = center_y -
                (float)samples[i] * amplitude / 32768.0f;
        }
        SDL_SetRenderDrawColor(renderer, 255, 180, 70, 235);
        SDL_RenderLines(renderer, points, (int)sample_count);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void overlay_render_content(const Overlay *overlay,
                                   float view_w, float view_h) {
    SDL_Renderer *renderer;
    int rows;
    int panel_h;
    int tab_x = 20;

    renderer = overlay->display->renderer;
    rows = section_rows(overlay, overlay->section);
    panel_h = OVERLAY_FIRST_Y + rows * OVERLAY_LINE_H + 54;

    ui_fill_rect(renderer, 0.0f, 0.0f, view_w, view_h,
                 0, 0, 0, 110);
    ui_fill_rect(renderer, 8.0f, 8.0f, view_w - 16.0f, (float)panel_h,
                 8, 10, 24, 235);
    ui_draw_rect(renderer, 8.0f, 8.0f, view_w - 16.0f, (float)panel_h,
                 70, 90, 180);

    for (int section = 0; section < OVERLAY_SECTION_COUNT; ++section) {
        const char *name;
        bool active;
        if (!section_available(overlay, (OverlaySection)section))
            continue;
        name = section_name((OverlaySection)section);
        active = overlay->section == (OverlaySection)section;
        ui_draw_text(renderer, (float)tab_x, 18.0f, name,
                     active ? 255 : 160,
                     active ? 255 : 160,
                     active ? 80 : 180);
        tab_x += (int)strlen(name) * 8 + 20;
    }

    for (int row = 0; row < rows; ++row) {
        char label[64];
        char value[160];
        float y = (float)(OVERLAY_FIRST_Y + row * OVERLAY_LINE_H);
        bool selected = row == overlay->row;
        Uint8 red = selected ? 255 : 210;
        Uint8 green = selected ? 255 : 210;
        Uint8 blue = selected ? 70 : 225;

        item_text(overlay, row, label, sizeof(label), value, sizeof(value));
        if (selected)
            ui_draw_text(renderer, 16.0f, y, ">", red, green, blue);
        ui_draw_text(renderer, OVERLAY_LABEL_X, y, label,
                     red, green, blue);
        ui_draw_text(renderer, OVERLAY_VALUE_X, y, value,
                     red, green, blue);
    }

    ui_draw_text(renderer, 20.0f,
                 (float)(OVERLAY_FIRST_Y + rows * OVERLAY_LINE_H + 8),
                 "Left/Right tabs  Up/Down row  Enter select  Esc cancel",
                 150, 160, 190);
    ui_draw_text(renderer, 20.0f,
                 (float)(OVERLAY_FIRST_Y + rows * OVERLAY_LINE_H + 25),
                 section_hint(overlay), 120, 180, 150);

    if (overlay->state == OVERLAY_STATE_SUNRISE_SETUP) {
        render_sunrise_setup(overlay, renderer);
        return;
    }
    if (overlay->state == OVERLAY_STATE_SD_MAPPER_SETUP) {
        render_sd_mapper_setup(overlay, renderer);
        return;
    }
    if (overlay->state == OVERLAY_STATE_MEGAFLASH_SETUP) {
        render_megaflash_setup(overlay, renderer);
        return;
    }
    if (overlay->state == OVERLAY_STATE_MODEL_LIST) {
        render_model_list(overlay, renderer);
        return;
    }
    if (overlay->state == OVERLAY_STATE_MODEL_EDIT) {
        render_model_edit(overlay, renderer);
        return;
    }
    if (overlay->state == OVERLAY_STATE_MODEL_TEXT) {
        render_model_text(overlay, renderer);
        return;
    }
    if (overlay->state == OVERLAY_STATE_MODEL_DELETE) {
        render_model_delete(overlay, renderer);
        return;
    }
    if (overlay->state == OVERLAY_STATE_MACHINE) {
        const char *title = "Choose machine profile";
        const char *hint = "Up/Down choose   Enter select   Esc cancel";
        const int visible_rows = 10;
        int first_model = overlay->machine_row - visible_rows / 2;
        int last_model;
        float box_w = 500.0f;
        float box_h;
        float box_x = (view_w - box_w) * 0.5f;
        float box_y;

        if (first_model < 0)
            first_model = 0;
        if (first_model + visible_rows > (int)overlay->models->count)
            first_model = (int)overlay->models->count - visible_rows;
        if (first_model < 0)
            first_model = 0;
        last_model = first_model + visible_rows;
        if (last_model > (int)overlay->models->count)
            last_model = (int)overlay->models->count;
        box_h = 74.0f + (last_model - first_model) * 18.0f;
        box_y = (view_h - box_h) * 0.5f;

        ui_fill_rect(renderer, 0.0f, 0.0f, view_w, view_h,
                     0, 0, 0, 130);
        ui_fill_rect(renderer, box_x, box_y, box_w, box_h,
                     20, 22, 52, 255);
        ui_draw_rect(renderer, box_x, box_y, box_w, box_h,
                     90, 110, 220);
        ui_draw_text(renderer,
                     box_x + (box_w - (float)strlen(title) * 8.0f) * 0.5f,
                     box_y + 10.0f, title, 255, 255, 255);
        for (int model = first_model; model < last_model; ++model) {
            const ModelDefinition *definition =
                &overlay->models->entries[model];
            char line[160];
            bool selected = model == overlay->machine_row;
            bool has_subrom = definition->subrom_path[0];
            bool has_disk_rom = definition->disk_rom_path[0];

            snprintf(line, sizeof(line), "%s%s",
                     definition->name,
                     has_disk_rom
                     ? "  [BIOS + Sub-ROM + disk ROM]" :
                     has_subrom
                     ? "  [BIOS + Sub-ROM]" : "  [BIOS]");
            if (selected)
                ui_draw_text(renderer, box_x + 18.0f,
                             box_y + 38.0f +
                             (model - first_model) * 18.0f,
                             ">", 255, 255, 70);
            ui_draw_text(renderer, box_x + 32.0f,
                         box_y + 38.0f +
                         (model - first_model) * 18.0f,
                         line,
                         selected ? 255 : 210,
                         selected ? 255 : 210,
                         selected ? 70 : 225);
        }
        ui_draw_text(renderer,
                     box_x + (box_w - (float)strlen(hint) * 8.0f) * 0.5f,
                     box_y + box_h - 20.0f, hint, 160, 180, 210);
        return;
    }

    if (overlay->state == OVERLAY_STATE_CONFIRM) {
        const char *line1 = "Save changes?";
        const char *line2 = "Enter/Y = Save     Esc/N = Discard";
        float box_w = 320.0f;
        float box_h = 62.0f;
        float box_x = (view_w - box_w) * 0.5f;
        float box_y = (view_h - box_h) * 0.5f;
        ui_fill_rect(renderer, 0.0f, 0.0f, view_w, view_h,
                     0, 0, 0, 130);
        ui_fill_rect(renderer, box_x, box_y, box_w, box_h,
                     20, 22, 52, 255);
        ui_draw_rect(renderer, box_x, box_y, box_w, box_h,
                     90, 110, 220);
        ui_draw_text(renderer,
                     box_x + (box_w - (float)strlen(line1) * 8.0f) * 0.5f,
                     box_y + 12.0f, line1, 255, 255, 255);
        ui_draw_text(renderer,
                     box_x + (box_w - (float)strlen(line2) * 8.0f) * 0.5f,
                     box_y + 36.0f, line2, 220, 220, 120);
    }
}

void overlay_render(const Overlay *overlay) {
    DisplayLayout layout;
    SDL_Rect viewport;
    SDL_Renderer *renderer;
    float scale = OVERLAY_RENDER_SCALE;
    float fit_scale;
    float view_w;
    float view_h;
    int output_w;
    int output_h;

    if (!overlay || !overlay->visible || !overlay->display)
        return;
    renderer = overlay->display->renderer;
    if (!SDL_GetRenderOutputSize(renderer, &output_w, &output_h))
        SDL_GetWindowSize(overlay->display->window, &output_w, &output_h);
    display_calculate_layout(output_w, output_h, &layout);
    fit_scale = (float)layout.screen_w / (float)DISPLAY_LOGICAL_W;
    if ((float)layout.screen_h / (float)DISPLAY_LOGICAL_H < fit_scale)
        fit_scale =
            (float)layout.screen_h / (float)DISPLAY_LOGICAL_H;
    if (scale > fit_scale)
        scale = fit_scale;
    if (scale <= 0.0f)
        return;
    view_w = (float)layout.screen_w / scale;
    view_h = (float)layout.screen_h / scale;
    viewport = (SDL_Rect) {
        layout.screen_x, layout.screen_y,
        layout.screen_w, layout.screen_h
    };
    SDL_SetRenderViewport(renderer, &viewport);
    SDL_SetRenderScale(renderer, scale, scale);
    overlay_render_content(overlay, view_w, view_h);
    SDL_SetRenderViewport(renderer, NULL);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}
