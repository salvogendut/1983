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

#define OVERLAY_LINE_H 18
#define OVERLAY_LABEL_X 28
#define OVERLAY_VALUE_X 188
#define OVERLAY_FIRST_Y 48
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
    MODEL_FIELD_ID = 0,
    MODEL_FIELD_NAME,
    MODEL_FIELD_HARDWARE,
    MODEL_FIELD_BIOS,
    MODEL_FIELD_LOGO,
    MODEL_FIELD_SUBROM,
    MODEL_FIELD_DISK_ROM
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
            return overlay->config->sunrise_ide ? 8 : 7;
        case OVERLAY_EXTENSIONS: return 4;
        case OVERLAY_ADVANCED:   return 7;
        case OVERLAY_SECTION_COUNT: break;
    }
    return 0;
}

static const char *toggle_name(bool enabled) {
    return enabled ? "On" : "Off";
}

static const char *stub_toggle_name(bool enabled) {
    return enabled ? "On (device stub)" : "Off";
}

static const char *input_port_name(InputPort port) {
    return port == INPUT_PORT_B ? "Joy Port B" : "Joy Port A";
}

static const char *joy_port_device_name(JoyPortDevice device) {
    return device == JOY_PORT_MOUSE ? "Mouse" : "Joystick";
}

static const char *path_basename(const char *path);

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
        snprintf(value, value_size, "Off");
    } else if (slot < 0) {
        snprintf(value, value_size, "Off (no free cartridge slot)");
    } else if (!msx_sunrise_connected(overlay->msx)) {
        snprintf(value, value_size,
                 "On (Cartridge %d, ROM not loaded)", slot + 1);
    } else {
        snprintf(value, value_size, "On (Cartridge %d) - %s",
                 slot + 1,
                 path_basename(config->sunrise_rom_path));
    }
}

static void ide_image_text(const Overlay *overlay,
                           char *value, size_t value_size) {
    const char *path = overlay->config->ide_image_path;

    if (msx_sunrise_disk_mounted(overlay->msx))
        snprintf(value, value_size, "%s [read-only]",
                 path_basename(path));
    else if (path[0])
        snprintf(value, value_size, "%s [not mounted]",
                 path_basename(path));
    else
        snprintf(value, value_size, "[not mounted]");
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
            switch (row) {
                case 0:
                    snprintf(label, label_size, "Cartridge 1");
                    cartridge_text(overlay, 0, value, value_size);
                    break;
                case 1:
                    snprintf(label, label_size, "Cart 1 mapper");
                    mapper_text(overlay, 0, value, value_size);
                    break;
                case 2:
                    snprintf(label, label_size, "Cartridge 2");
                    cartridge_text(overlay, 1, value, value_size);
                    break;
                case 3:
                    snprintf(label, label_size, "Cart 2 mapper");
                    mapper_text(overlay, 1, value, value_size);
                    break;
                case 4:
                    snprintf(label, label_size, "Cassette");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
                case 5:
                    snprintf(label, label_size, "Drive A");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
                case 6:
                    snprintf(label, label_size, "Drive B");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
                case 7:
                    snprintf(label, label_size, "IDE hard disk");
                    ide_image_text(overlay, value, value_size);
                    break;
            }
            break;
        case OVERLAY_EXTENSIONS:
            switch (row) {
                case 0:
                    snprintf(label, label_size, "Second floppy drive");
                    snprintf(value, value_size, "%s",
                             stub_toggle_name(config->second_drive));
                    break;
                case 1:
                    snprintf(label, label_size, "Sunrise IDE");
                    sunrise_extension_text(
                        overlay, value, value_size);
                    break;
                case 2:
                    snprintf(label, label_size, "Konami SCC");
                    cartridge_extension_text(
                        config, "Konami SCC", config->scc,
                        value, value_size);
                    break;
                case 3:
                    snprintf(label, label_size, "MSX-MUSIC");
                    cartridge_extension_text(
                        config, "MSX-MUSIC", config->msx_music,
                        value, value_size);
                    break;
            }
            break;
        case OVERLAY_ADVANCED:
            switch (row) {
                case 0:
                    snprintf(label, label_size,
                             "Machine model editor");
                    snprintf(value, value_size, "%zu models",
                             overlay->models->count);
                    break;
                case 1:
                    snprintf(label, label_size, "Smoothing");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->smoothing));
                    break;
                case 2:
                    snprintf(label, label_size, "Real CRT");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->real_crt));
                    break;
                case 3:
                    snprintf(label, label_size, "CRT scanlines");
                    snprintf(value, value_size,
                             config->real_crt ? "%d%%" : "%d%% (inactive)",
                             config->crt_scanlines);
                    break;
                case 4:
                    snprintf(label, label_size, "Notifications");
                    snprintf(value, value_size, "%s",
                             notification_name(config->notifications));
                    break;
                case 5:
                    snprintf(label, label_size, "Debug overlay");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->debug));
                    break;
                case 6:
                    snprintf(label, label_size, "Version");
                    snprintf(value, value_size, "0.1.0 (git %s)",
                             PROG_GIT_COMMIT);
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

static void apply_config(Overlay *overlay) {
    Config *config = overlay->config;
    MsxMachine *msx = overlay->msx;
    bool machine_changed =
        msx->profile->model != config->model ||
        msx->region != config->region ||
        msx->ram_kb != config->memory_kb;

    config_normalize(config);
    if (machine_changed) {
        msx_configure(msx, config->model, config->region,
                      config->memory_kb);
        config->memory_kb = msx->ram_kb;
        display_prepare_scaffold(overlay->display, msx);
        display_set_title(overlay->display, msx,
                          selected_model_name(overlay));
    }
    display_set_smoothing(overlay->display, config->smoothing);
    display_set_crt(overlay->display, config->real_crt,
                    config->crt_scanlines);
    psg_set_volume(&msx->psg, config->audio_volume);
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

static void restore_sunrise(Overlay *overlay) {
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
        msx_sunrise_slot(overlay->msx) !=
            (saved->sunrise_ide ? saved_slot : -1);

    if (!changed)
        return;
    msx_eject_sunrise_ide(overlay->msx);
    if (!saved->sunrise_ide || saved_slot < 0)
        return;
    if (msx_load_sunrise_ide(
            overlay->msx, (unsigned)saved_slot,
            saved->sunrise_rom_path) != 0) {
        notify_post("Could not restore Sunrise IDE ROM");
        return;
    }
    if (saved->ide_image_path[0] &&
        msx_mount_sunrise_disk(
            overlay->msx, saved->ide_image_path) != 0)
        notify_post("Could not restore Sunrise IDE disk");
}

static void close_overlay(Overlay *overlay, bool save) {
    if (overlay->state == OVERLAY_STATE_MODEL_TEXT &&
        overlay->display && overlay->display->window)
        SDL_StopTextInput(overlay->display->window);
    if (save) {
        apply_config(overlay);
        if (overlay->dirty) {
            if (config_save(overlay->config) == 0)
                notify_post("Settings saved");
            else
                notify_post("Could not save settings");
        }
    } else {
        restore_cartridges(overlay);
        restore_sunrise(overlay);
        restore_firmware(overlay);
        *overlay->config = overlay->saved;
        apply_config(overlay);
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

static void open_ide_image_dialog(Overlay *overlay) {
    static const SDL_DialogFileFilter filters[] = {
        { "Raw IDE disk images", "img;IMG;dsk;DSK;hdd;HDD" },
        { "All files", "*" },
    };
    const char *location =
        overlay->config->last_media_dir[0]
        ? overlay->config->last_media_dir : NULL;

    if (!msx_sunrise_connected(overlay->msx)) {
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

static void open_firmware_dialog(Overlay *overlay,
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
        target == OVERLAY_DIALOG_BIOS
        ? bios_filters : extension_filters;
    const char *location =
        overlay->pending_firmware_dir[0]
        ? overlay->pending_firmware_dir : NULL;
    const char *component =
        target == OVERLAY_DIALOG_BIOS ? "main BIOS" :
        target == OVERLAY_DIALOG_LOGO ? "C-BIOS logo ROM" :
        target == OVERLAY_DIALOG_SUBROM ? "Sub-ROM" : "disk ROM";
    const ModelDefinition *definition =
        &overlay->models->entries[overlay->pending_model_index];

    if (overlay->dialog_target != OVERLAY_DIALOG_NONE)
        return;
    overlay->dialog_target = target;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    notify_post("Select the %s for %s", component,
                definition->name);
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

static void continue_firmware_selection(Overlay *overlay);

static void begin_firmware_selection(Overlay *overlay) {
    const ModelDefinition *definition;

    overlay->pending_model_index = (size_t)overlay->machine_row;
    definition = &overlay->models->entries[overlay->pending_model_index];
    overlay->pending_model = definition->hardware;
    snprintf(overlay->pending_bios_path,
             sizeof(overlay->pending_bios_path), "%s",
             definition->bios_path);
    snprintf(overlay->pending_logo_path,
             sizeof(overlay->pending_logo_path), "%s",
             definition->logo_path);
    snprintf(overlay->pending_subrom_path,
             sizeof(overlay->pending_subrom_path), "%s",
             definition->subrom_path);
    snprintf(overlay->pending_disk_rom_path,
             sizeof(overlay->pending_disk_rom_path), "%s",
             definition->disk_rom_path);
    snprintf(overlay->pending_firmware_dir,
             sizeof(overlay->pending_firmware_dir), "%s",
             overlay->config->last_media_dir);
    if (definition->bios_path[0])
        copy_dirname(overlay->pending_firmware_dir,
                     sizeof(overlay->pending_firmware_dir),
                     definition->bios_path);
    overlay->state = OVERLAY_STATE_MENU;
    continue_firmware_selection(overlay);
}

static void finish_firmware_selection(Overlay *overlay) {
    const ModelDefinition *definition =
        &overlay->models->entries[overlay->pending_model_index];
    Config *config = overlay->config;

    if (msx_load_firmware_set(
            overlay->msx, overlay->pending_bios_path,
            overlay->pending_logo_path,
            overlay->pending_subrom_path,
            overlay->pending_disk_rom_path) != 0) {
        notify_post("Could not load %s firmware; check ROM sizes",
                    definition->name);
        return;
    }

    config->model = overlay->pending_model;
    snprintf(config->machine_id, sizeof(config->machine_id),
             "%s", definition->id);
    config->memory_kb = msx_default_ram_kb(config->model);
    snprintf(config->bios_path, sizeof(config->bios_path), "%s",
             overlay->pending_bios_path);
    snprintf(config->logo_path, sizeof(config->logo_path), "%s",
             overlay->pending_logo_path);
    snprintf(config->subrom_path, sizeof(config->subrom_path), "%s",
             overlay->pending_subrom_path);
    snprintf(config->disk_rom_path, sizeof(config->disk_rom_path), "%s",
             overlay->pending_disk_rom_path);
    snprintf(config->last_media_dir, sizeof(config->last_media_dir), "%s",
             overlay->pending_firmware_dir);
    overlay->dirty = true;
    apply_config(overlay);
    display_prepare_scaffold(overlay->display, overlay->msx);
    display_set_title(overlay->display, overlay->msx,
                      definition->name);
    notify_post("%s firmware loaded: %s", definition->name,
                path_basename(config->bios_path));
}

static void continue_firmware_selection(Overlay *overlay) {
    const MsxProfile *profile = msx_profile(overlay->pending_model);

    if (!firmware_file_has_size(
            overlay->pending_bios_path, MSX_BIOS_SIZE)) {
        overlay->pending_bios_path[0] = '\0';
        open_firmware_dialog(overlay, OVERLAY_DIALOG_BIOS);
        return;
    }
    if (overlay->pending_logo_path[0] &&
        !firmware_file_has_size(
            overlay->pending_logo_path, MSX_LOGO_SIZE)) {
        overlay->pending_logo_path[0] = '\0';
        open_firmware_dialog(overlay, OVERLAY_DIALOG_LOGO);
        return;
    }
    if ((profile->requires_subrom ||
         overlay->pending_subrom_path[0]) &&
        !firmware_file_has_size(
            overlay->pending_subrom_path, MSX_SUBROM_SIZE)) {
        overlay->pending_subrom_path[0] = '\0';
        open_firmware_dialog(overlay, OVERLAY_DIALOG_SUBROM);
        return;
    }
    if ((profile->requires_disk_rom ||
         overlay->pending_disk_rom_path[0]) &&
        !firmware_file_has_size(
            overlay->pending_disk_rom_path, MSX_DISK_ROM_SIZE)) {
        overlay->pending_disk_rom_path[0] = '\0';
        open_firmware_dialog(overlay, OVERLAY_DIALOG_DISK_ROM);
        return;
    }
    finish_firmware_selection(overlay);
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
    return true;
}

static bool connect_sunrise(Overlay *overlay, const char *rom_path) {
    Config *config = overlay->config;
    int slot;

    if (!firmware_file_has_size(
            rom_path, MSX_SUNRISE_ROM_SIZE)) {
        notify_post("Sunrise IDE needs an exact 128 KB ROM");
        return false;
    }
    if (!toggle_cartridge_extension(
            overlay, &config->sunrise_ide, "Sunrise IDE"))
        return false;
    slot = cartridge_extension_slot(config, "Sunrise IDE");
    if (slot < 0 || msx_load_sunrise_ide(
            overlay->msx, (unsigned)slot, rom_path) != 0) {
        config->sunrise_ide = false;
        notify_post("Could not load Sunrise IDE ROM: %s",
                    path_basename(rom_path));
        return false;
    }
    snprintf(config->sunrise_rom_path,
             sizeof(config->sunrise_rom_path), "%s", rom_path);
    if (config->ide_image_path[0] &&
        msx_mount_sunrise_disk(
            overlay->msx, config->ide_image_path) != 0)
        notify_post("Sunrise connected; could not mount IDE image: %s",
                    path_basename(config->ide_image_path));
    else
        notify_post("Sunrise ROM loaded: %s",
                    path_basename(rom_path));
    return true;
}

static void disconnect_sunrise(Overlay *overlay) {
    msx_eject_sunrise_ide(overlay->msx);
    (void)toggle_cartridge_extension(
        overlay, &overlay->config->sunrise_ide, "Sunrise IDE");
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
            static const char *media[] = {
                "", "", "", "", "Cassette", "Drive A", "Drive B",
                "IDE hard disk"
            };
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
            if (overlay->row == 7) {
                open_ide_image_dialog(overlay);
                return;
            }
            notify_post("%s loading is not implemented yet",
                        media[overlay->row]);
            return;
        }
        case OVERLAY_EXTENSIONS:
            switch (overlay->row) {
                case 0: config->second_drive = !config->second_drive; break;
                case 1:
                    if (config->sunrise_ide) {
                        disconnect_sunrise(overlay);
                    } else if (firmware_file_has_size(
                                   config->sunrise_rom_path,
                                   MSX_SUNRISE_ROM_SIZE)) {
                        if (!connect_sunrise(
                                overlay,
                                config->sunrise_rom_path))
                            return;
                    } else {
                        open_sunrise_rom_dialog(overlay);
                        return;
                    }
                    break;
                case 2:
                    if (!toggle_cartridge_extension(
                            overlay, &config->scc,
                            "Konami SCC"))
                        return;
                    break;
                case 3:
                    if (!toggle_cartridge_extension(
                            overlay, &config->msx_music,
                            "MSX-MUSIC"))
                        return;
                    break;
            }
            break;
        case OVERLAY_ADVANCED:
            switch (overlay->row) {
                case 0:
                    begin_model_editor(overlay);
                    return;
                case 1: config->smoothing = !config->smoothing; break;
                case 2: config->real_crt = !config->real_crt; break;
                case 3:
                    config->crt_scanlines += 5;
                    if (config->crt_scanlines > 95)
                        config->crt_scanlines = 0;
                    break;
                case 4: change_notification_mode(config); break;
                case 5: config->debug = !config->debug; break;
                case 6: return;
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
                  MsxMachine *msx) {
    memset(overlay, 0, sizeof(*overlay));
    overlay->dialog_target = OVERLAY_DIALOG_NONE;
    overlay->config = config;
    overlay->models = models;
    overlay->display = display;
    overlay->msx = msx;
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
            close_overlay(overlay, true);
        }
        return true;
    }
    if (!overlay->visible)
        return false;
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
            begin_firmware_selection(overlay);
        } else if (key == SDLK_ESCAPE) {
            overlay->state = OVERLAY_STATE_MENU;
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
        case SDLK_DELETE:
            if (overlay->section == OVERLAY_GENERAL &&
                overlay->row == GENERAL_MACHINE) {
                msx_eject_firmware(overlay->msx);
                overlay->config->bios_path[0] = '\0';
                overlay->config->logo_path[0] = '\0';
                overlay->config->subrom_path[0] = '\0';
                overlay->config->disk_rom_path[0] = '\0';
                overlay->dirty = true;
                notify_post("%s firmware unloaded",
                            msx_model_name(overlay->config->model));
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
                       overlay->row == 7 &&
                       overlay->config->sunrise_ide) {
                msx_eject_sunrise_disk(overlay->msx);
                overlay->config->ide_image_path[0] = '\0';
                overlay->dirty = true;
                notify_post("IDE disk ejected");
            } else if (overlay->section == OVERLAY_EXTENSIONS &&
                       overlay->row == 1 &&
                       overlay->config->sunrise_ide) {
                disconnect_sunrise(overlay);
                overlay->config->sunrise_rom_path[0] = '\0';
                overlay->dirty = true;
                apply_config(overlay);
                notify_post("Sunrise IDE ROM unloaded");
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

static const char *section_hint(OverlaySection section) {
    switch (section) {
        case OVERLAY_GENERAL:
            return "Machine, memory, audio, input, and optional controls.";
        case OVERLAY_MEDIA:
            return "Enter loads/selects; Delete ejects mounted media.";
        case OVERLAY_EXTENSIONS:
            return "Cartridge devices reserve slot 2, then slot 1.";
        case OVERLAY_ADVANCED:
            return "Machine models are saved to the per-user catalogue.";
        case OVERLAY_SECTION_COUNT:
            break;
    }
    return "";
}

void overlay_tick(Overlay *overlay) {
    OverlayDialogTarget target;
    int slot;

    if (overlay->dialog_failed) {
        SDL_MemoryBarrierAcquire();
        overlay->dialog_failed = false;
        overlay->dialog_target = OVERLAY_DIALOG_NONE;
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
    if (!overlay->visible || target == OVERLAY_DIALOG_NONE)
        return;
    if (!overlay->dialog_path[0]) {
        if (target == OVERLAY_DIALOG_BIOS ||
            target == OVERLAY_DIALOG_LOGO ||
            target == OVERLAY_DIALOG_SUBROM ||
            target == OVERLAY_DIALOG_DISK_ROM)
            notify_post("Machine firmware selection cancelled");
        else if (target == OVERLAY_DIALOG_MODEL_BIOS ||
                 target == OVERLAY_DIALOG_MODEL_LOGO ||
                 target == OVERLAY_DIALOG_MODEL_SUBROM ||
                 target == OVERLAY_DIALOG_MODEL_DISK_ROM)
            notify_post("Model firmware selection cancelled");
        else if (target == OVERLAY_DIALOG_SUNRISE_ROM)
            notify_post("Sunrise IDE ROM selection cancelled");
        else if (target == OVERLAY_DIALOG_IDE_IMAGE)
            notify_post("IDE disk selection cancelled");
        return;
    }

    if (target == OVERLAY_DIALOG_SUNRISE_ROM) {
        if (!connect_sunrise(overlay, overlay->dialog_path))
            return;
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        apply_config(overlay);
        return;
    }
    if (target == OVERLAY_DIALOG_IDE_IMAGE) {
        if (msx_mount_sunrise_disk(
                overlay->msx, overlay->dialog_path) != 0) {
            notify_post("Could not mount IDE image: %s",
                        path_basename(overlay->dialog_path));
            return;
        }
        snprintf(overlay->config->ide_image_path,
                 sizeof(overlay->config->ide_image_path), "%s",
                 overlay->dialog_path);
        copy_dirname(overlay->config->last_media_dir,
                     sizeof(overlay->config->last_media_dir),
                     overlay->dialog_path);
        overlay->dirty = true;
        notify_post("IDE disk mounted read-only: %s",
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

    if (target == OVERLAY_DIALOG_BIOS ||
        target == OVERLAY_DIALOG_LOGO ||
        target == OVERLAY_DIALOG_SUBROM ||
        target == OVERLAY_DIALOG_DISK_ROM) {
        char *destination =
            target == OVERLAY_DIALOG_BIOS
            ? overlay->pending_bios_path :
            target == OVERLAY_DIALOG_LOGO
            ? overlay->pending_logo_path :
            target == OVERLAY_DIALOG_SUBROM
            ? overlay->pending_subrom_path
            : overlay->pending_disk_rom_path;

        snprintf(destination, PATH_MAX, "%s", overlay->dialog_path);
        copy_dirname(overlay->pending_firmware_dir,
                     sizeof(overlay->pending_firmware_dir),
                     overlay->dialog_path);
        continue_firmware_selection(overlay);
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
                 "[choose when selected]");
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
                 "Blank ROM fields invoke a picker when the model is selected.",
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

void overlay_render(const Overlay *overlay) {
    SDL_Renderer *renderer;
    int rows;
    int panel_h;
    int tab_x = 20;

    if (!overlay->visible)
        return;
    renderer = overlay->display->renderer;
    rows = section_rows(overlay, overlay->section);
    panel_h = OVERLAY_FIRST_Y + rows * OVERLAY_LINE_H + 54;

    ui_fill_rect(renderer, 0.0f, 0.0f,
                 (float)DISPLAY_LOGICAL_W, (float)DISPLAY_LOGICAL_H,
                 0, 0, 0, 110);
    ui_fill_rect(renderer, 8.0f, 8.0f, 624.0f, (float)panel_h,
                 8, 10, 24, 235);
    ui_draw_rect(renderer, 8.0f, 8.0f, 624.0f, (float)panel_h,
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
                 "Left/Right tabs  Up/Down row  Enter select  Delete unload",
                 150, 160, 190);
    ui_draw_text(renderer, 20.0f,
                 (float)(OVERLAY_FIRST_Y + rows * OVERLAY_LINE_H + 25),
                 section_hint(overlay->section), 120, 180, 150);

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
        const char *hint = "Up/Down choose   Enter continue   Esc cancel";
        const int visible_rows = 10;
        int first_model = overlay->machine_row - visible_rows / 2;
        int last_model;
        float box_w = 500.0f;
        float box_h;
        float box_x = ((float)DISPLAY_LOGICAL_W - box_w) * 0.5f;
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
        box_y = ((float)DISPLAY_LOGICAL_H - box_h) * 0.5f;

        ui_fill_rect(renderer, 0.0f, 0.0f,
                     (float)DISPLAY_LOGICAL_W, (float)DISPLAY_LOGICAL_H,
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
            const MsxProfile *profile =
                msx_profile(definition->hardware);
            char line[160];
            bool selected = model == overlay->machine_row;
            bool has_subrom =
                profile->requires_subrom ||
                definition->subrom_path[0];
            bool has_disk_rom =
                profile->requires_disk_rom ||
                definition->disk_rom_path[0];

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
        float box_x = ((float)DISPLAY_LOGICAL_W - box_w) * 0.5f;
        float box_y = ((float)DISPLAY_LOGICAL_H - box_h) * 0.5f;
        ui_fill_rect(renderer, 0.0f, 0.0f,
                     (float)DISPLAY_LOGICAL_W, (float)DISPLAY_LOGICAL_H,
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
