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

int main(void) {
    Config config;
    ModelCatalog models;
    MsxMachine msx;
    Display display;
    Overlay overlay;

    config_defaults(&config);
    model_catalog_defaults(&models);
    snprintf(models.entries[models.count].id,
             sizeof(models.entries[models.count].id), "custom-msx2");
    snprintf(models.entries[models.count].name,
             sizeof(models.entries[models.count].name), "Custom MSX2");
    models.entries[models.count].hardware = MSX_MODEL_GENERIC_MSX2;
    ++models.count;
    msx_init(&msx, config.model, config.region, config.memory_kb);
    memset(&display, 0, sizeof(display));
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

    msx_destroy(&msx);
    puts("overlay machine chooser tests passed");
    return 0;
}
