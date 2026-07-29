#include "overlay.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void send_key(Overlay *overlay, SDL_Keycode key) {
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    assert(overlay_handle_event(overlay, &event));
}

static void send_key_mod(Overlay *overlay, SDL_Keycode key,
                         SDL_Keymod modifiers) {
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.mod = modifiers;
    assert(overlay_handle_event(overlay, &event));
}

static void send_text(Overlay *overlay, const char *text) {
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    assert(overlay_handle_event(overlay, &event));
}

static void render_overlay(Display *display, MsxMachine *msx,
                           Overlay *overlay) {
    display_draw(display, msx);
    overlay_render(overlay);
    display_present(display, msx);
}

int main(void) {
    const char *editor_path = "tests/test-model-editor.tmp";
    Config config;
    ModelCatalog models;
    MsxMachine msx;
    Display display;
    Overlay overlay;
    DisplayLayout layout;
    u8 cartridge[0x4000];

    display_calculate_layout(640, 520, &layout);
    assert(layout.screen_x == 0);
    assert(layout.screen_y == 0);
    assert(layout.screen_w == 640);
    assert(layout.screen_h == 480);
    assert(layout.footer_y == 480);

    display_calculate_layout(1000, 1000, &layout);
    assert(layout.screen_x == 0);
    assert(layout.screen_y == 105);
    assert(layout.screen_w == 1000);
    assert(layout.screen_h == 750);
    assert(layout.footer_y == 960);

    display_calculate_layout(1600, 900, &layout);
    assert(layout.screen_x == 227);
    assert(layout.screen_y == 0);
    assert(layout.screen_w == 1146);
    assert(layout.screen_h == 860);
    assert(layout.footer_y == 860);

    config_defaults(&config);
    config.tinker = true;
    model_catalog_defaults(&models);
    snprintf(models.entries[models.count].id,
             sizeof(models.entries[models.count].id), "custom-msx2");
    snprintf(models.entries[models.count].name,
             sizeof(models.entries[models.count].name), "Custom MSX2");
    models.entries[models.count].hardware = MSX_MODEL_GENERIC_MSX2;
    ++models.count;
    snprintf(models.edit_path, sizeof(models.edit_path),
             "%s", editor_path);
    msx_init(&msx, config.model, config.region, config.memory_kb);
    memset(cartridge, 0xff, sizeof(cartridge));
    cartridge[0] = 'A';
    cartridge[1] = 'B';
    assert(msx_install_cartridge_slot(
               &msx, 1, cartridge, sizeof(cartridge),
               MSX_CART_MAPPER_LINEAR) == 0);
    snprintf(config.cartridge_path[1],
             sizeof(config.cartridge_path[1]),
             "test-cartridge-2.rom");
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen",
                            SDL_HINT_OVERRIDE);
    assert(display_init(&display, &config, &msx, "Test MSX") == 0);
    overlay_init(&overlay, &config, &models, &display, &msx);

    send_key(&overlay, SDLK_F9);
    assert(overlay.visible);
    assert(overlay.section == OVERLAY_GENERAL);
    assert(overlay.row == 0);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MACHINE);
    assert(overlay.machine_row == 0);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.machine_row == 1);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.machine_row == 2);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.machine_row == 3);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.machine_row == 0);
    send_key(&overlay, SDLK_UP);
    assert(overlay.machine_row == 3);
    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.model == MSX_MODEL_GENERIC_MSX1);
    assert(!overlay.dirty);

    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 2);
    send_key(&overlay, SDLK_RETURN);
    assert(config.memory_kb == 128);
    assert(msx.ram_kb == 128);
    for (int step = 0; step < 5; ++step)
        send_key(&overlay, SDLK_RETURN);
    assert(config.memory_kb == 4096);
    assert(msx.ram_kb == 4096);
    assert(msx.ram_capacity == MSX_RAM_MAX_SIZE);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 4);
    send_key(&overlay, SDLK_RETURN);
    assert(config.audio_volume == 90);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 5);
    send_key(&overlay, SDLK_RETURN);
    assert(config.extra_hardware);

    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_MEDIA);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    assert(overlay.row == 0);
    render_overlay(&display, &msx, &overlay);

    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(config.sunrise_ide);
    assert(strcmp(config_cartridge_slot_owner(&config, 1),
                  "Sunrise IDE") == 0);
    assert(!msx_get_cartridge(&msx, 1)->loaded);
    assert(!config.cartridge_path[1][0]);

    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(config.scc);
    assert(strcmp(config_cartridge_slot_owner(&config, 0),
                  "Konami SCC") == 0);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.msx_music);
    assert(config_cartridge_extension_count(&config) == 2);

    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_MEDIA);
    assert(overlay.row == 0);
    render_overlay(&display, &msx, &overlay);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.dialog_target == OVERLAY_DIALOG_NONE);
    for (int row = 0; row < 7; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 7);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 0);

    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.sunrise_ide);
    assert(config_cartridge_slot_available(&config, 0));
    assert(strcmp(config_cartridge_slot_owner(&config, 1),
                  "Konami SCC") == 0);

    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_MEDIA);
    for (int row = 0; row < 6; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 6);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 0);
    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_GENERAL);
    for (int row = 0; row < 5; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 5);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.extra_hardware);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_MEDIA);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_ADVANCED);
    assert(overlay.row == 0);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(overlay.model_editor_row == 0);
    render_overlay(&display, &msx, &overlay);

    send_key(&overlay, SDLK_DELETE);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(strstr(overlay.model_editor_error,
                  "another machine"));

    send_key(&overlay, SDLK_INSERT);
    assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
    assert(overlay.model_edit_index == -1);
    assert(strcmp(overlay.model_edit.id, "new-model") == 0);
    render_overlay(&display, &msx, &overlay);
    send_key(&overlay, SDLK_F2);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(models.count == 5);
    assert(model_catalog_find(&models, "new-model"));

    send_key(&overlay, SDLK_D);
    assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
    assert(overlay.model_edit_index == -1);
    assert(strcmp(overlay.model_edit.id,
                  "new-model-copy") == 0);
    send_key(&overlay, SDLK_F2);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(models.count == 6);

    send_key(&overlay, SDLK_DELETE);
    assert(overlay.state == OVERLAY_STATE_MODEL_DELETE);
    render_overlay(&display, &msx, &overlay);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(models.count == 5);
    assert(!model_catalog_find(&models, "new-model-copy"));

    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.model_edit_field == 1);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MODEL_TEXT);
    render_overlay(&display, &msx, &overlay);
    send_key_mod(&overlay, SDLK_A, SDL_KMOD_CTRL);
    send_text(&overlay, "Test model");
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
    assert(strcmp(overlay.model_edit.name, "Test model") == 0);
    send_key(&overlay, SDLK_F2);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(strcmp(model_catalog_find(
                      &models, "new-model")->name,
                  "Test model") == 0);

    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(remove(editor_path) == 0);

    display_quit(&display);
    msx_destroy(&msx);
    puts("overlay machine chooser and model editor tests passed");
    return 0;
}
