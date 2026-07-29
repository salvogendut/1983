#include "overlay.h"

#include <stdio.h>
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

static const char *section_name(OverlaySection section) {
    switch (section) {
        case OVERLAY_GENERAL:    return "General";
        case OVERLAY_MEDIA:      return "Media";
        case OVERLAY_AUDIO:      return "Audio";
        case OVERLAY_EXTENSIONS: return "Extensions";
        case OVERLAY_ADVANCED:   return "Advanced";
        case OVERLAY_SECTION_COUNT: break;
    }
    return "";
}

static bool section_available(const Overlay *overlay,
                              OverlaySection section) {
    return section != OVERLAY_ADVANCED || overlay->config->tinker;
}

static int section_rows(OverlaySection section) {
    switch (section) {
        case OVERLAY_GENERAL:    return 5;
        case OVERLAY_MEDIA:      return 10;
        case OVERLAY_AUDIO:      return 1;
        case OVERLAY_EXTENSIONS: return 5;
        case OVERLAY_ADVANCED:   return 6;
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
    const char *path = overlay->config->cartridge_path[slot];
    const MsxCartridge *cartridge =
        msx_get_cartridge(overlay->msx, slot);

    if (cartridge && cartridge->loaded)
        snprintf(value, value_size, "%s", path_basename(path));
    else if (path[0])
        snprintf(value, value_size, "%s [not loaded]",
                 path_basename(path));
    else
        snprintf(value, value_size, "[not mounted]");
}

static void mapper_text(const Overlay *overlay, unsigned slot,
                        char *value, size_t value_size) {
    MsxCartridgeMapper requested =
        overlay->config->cartridge_mapper[slot];
    const MsxCartridge *cartridge =
        msx_get_cartridge(overlay->msx, slot);

    if (requested == MSX_CART_MAPPER_AUTO &&
        cartridge && cartridge->loaded)
        snprintf(value, value_size, "Auto (%s)",
                 msx_cartridge_mapper_display_name(cartridge->mapper));
    else
        snprintf(value, value_size, "%s",
                 msx_cartridge_mapper_display_name(requested));
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
                case 0:
                    snprintf(label, label_size, "Machine");
                    snprintf(value, value_size, "%s",
                             msx_model_name(config->model));
                    break;
                case 1:
                    snprintf(label, label_size, "Video standard");
                    snprintf(value, value_size, "%s",
                             msx_region_name(config->region));
                    break;
                case 2:
                    snprintf(label, label_size, "RAM");
                    snprintf(value, value_size, "%d KB", config->memory_kb);
                    break;
                case 3:
                    snprintf(label, label_size, "VRAM");
                    snprintf(value, value_size, "%d KB (%s)",
                             msx->profile->vram_kb, msx_vdp_name(msx));
                    break;
                case 4:
                    snprintf(label, label_size, "Tinker");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->tinker));
                    break;
            }
            break;
        case OVERLAY_MEDIA:
            switch (row) {
                case 0:
                    snprintf(label, label_size, "Firmware set");
                    snprintf(value, value_size,
                             "[command-line setup]");
                    break;
                case 1:
                    snprintf(label, label_size, "Cartridge 1");
                    cartridge_text(overlay, 0, value, value_size);
                    break;
                case 2:
                    snprintf(label, label_size, "Cart 1 mapper");
                    mapper_text(overlay, 0, value, value_size);
                    break;
                case 3:
                    snprintf(label, label_size, "Cartridge 2");
                    cartridge_text(overlay, 1, value, value_size);
                    break;
                case 4:
                    snprintf(label, label_size, "Cart 2 mapper");
                    mapper_text(overlay, 1, value, value_size);
                    break;
                case 5:
                    snprintf(label, label_size, "Cassette");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
                case 6:
                    snprintf(label, label_size, "Drive A");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
                case 7:
                    snprintf(label, label_size, "Drive B");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
                case 8:
                    snprintf(label, label_size, "Nextor kernel");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
                case 9:
                    snprintf(label, label_size, "IDE hard disk");
                    snprintf(value, value_size,
                             "[not mounted - loader planned]");
                    break;
            }
            break;
        case OVERLAY_AUDIO:
            snprintf(label, label_size, "PSG volume");
            snprintf(value, value_size, "%d%%", config->audio_volume);
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
                    snprintf(value, value_size, "%s",
                             stub_toggle_name(config->sunrise_ide));
                    break;
                case 2:
                    snprintf(label, label_size, "Konami SCC");
                    snprintf(value, value_size, "%s",
                             stub_toggle_name(config->scc));
                    break;
                case 3:
                    snprintf(label, label_size, "MSX-MUSIC");
                    snprintf(value, value_size, "%s",
                             stub_toggle_name(config->msx_music));
                    break;
                case 4:
                    snprintf(label, label_size, "Kanji ROM");
                    snprintf(value, value_size, "%s",
                             stub_toggle_name(config->kanji_rom));
                    break;
            }
            break;
        case OVERLAY_ADVANCED:
            switch (row) {
                case 0:
                    snprintf(label, label_size, "Smoothing");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->smoothing));
                    break;
                case 1:
                    snprintf(label, label_size, "Real CRT");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->real_crt));
                    break;
                case 2:
                    snprintf(label, label_size, "CRT scanlines");
                    snprintf(value, value_size,
                             config->real_crt ? "%d%%" : "%d%% (inactive)",
                             config->crt_scanlines);
                    break;
                case 3:
                    snprintf(label, label_size, "Notifications");
                    snprintf(value, value_size, "%s",
                             notification_name(config->notifications));
                    break;
                case 4:
                    snprintf(label, label_size, "Debug overlay");
                    snprintf(value, value_size, "%s",
                             toggle_name(config->debug));
                    break;
                case 5:
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
        display_set_title(overlay->display, msx);
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
        bool changed =
            strcmp(current, saved) != 0 ||
            overlay->config->cartridge_mapper[slot] !=
                overlay->saved.cartridge_mapper[slot];

        if (!changed)
            continue;
        if (!saved[0]) {
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

static void close_overlay(Overlay *overlay, bool save) {
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

static void cartridge_dialog_callback(void *userdata,
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

    if (overlay->dialog_slot >= 0)
        return;
    overlay->dialog_slot = (int)slot;
    overlay->dialog_ready = false;
    overlay->dialog_failed = false;
    overlay->dialog_error[0] = '\0';
    SDL_ShowOpenFileDialog(cartridge_dialog_callback, overlay,
                           overlay->display
                           ? overlay->display->window : NULL,
                           filters, 2, location, false);
}

static void change_cartridge_mapper(Overlay *overlay, unsigned slot) {
    MsxCartridgeMapper previous =
        overlay->config->cartridge_mapper[slot];
    MsxCartridgeMapper next =
        (MsxCartridgeMapper)(previous + 1);
    const MsxCartridge *cartridge;

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

static void activate_item(Overlay *overlay) {
    Config *config = overlay->config;

    switch (overlay->section) {
        case OVERLAY_GENERAL:
            switch (overlay->row) {
                case 0:
                    config->model =
                        config->model == MSX_MODEL_GENERIC_MSX1
                        ? MSX_MODEL_GENERIC_MSX2
                        : MSX_MODEL_GENERIC_MSX1;
                    config->memory_kb = msx_default_ram_kb(config->model);
                    break;
                case 1:
                    config->region =
                        config->region == MSX_REGION_PAL
                        ? MSX_REGION_NTSC : MSX_REGION_PAL;
                    break;
                case 2:
                    config->memory_kb =
                        msx_next_ram_kb(config->model,
                                        config->memory_kb, 1);
                    break;
                case 3:
                    notify_post("VRAM follows the selected MSX generation");
                    return;
                case 4:
                    config->tinker = !config->tinker;
                    break;
            }
            break;
        case OVERLAY_MEDIA: {
            static const char *media[] = {
                "Firmware set", "", "", "", "", "Cassette",
                "Drive A", "Drive B", "Nextor kernel", "IDE hard disk"
            };
            if (overlay->row == 1) {
                open_cartridge_dialog(overlay, 0);
                return;
            }
            if (overlay->row == 2) {
                change_cartridge_mapper(overlay, 0);
                return;
            }
            if (overlay->row == 3) {
                open_cartridge_dialog(overlay, 1);
                return;
            }
            if (overlay->row == 4) {
                change_cartridge_mapper(overlay, 1);
                return;
            }
            notify_post("%s loading is not implemented yet",
                        media[overlay->row]);
            return;
        }
        case OVERLAY_AUDIO:
            config->audio_volume += 10;
            if (config->audio_volume > 100)
                config->audio_volume = 0;
            break;
        case OVERLAY_EXTENSIONS:
            switch (overlay->row) {
                case 0: config->second_drive = !config->second_drive; break;
                case 1: config->sunrise_ide = !config->sunrise_ide; break;
                case 2: config->scc = !config->scc; break;
                case 3: config->msx_music = !config->msx_music; break;
                case 4: config->kanji_rom = !config->kanji_rom; break;
            }
            break;
        case OVERLAY_ADVANCED:
            switch (overlay->row) {
                case 0: config->smoothing = !config->smoothing; break;
                case 1: config->real_crt = !config->real_crt; break;
                case 2:
                    config->crt_scanlines += 5;
                    if (config->crt_scanlines > 95)
                        config->crt_scanlines = 0;
                    break;
                case 3: change_notification_mode(config); break;
                case 4: config->debug = !config->debug; break;
                case 5: return;
            }
            break;
        case OVERLAY_SECTION_COUNT:
            return;
    }
    overlay->dirty = true;
    apply_config(overlay);
}

void overlay_init(Overlay *overlay, Config *config, Display *display,
                  MsxMachine *msx) {
    memset(overlay, 0, sizeof(*overlay));
    overlay->dialog_slot = -1;
    overlay->config = config;
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
                overlay->row = section_rows(overlay->section) - 1;
            break;
        case SDLK_DOWN:
            ++overlay->row;
            if (overlay->row >= section_rows(overlay->section))
                overlay->row = 0;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            activate_item(overlay);
            break;
        case SDLK_DELETE:
            if (overlay->section == OVERLAY_MEDIA &&
                (overlay->row == 1 || overlay->row == 3)) {
                unsigned slot = overlay->row == 1 ? 0 : 1;
                msx_eject_cartridge(overlay->msx, slot);
                overlay->config->cartridge_path[slot][0] = '\0';
                overlay->dirty = true;
                notify_post("Cartridge %u ejected", slot + 1);
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
            return "Machine changes reset the current scaffold state.";
        case OVERLAY_MEDIA:
            return "Enter loads/selects; Delete ejects a cartridge.";
        case OVERLAY_AUDIO:
            return "Volume zero mutes the built-in PSG.";
        case OVERLAY_EXTENSIONS:
            return "Extension switches are UI/config stubs, not emulated devices.";
        case OVERLAY_ADVANCED:
            return "Advanced is visible while General > Tinker is enabled.";
        case OVERLAY_SECTION_COUNT:
            break;
    }
    return "";
}

void overlay_tick(Overlay *overlay) {
    int slot;

    if (overlay->dialog_failed) {
        SDL_MemoryBarrierAcquire();
        overlay->dialog_failed = false;
        overlay->dialog_slot = -1;
        notify_post("File picker unavailable: %s",
                    overlay->dialog_error[0]
                    ? overlay->dialog_error : "unknown SDL error");
        return;
    }
    if (!overlay->dialog_ready)
        return;
    SDL_MemoryBarrierAcquire();
    overlay->dialog_ready = false;
    slot = overlay->dialog_slot;
    overlay->dialog_slot = -1;
    if (!overlay->dialog_path[0] || !overlay->visible || slot < 0 ||
        slot >= (int)MSX_CARTRIDGE_SLOTS)
        return;
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
    overlay->dirty = true;
    notify_post("Cartridge %d mounted: %s (%s)", slot + 1,
                path_basename(overlay->dialog_path),
                msx_cartridge_mapper_display_name(
                    msx_get_cartridge(overlay->msx,
                                      (unsigned)slot)->mapper));
}

void overlay_render(const Overlay *overlay) {
    SDL_Renderer *renderer;
    int rows;
    int panel_h;
    int tab_x = 20;

    if (!overlay->visible)
        return;
    renderer = overlay->display->renderer;
    rows = section_rows(overlay->section);
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
                 "Left/Right tabs  Up/Down row  Enter change  F9 save  Esc close",
                 150, 160, 190);
    ui_draw_text(renderer, 20.0f,
                 (float)(OVERLAY_FIRST_Y + rows * OVERLAY_LINE_H + 25),
                 section_hint(overlay->section), 120, 180, 150);

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
