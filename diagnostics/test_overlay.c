#include "overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define TEST_RMDIR _rmdir
#else
#include <unistd.h>
#define TEST_RMDIR rmdir
#endif

#include "leds.h"

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

static bool send_quit(Overlay *overlay) {
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_QUIT;
    return overlay_handle_event(overlay, &event);
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
    display_present_begin(display);
    overlay_render(overlay);
    display_present_end(display, msx);
}

static void assert_pixel(SDL_Surface *surface, int x, int y,
                         Uint8 expected_red, Uint8 expected_green,
                         Uint8 expected_blue) {
    Uint8 red;
    Uint8 green;
    Uint8 blue;

    assert(SDL_ReadSurfacePixel(
        surface, x, y, &red, &green, &blue, NULL));
    assert(red == expected_red);
    assert(green == expected_green);
    assert(blue == expected_blue);
}

static void test_fixed_overlay_scale(Display *display, MsxMachine *msx,
                                     Overlay *overlay) {
    DisplayLayout layout;
    SDL_Surface *pixels;
    int output_w;
    int output_h;

    display_draw(display, msx);
    display_present_begin(display);
    overlay_render(overlay);
    assert(SDL_GetRenderOutputSize(
        display->renderer, &output_w, &output_h));
    display_calculate_layout(output_w, output_h, &layout);
    assert(layout.screen_w >= 960);
    assert(layout.screen_h >= 720);
    pixels = SDL_RenderReadPixels(display->renderer, NULL);
    assert(pixels);

    /*
     * The panel's eight-unit inset lands at 12 output pixels because the
     * presentation overlay uses 1984's fixed 1.5x scale. Rendering it into
     * the 2x guest canvas would instead place this border at 16 pixels.
     */
    assert_pixel(pixels,
                 layout.screen_x + 12,
                 layout.screen_y + 12,
                 70, 90, 180);
    SDL_DestroySurface(pixels);
    display_present_end(display, msx);
}

static void test_cartridge_led_rendering(SDL_Renderer *renderer) {
    SDL_Surface *pixels;

    assert(LED_CARTRIDGE_I == LED_POWER + 1);
    assert(LED_CARTRIDGE_II == LED_CARTRIDGE_I + 1);
    assert(LED_CAPS == LED_CARTRIDGE_II + 1);

    leds_init();
    leds_set_cartridge(0, LED_CARTRIDGE_STANDARD, true);
    leds_render(renderer, 0, 0, 64, 24);
    pixels = SDL_RenderReadPixels(renderer, NULL);
    assert(pixels);
    assert_pixel(pixels, 26, 12, 255, 160, 30);
    assert_pixel(pixels, 38, 12, 255, 160, 30);
    SDL_DestroySurface(pixels);

    leds_set_cartridge_activity(0, true);
    assert(!leds_get_cartridge_state(0).activity);
    leds_render(renderer, 0, 0, 64, 24);
    pixels = SDL_RenderReadPixels(renderer, NULL);
    assert(pixels);
    assert_pixel(pixels, 26, 12, 255, 160, 30);
    assert_pixel(pixels, 38, 12, 255, 160, 30);
    SDL_DestroySurface(pixels);

    leds_set_cartridge(0, LED_CARTRIDGE_NETWORK, true);
    leds_render(renderer, 0, 0, 64, 24);
    pixels = SDL_RenderReadPixels(renderer, NULL);
    assert(pixels);
    assert_pixel(pixels, 26, 12, 255, 160, 30);
    assert_pixel(pixels, 38, 12, 48, 48, 48);
    SDL_DestroySurface(pixels);

    leds_set_cartridge_activity(0, true);
    leds_render(renderer, 0, 0, 64, 24);
    pixels = SDL_RenderReadPixels(renderer, NULL);
    assert(pixels);
    assert_pixel(pixels, 26, 12, 255, 160, 30);
    assert_pixel(pixels, 38, 12, 245, 245, 245);
    SDL_DestroySurface(pixels);
}

static void fill_vdp_pixels(MsxVdp *vdp, u32 colour) {
    for (unsigned y = 0; y < vdp->render_height; ++y)
        for (unsigned x = 0; x < vdp->render_width; ++x)
            vdp->pixels[y * vdp->render_width + x] = colour;
}

static void test_vdp_presentation_geometry(void) {
    static const u32 active_colour = 0x123456;
    static const u32 adjusted_colour = 0xabcdef;
    MsxVdp vdp;
    u32 *pixels = malloc(
        (size_t)DISPLAY_FB_W * DISPLAY_FB_H * sizeof(*pixels));
    u32 border;

    assert(pixels);
    vdp_init(&vdp);
    vdp.registers[7] = 4;
    fill_vdp_pixels(&vdp, active_colour);
    border = vdp_border_colour(&vdp, 0);
    assert(border != active_colour);
    display_compose_vdp(pixels, &vdp);

    /* A 256x192 source is centred in the 280x240 visible aperture. */
    assert(pixels[47 * DISPLAY_FB_W + 320] == border);
    assert(pixels[48 * DISPLAY_FB_W + 320] == active_colour);
    assert(pixels[431 * DISPLAY_FB_W + 320] == active_colour);
    assert(pixels[432 * DISPLAY_FB_W + 320] == border);
    assert(pixels[240 * DISPLAY_FB_W + 26] == border);
    assert(pixels[240 * DISPLAY_FB_W + 27] == active_colour);
    assert(pixels[240 * DISPLAY_FB_W + 612] == active_colour);
    assert(pixels[240 * DISPLAY_FB_W + 613] == border);

    vdp_set_type(&vdp, MSX_VDP_V9938);
    vdp_reset(&vdp);
    vdp.render_width = MSX2_VIDEO_W;
    vdp.render_height = MSX2_VIDEO_H;
    fill_vdp_pixels(&vdp, adjusted_colour);
    display_compose_vdp(pixels, &vdp);

    /* Neutral R#18 centres 212 lines with 14 border lines per side. */
    assert(pixels[27 * DISPLAY_FB_W + 320] ==
           vdp_border_colour(&vdp, 0));
    assert(pixels[28 * DISPLAY_FB_W + 320] == adjusted_colour);
    assert(pixels[451 * DISPLAY_FB_W + 320] == adjusted_colour);
    assert(pixels[452 * DISPLAY_FB_W + 320] ==
           vdp_border_colour(&vdp, 0));

    /*
     * Encoded R#18 value 7 selects adjustment zero: seven lines up and
     * fourteen high-resolution dots left from the neutral position.
     */
    vdp.registers[18] = 0x77;
    display_compose_vdp(pixels, &vdp);
    assert(pixels[13 * DISPLAY_FB_W + 320] ==
           vdp_border_colour(&vdp, 0));
    assert(pixels[14 * DISPLAY_FB_W + 320] == adjusted_colour);
    assert(pixels[437 * DISPLAY_FB_W + 320] == adjusted_colour);
    assert(pixels[438 * DISPLAY_FB_W + 320] ==
           vdp_border_colour(&vdp, 0));
    assert(pixels[240 * DISPLAY_FB_W + 10] ==
           vdp_border_colour(&vdp, 0));
    assert(pixels[240 * DISPLAY_FB_W + 11] == adjusted_colour);
    assert(pixels[240 * DISPLAY_FB_W + 596] == adjusted_colour);
    assert(pixels[240 * DISPLAY_FB_W + 597] ==
           vdp_border_colour(&vdp, 0));

    /* SCREEN 6 has a striped border; SCREEN 8 uses direct GRB colour. */
    vdp.registers[0] = 0x08;
    vdp.registers[7] = 0x0e;
    assert(vdp_border_colour(&vdp, 0) !=
           vdp_border_colour(&vdp, 1));
    vdp.registers[0] = 0x0e;
    vdp.registers[7] = 0xe3;
    assert(vdp_border_colour(&vdp, 0) == 0x00ffff);
    assert(vdp_border_colour(&vdp, 1) == 0x00ffff);

    free(pixels);
}

int main(void) {
    static const u8 cassette_image[] = {
        0x1f, 0xa6, 0xde, 0xba, 0xcc, 0x13, 0x7d, 0x74,
        0xea, 0xea, 0xea, 0xea, 0xea,
        0xea, 0xea, 0xea, 0xea, 0xea, 0x1a
    };
    const char *editor_path = "diagnostics/test-model-editor.tmp";
    const char *machine_bios_path = "diagnostics/test-machine-bios.tmp";
    const char *machine_subrom_path = "diagnostics/test-machine-subrom.tmp";
    const char *machine_disk_rom_path =
        "diagnostics/test-machine-disk-rom.tmp";
    const char *sunrise_rom_path = "diagnostics/test-sunrise-rom.tmp";
    const char *sunrise_rom_path_2 = "diagnostics/test-sunrise-rom-2.tmp";
    const char *ide_image_path = "diagnostics/test-sunrise-disk.tmp";
    const char *sd_mapper_rom_path = "diagnostics/test-sd-mapper-rom.tmp";
    const char *sd_image_path = "diagnostics/test-sd-card.tmp";
    const char *megaflash_rom_path = "diagnostics/test-megaflash-rom.tmp";
    const char *megaflash_rom_path_2 = "diagnostics/test-megaflash-rom-2.tmp";
    const char *megaflash_config_path =
        "diagnostics/test-overlay-state/config.ini";
    const char *cassette_path = "diagnostics/test-cassette.tmp";
    const char *rs232_rom_path = "diagnostics/test-rs232-rom.tmp";
    const char *cdx2_rom_path = "diagnostics/test-cdx2-rom.tmp";
    const char *rdf600_rom_path = "diagnostics/test-rdf600-rom.tmp";
    Config config;
    ModelCatalog models;
    MsxMachine msx;
    Display display;
    Overlay overlay;
    DisplayLayout layout;
    u8 cartridge[0x4000];
    u8 sunrise_rom[MSX_SUNRISE_ROM_SIZE];
    FILE *fixture;

    test_vdp_presentation_geometry();
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
    config.scale = 2;
    config.tinker = true;
    memset(sunrise_rom, 0xff, sizeof(sunrise_rom));
    fixture = fopen(sunrise_rom_path, "wb");
    assert(fixture);
    assert(fwrite(sunrise_rom, 1, sizeof(sunrise_rom), fixture) ==
           sizeof(sunrise_rom));
    assert(fclose(fixture) == 0);
    fixture = fopen(sunrise_rom_path_2, "wb");
    assert(fixture);
    assert(fwrite(sunrise_rom, 1, sizeof(sunrise_rom), fixture) ==
           sizeof(sunrise_rom));
    assert(fclose(fixture) == 0);
    fixture = fopen(machine_bios_path, "wb");
    assert(fixture);
    assert(fwrite(sunrise_rom, 1, MSX_BIOS_SIZE, fixture) ==
           MSX_BIOS_SIZE);
    assert(fclose(fixture) == 0);
    fixture = fopen(machine_subrom_path, "wb");
    assert(fixture);
    assert(fwrite(sunrise_rom, 1, MSX_SUBROM_SIZE, fixture) ==
           MSX_SUBROM_SIZE);
    assert(fclose(fixture) == 0);
    fixture = fopen(machine_disk_rom_path, "wb");
    assert(fixture);
    assert(fwrite(sunrise_rom, 1, MSX_DISK_ROM_SIZE, fixture) ==
           MSX_DISK_ROM_SIZE);
    assert(fclose(fixture) == 0);
    fixture = fopen(ide_image_path, "wb");
    assert(fixture);
    memset(sunrise_rom, 0x83, ATA_SECTOR_SIZE);
    assert(fwrite(sunrise_rom, 1, ATA_SECTOR_SIZE, fixture) ==
           ATA_SECTOR_SIZE);
    assert(fclose(fixture) == 0);
    fixture = fopen(sd_mapper_rom_path, "wb");
    assert(fixture);
    assert(fwrite(sunrise_rom, 1, sizeof(sunrise_rom), fixture) ==
           sizeof(sunrise_rom));
    assert(fclose(fixture) == 0);
    fixture = fopen(sd_image_path, "wb");
    assert(fixture);
    memset(sunrise_rom, 0x5a, SD_CARD_SECTOR_SIZE);
    assert(fwrite(sunrise_rom, 1, SD_CARD_SECTOR_SIZE, fixture) ==
           SD_CARD_SECTOR_SIZE);
    assert(fclose(fixture) == 0);
    fixture = fopen(megaflash_rom_path, "wb");
    assert(fixture);
    memset(sunrise_rom, 0xff, sizeof(sunrise_rom));
    for (size_t chunk = 0;
         chunk < MSX_MEGAFLASH_FLASH_SIZE / sizeof(sunrise_rom);
         ++chunk)
        assert(fwrite(
                   sunrise_rom, 1, sizeof(sunrise_rom), fixture) ==
                sizeof(sunrise_rom));
    assert(fclose(fixture) == 0);
    fixture = fopen(megaflash_rom_path_2, "wb");
    assert(fixture);
    memset(sunrise_rom, 0x44, sizeof(sunrise_rom));
    for (size_t chunk = 0;
         chunk < MSX_MEGAFLASH_FLASH_SIZE / sizeof(sunrise_rom);
         ++chunk)
        assert(fwrite(
                   sunrise_rom, 1, sizeof(sunrise_rom), fixture) ==
               sizeof(sunrise_rom));
    assert(fclose(fixture) == 0);
    fixture = fopen(cassette_path, "wb");
    assert(fixture);
    assert(fwrite(cassette_image, 1, sizeof(cassette_image), fixture) ==
           sizeof(cassette_image));
    assert(fclose(fixture) == 0);
    {
        /* A 16 KB stand-in RS-232C cartridge ROM (gitignored in ROMS/). */
        u8 rs232_rom[0x4000];

        memset(rs232_rom, 0x00, sizeof(rs232_rom));
        rs232_rom[0] = 'A';
        rs232_rom[1] = 'B';
        fixture = fopen(rs232_rom_path, "wb");
        assert(fixture);
        assert(fwrite(rs232_rom, 1, sizeof(rs232_rom), fixture) ==
               sizeof(rs232_rom));
        assert(fclose(fixture) == 0);
    }
    {
        u8 cdx2_rom[MSX_CDX2_COMBINED_ROM_SIZE] = { 0 };

        cdx2_rom[0] = 'A';
        cdx2_rom[1] = 'B';
        cdx2_rom[4] = 0x11;
        cdx2_rom[MSX_CDX2_ROM_SIZE] = 'A';
        cdx2_rom[MSX_CDX2_ROM_SIZE + 1] = 'B';
        cdx2_rom[MSX_CDX2_ROM_SIZE + 4] = 0x22;
        fixture = fopen(cdx2_rom_path, "wb");
        assert(fixture);
        assert(fwrite(cdx2_rom, 1, sizeof(cdx2_rom), fixture) ==
               sizeof(cdx2_rom));
        assert(fclose(fixture) == 0);
    }
    {
        u8 rdf600_rom[MSX_RDF600_ROM_SIZE] = { 0 };

        rdf600_rom[0] = 'A';
        rdf600_rom[1] = 'B';
        fixture = fopen(rdf600_rom_path, "wb");
        assert(fixture);
        assert(fwrite(rdf600_rom, 1, sizeof(rdf600_rom), fixture) ==
               sizeof(rdf600_rom));
        assert(fclose(fixture) == 0);
    }
    model_catalog_defaults(&models);
    {
        size_t cbios_index = model_catalog_index(&models, "cbios");
        size_t nms8250_index = model_catalog_index(&models, "nms8250");

        assert(cbios_index < models.count);
        assert(nms8250_index < models.count);
        snprintf(models.entries[cbios_index].bios_path,
                 sizeof(models.entries[cbios_index].bios_path), "%s",
                 machine_bios_path);
        models.entries[cbios_index].logo_path[0] = '\0';
        snprintf(models.entries[nms8250_index].bios_path,
                 sizeof(models.entries[nms8250_index].bios_path), "%s",
                 machine_bios_path);
        snprintf(models.entries[nms8250_index].subrom_path,
                 sizeof(models.entries[nms8250_index].subrom_path), "%s",
                 machine_subrom_path);
        snprintf(models.entries[nms8250_index].disk_rom_path,
                 sizeof(models.entries[nms8250_index].disk_rom_path),
                 "diagnostics/missing-machine-disk-rom.rom");
    }
    snprintf(models.entries[models.count].id,
             sizeof(models.entries[models.count].id),
             "custom-floppy-msx2");
    snprintf(models.entries[models.count].name,
             sizeof(models.entries[models.count].name),
             "Custom floppy MSX2");
    models.entries[models.count].hardware =
        MSX_MODEL_GENERIC_MSX2;
    models.entries[models.count].floppy.controller =
        MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793;
    models.entries[models.count].floppy.primary_slot = 2;
    models.entries[models.count].floppy.secondary_slot = -1;
    snprintf(models.entries[models.count].bios_path,
             sizeof(models.entries[models.count].bios_path), "%s",
             machine_bios_path);
    snprintf(models.entries[models.count].subrom_path,
             sizeof(models.entries[models.count].subrom_path), "%s",
             machine_subrom_path);
    snprintf(models.entries[models.count].disk_rom_path,
             sizeof(models.entries[models.count].disk_rom_path), "%s",
             machine_disk_rom_path);
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
#ifdef __APPLE__
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy",
                            SDL_HINT_OVERRIDE);
#else
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen",
                            SDL_HINT_OVERRIDE);
#endif
    assert(display_init(&display, &config, &msx, "Test MSX") == 0);
    test_cartridge_led_rendering(display.renderer);
    leds_init();
    leds_set_cartridge(0, LED_CARTRIDGE_NETWORK, true);
    leds_set_cartridge_activity(0, true);
    assert(leds_get_cartridge_state(0).type ==
           LED_CARTRIDGE_NETWORK);
    assert(leds_get_cartridge_state(0).present);
    assert(leds_get_cartridge_state(0).activity);
    overlay_init(&overlay, &config, &models, &display, &msx, NULL, NULL);
    assert(leds_get_cartridge_state(0).type ==
           LED_CARTRIDGE_STANDARD);
    assert(!leds_get_cartridge_state(0).present);
    assert(leds_get_cartridge_state(1).type ==
           LED_CARTRIDGE_STANDARD);
    assert(leds_get_cartridge_state(1).present);

    send_key(&overlay, SDLK_F9);
    assert(overlay.visible);
    test_fixed_overlay_scale(&display, &msx, &overlay);
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
    assert(overlay.machine_row == 4);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.machine_row == 0);
    send_key(&overlay, SDLK_UP);
    assert(overlay.machine_row == 4);
    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.model == MSX_MODEL_GENERIC_MSX1);
    assert(!overlay.dirty);

    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 2);
    assert(config.vdp_type == MSX_VDP_TMS9918);
    send_key(&overlay, SDLK_RETURN);
    assert(config.vdp_type == MSX_VDP_TMS9918);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 3);
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
    assert(overlay.row == 5);
    send_key(&overlay, SDLK_RETURN);
    assert(config.audio_volume == 90);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 6);
    send_key(&overlay, SDLK_RETURN);
    assert(config.main_input == INPUT_PORT_B);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 7);
    send_key(&overlay, SDLK_RETURN);
    assert(config.joy_port_device[0] == JOY_PORT_MOUSE);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 8);
    send_key(&overlay, SDLK_RETURN);
    assert(config.joy_port_device[1] == JOY_PORT_MOUSE);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 9);
    send_key(&overlay, SDLK_RETURN);
    assert(config.extra_hardware);

    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_MEDIA);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    assert(overlay.row == 0);
    render_overlay(&display, &msx, &overlay);

    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_SUNRISE_SETUP);
    assert(overlay.extension_setup_editing);
    assert(overlay.sunrise_setup_row == 0);
    assert(!config.sunrise_ide);
    assert(msx_get_cartridge(&msx, 1)->loaded);
    assert(config.cartridge_path[1][0]);
    render_overlay(&display, &msx, &overlay);
    overlay.dialog_target = OVERLAY_DIALOG_SUNRISE_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sunrise_rom_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(overlay.state == OVERLAY_STATE_SUNRISE_SETUP);
    assert(overlay.sunrise_setup_row == 1);
    assert(strcmp(overlay.pending_sunrise_rom_path,
                  sunrise_rom_path) == 0);
    assert(!config.sunrise_rom_path[0]);
    assert(!config.sunrise_ide);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.sunrise_setup_row == 2);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(!config.sunrise_ide);
    assert(!msx_sunrise_connected(&msx));
    assert(strcmp(config.sunrise_rom_path,
                   sunrise_rom_path) == 0);
    assert(!config.ide_image_path[0]);

    send_key(&overlay, SDLK_RETURN);
    assert(config.sunrise_ide);
    assert(msx_sunrise_connected(&msx));
    assert(!msx_sunrise_disk_mounted(&msx));
    assert(msx_sunrise_slot(&msx) == 0);
    assert(strcmp(config_cartridge_slot_owner(&config, 0),
                  "Sunrise IDE") == 0);
    assert(msx_get_cartridge(&msx, 1)->loaded);
    assert(config.cartridge_path[1][0]);
    assert(leds_get_cartridge_state(0).type ==
           LED_CARTRIDGE_STANDARD);
    assert(leds_get_cartridge_state(0).present);
    assert(!leds_get_cartridge_state(0).activity);

    snprintf(config.file_chooser_dir[
                 CONFIG_FILE_CHOOSER_SUNRISE_ROM], PATH_MAX,
             "before-sunrise-rom");
    snprintf(config.file_chooser_dir[
                 CONFIG_FILE_CHOOSER_IDE_IMAGE], PATH_MAX,
             "before-ide-image");
    send_key(&overlay, SDLK_SPACE);
    overlay.dialog_target = OVERLAY_DIALOG_SUNRISE_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sunrise_rom_path_2);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(strcmp(config.file_chooser_dir[
                      CONFIG_FILE_CHOOSER_SUNRISE_ROM],
                  "diagnostics") == 0);
    assert(strcmp(config.file_chooser_dir[
                      CONFIG_FILE_CHOOSER_IDE_IMAGE],
                  "before-ide-image") == 0);
    send_key(&overlay, SDLK_ESCAPE);
    assert(strcmp(config.file_chooser_dir[
                      CONFIG_FILE_CHOOSER_SUNRISE_ROM],
                  "diagnostics") == 0);
    assert(strcmp(config.file_chooser_dir[
                      CONFIG_FILE_CHOOSER_IDE_IMAGE],
                  "before-ide-image") == 0);
    assert(config.sunrise_ide);
    assert(msx_sunrise_connected(&msx));

    send_key(&overlay, SDLK_SPACE);
    overlay.dialog_target = OVERLAY_DIALOG_IDE_IMAGE;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", ide_image_path);
    overlay.dialog_ready = true;
    send_key(&overlay, SDLK_ESCAPE);
    overlay_tick(&overlay);
    assert(overlay.dialog_target == OVERLAY_DIALOG_NONE);
    assert(!config.ide_image_path[0]);
    assert(!msx_sunrise_disk_mounted(&msx));
    assert(strcmp(config.file_chooser_dir[
                      CONFIG_FILE_CHOOSER_IDE_IMAGE],
                  "before-ide-image") == 0);

    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_SUNRISE_SETUP);
    assert(overlay.extension_setup_editing);
    assert(strcmp(overlay.pending_sunrise_rom_path,
                  sunrise_rom_path) == 0);
    snprintf(overlay.pending_sunrise_rom_path,
             sizeof(overlay.pending_sunrise_rom_path),
             "diagnostics/missing-sunrise.rom");
    overlay.sunrise_setup_row = 2;
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_SUNRISE_SETUP);
    assert(config.sunrise_ide);
    assert(msx_sunrise_connected(&msx));
    assert(strcmp(config.sunrise_rom_path,
                  sunrise_rom_path) == 0);
    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.sunrise_ide);
    assert(msx_sunrise_connected(&msx));

    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_SUNRISE_SETUP);
    overlay.dialog_target = OVERLAY_DIALOG_SUNRISE_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sunrise_rom_path_2);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(overlay.sunrise_setup_row == 1);
    overlay.dialog_target = OVERLAY_DIALOG_IDE_IMAGE;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", ide_image_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(overlay.state == OVERLAY_STATE_SUNRISE_SETUP);
    assert(overlay.sunrise_setup_row == 2);
    assert(strcmp(overlay.pending_ide_image_path,
                  ide_image_path) == 0);
    assert(config.ide_image_mode == ATA_IMAGE_READ_ONLY);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.sunrise_ide);
    assert(msx_sunrise_connected(&msx));
    assert(msx_sunrise_slot(&msx) == 0);
    assert(strcmp(config.sunrise_rom_path,
                   sunrise_rom_path_2) == 0);
    assert(strcmp(config.ide_image_path,
                  ide_image_path) == 0);
    assert(msx_sunrise_disk_mounted(&msx));
    assert(!msx_sunrise_disk_writable(&msx));
    assert(config.ide_image_mode == ATA_IMAGE_READ_ONLY);

    send_key(&overlay, SDLK_RETURN);
    assert(!config.sunrise_ide);
    assert(!msx_sunrise_connected(&msx));
    assert(strcmp(config.sunrise_rom_path,
                  sunrise_rom_path_2) == 0);
    send_key(&overlay, SDLK_RETURN);
    assert(config.sunrise_ide);
    assert(msx_sunrise_connected(&msx));
    send_key(&overlay, SDLK_DELETE);
    assert(!config.sunrise_ide);
    assert(!msx_sunrise_connected(&msx));
    assert(!config.sunrise_rom_path[0]);
    assert(!config.ide_image_path[0]);

    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_SUNRISE_SETUP);
    assert(!overlay.extension_setup_editing);
    overlay.dialog_target = OVERLAY_DIALOG_SUNRISE_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sunrise_rom_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    overlay.dialog_target = OVERLAY_DIALOG_IDE_IMAGE;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", ide_image_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    send_key(&overlay, SDLK_RETURN);
    assert(config.sunrise_ide);
    assert(strcmp(config.sunrise_rom_path,
                  sunrise_rom_path) == 0);
    assert(strcmp(config.ide_image_path,
                  ide_image_path) == 0);
    assert(msx_sunrise_disk_mounted(&msx));

    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(!config.tcpip_unapi);
    send_key(&overlay, SDLK_RETURN);
    assert(config.tcpip_unapi);
    assert(config_cartridge_extension_count(&config) == 1);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_SPACE);
    assert(!config.scc);
    send_key(&overlay, SDLK_RETURN);
    assert(config.scc);
    assert(strcmp(config_cartridge_slot_owner(&config, 1),
                  "Konami SCC") == 0);
    assert(leds_get_cartridge_state(1).type ==
           LED_CARTRIDGE_STANDARD);
    assert(leds_get_cartridge_state(1).present);
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
    for (int row = 0; row < 4; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 4);
    overlay.dialog_target = OVERLAY_DIALOG_CASSETTE;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", cassette_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(msx_cassette_mounted(&msx));
    assert(msx_cassette_file_type(&msx) == CASSETTE_FILE_ASCII);
    assert(strcmp(config.cassette_path, cassette_path) == 0);
    render_overlay(&display, &msx, &overlay);
    msx.cassette.position = 10;
    send_key(&overlay, SDLK_R);
    assert(msx_cassette_position_ms(&msx) == 0);
    send_key(&overlay, SDLK_DELETE);
    assert(!msx_cassette_mounted(&msx));
    assert(!config.cassette_path[0]);
    overlay.dialog_target = OVERLAY_DIALOG_CASSETTE;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", cassette_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(msx_cassette_mounted(&msx));
    assert(strcmp(config.cassette_path, cassette_path) == 0);
    for (int row = 0; row < 2; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 6);
    assert(msx_sunrise_disk_mounted(&msx));
    assert(strcmp(config.ide_image_path, ide_image_path) == 0);
    send_key(&overlay, SDLK_DELETE);
    assert(!msx_sunrise_disk_mounted(&msx));
    assert(!config.ide_image_path[0]);
    overlay.dialog_target = OVERLAY_DIALOG_IDE_IMAGE;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", ide_image_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(msx_sunrise_disk_mounted(&msx));
    assert(strcmp(config.ide_image_path, ide_image_path) == 0);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 0);

    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_ADVANCED);
    assert(overlay.row == 0);
    for (int row = 0; row < 3; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 3);
    send_key(&overlay, SDLK_RETURN);
    assert(config.ide_image_mode == ATA_IMAGE_READ_WRITE);
    assert(msx_sunrise_disk_writable(&msx));
    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.sunrise_ide);
    assert(config_cartridge_slot_available(&config, 1));
    assert(strcmp(config_cartridge_slot_owner(&config, 0),
                  "Konami SCC") == 0);
    assert(leds_get_cartridge_state(0).type ==
           LED_CARTRIDGE_STANDARD);
    assert(leds_get_cartridge_state(0).present);
    assert(!leds_get_cartridge_state(1).present);

    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_MEDIA);
    for (int row = 0; row < 5; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 5);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 0);
    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_GENERAL);
    for (int row = 0; row < 9; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 9);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.extra_hardware);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_MEDIA);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_ADVANCED);
    assert(overlay.row == 0);
    for (int row = 0; row < 13; ++row)
        send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(config.cassette_audible_monitor);
    assert(msx.cassette_audible_monitor);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(config.cassette_visual_monitor);
    for (int row = 0; row < 14; ++row)
        send_key(&overlay, SDLK_UP);
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
    assert(models.count == 6);
    assert(model_catalog_find(&models, "new-model"));

    send_key(&overlay, SDLK_D);
    assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
    assert(overlay.model_edit_index == -1);
    assert(strcmp(overlay.model_edit.id,
                  "new-model-copy") == 0);
    send_key(&overlay, SDLK_F2);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(models.count == 7);

    send_key(&overlay, SDLK_DELETE);
    assert(overlay.state == OVERLAY_STATE_MODEL_DELETE);
    render_overlay(&display, &msx, &overlay);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(models.count == 6);
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

    /* The editor exposes controller topology independently of the
     * hardware profile and refuses an incomplete controller model. */
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.model_edit_field == 2);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.model_edit.hardware == MSX_MODEL_GENERIC_MSX2);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.model_edit_field == 3);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.model_edit.floppy.controller ==
           MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793);
    assert(overlay.model_edit.floppy.primary_slot == 3);
    assert(overlay.model_edit.floppy.secondary_slot == 3);
    send_key(&overlay, SDLK_F2);
    assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
    assert(strstr(overlay.model_editor_error, "disk ROM"));
    overlay.dialog_target = OVERLAY_DIALOG_MODEL_DISK_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", machine_disk_rom_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(strcmp(overlay.model_edit.disk_rom_path,
                  machine_disk_rom_path) == 0);
    send_key(&overlay, SDLK_F2);
    assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
    assert(strstr(model_catalog_find(
                      &models, "new-model")->disk_rom_path,
                  "test-machine-disk-rom.tmp"));

    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_MENU);
    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_CONFIRM);
    send_key(&overlay, SDLK_N);
    assert(!overlay.visible);
    assert(!msx_cassette_mounted(&msx));
    assert(!config.cassette_path[0]);

    /* The Extensions switch conditionally adds Floppy B to Media. */
    config_defaults(&config);
    config.model = MSX_MODEL_PHILIPS_NMS8250;
    snprintf(config.machine_id, sizeof(config.machine_id), "nms8250");
    config.floppy = models.entries[
        model_catalog_index(&models, "nms8250")].floppy;
    config.extra_hardware = true;
    config.tinker = true;
    msx_configure(&msx, config.model, config.region, 128);
    overlay_init(&overlay, &config, &models, &display, &msx, NULL, NULL);
    assert(msx_floppy_supported(&msx));
    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_MEDIA);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    for (int row = 0; row < 6; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 6);
    send_key(&overlay, SDLK_RETURN);
    assert(config.second_drive);
    assert(overlay_take_machine_reset_request(&overlay));
    assert(!overlay_take_machine_reset_request(&overlay));
    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_MEDIA);
    send_key(&overlay, SDLK_UP);
    assert(overlay.row == 6);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    for (int row = 0; row < 6; ++row)
        send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.second_drive);
    assert(overlay_take_machine_reset_request(&overlay));
    send_key(&overlay, SDLK_LEFT);
    send_key(&overlay, SDLK_UP);
    assert(overlay.row == 5);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_ADVANCED);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 1);
    assert(config.rtc_persistence);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.rtc_persistence);

    /* RS-232C toggling flips the device. */
    snprintf(config.rs232_rom_path, sizeof(config.rs232_rom_path),
             "%s", rs232_rom_path);
    send_key(&overlay, SDLK_UP);
    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    for (int row = 0; row < 7; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 7); /* EXTENSION_RS232 */
    assert(!config.rs232);
    send_key(&overlay, SDLK_RETURN);
    assert(config.rs232);
    assert(msx_rs232_connected(&msx));
    send_key(&overlay, SDLK_RETURN);
    assert(!config.rs232);
    assert(!msx_rs232_connected(&msx));

    /* Port-mapped FDC changes request a full guest reset only after the
     * new I/O gate is active. */
    config_defaults(&config);
    config.extra_hardware = true;
    config.tinker = true;
    msx_configure(&msx, config.model, config.region,
                  config.memory_kb);
    overlay_init(&overlay, &config, &models, &display, &msx, NULL, NULL);
    assert(!msx_floppy_supported(&msx));
    assert(!overlay_take_machine_reset_request(&overlay));
    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    for (int row = 0; row < 8; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 8); /* EXTENSION_CDX2 */
    msx.instructions = 123;
    overlay.dialog_target = OVERLAY_DIALOG_CDX2_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", cdx2_rom_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(config.cdx2);
    assert(strcmp(config.cdx2_rom_path, cdx2_rom_path) == 0);
    assert(msx_cdx2_connected(&msx));
    assert(msx_cdx2_slot(&msx) == 0);
    assert(msx_cdx2_rom_bank(&msx) == 0);
    assert(msx_get_cartridge(&msx, 0)->data[4] == 0x11);
    assert(msx_floppy_supported(&msx));
    /* Installing the cartridge ROM resets immediately; the main-loop
     * request also guarantees a reset after the overlay transition. */
    assert(msx.instructions == 0);
    assert(overlay_take_machine_reset_request(&overlay));
    msx_reset(&msx); /* main loop fulfils the request */
    assert(msx.instructions == 0);
    assert(msx_cdx2_connected(&msx));
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 9); /* EXTENSION_CDX2_ROM_SWITCH */
    send_key(&overlay, SDLK_RETURN);
    assert(config.cdx2_rom_bank == 1);
    assert(msx_cdx2_rom_bank(&msx) == 1);
    assert(msx_get_cartridge(&msx, 0)->data[4] == 0x22);
    assert(overlay_take_machine_reset_request(&overlay));
    send_key(&overlay, SDLK_UP);
    assert(overlay.row == 8); /* EXTENSION_CDX2 */
    send_key(&overlay, SDLK_RETURN);
    assert(!config.cdx2);
    assert(!msx_cdx2_connected(&msx));
    assert(!msx_floppy_supported(&msx));
    assert(overlay_take_machine_reset_request(&overlay));

    /* Cancelling the overlay restores the live gate as well as Config. */
    send_key(&overlay, SDLK_RETURN);
    assert(config.cdx2);
    assert(msx_cdx2_connected(&msx));
    assert(overlay_take_machine_reset_request(&overlay));
    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_CONFIRM);
    send_key(&overlay, SDLK_N);
    assert(!overlay.visible);
    assert(!config.cdx2);
    assert(!msx_cdx2_connected(&msx));
    assert(overlay_take_machine_reset_request(&overlay));

    /* The reverse rollback direction restores the ROM after generic
     * cartridge reconciliation, so it is not immediately ejected again. */
    config.cdx2 = true;
    snprintf(config.cdx2_rom_path, sizeof(config.cdx2_rom_path),
             "%s", cdx2_rom_path);
    config_normalize(&config);
    assert(msx_load_cdx2(
               &msx, 0,
               cdx2_rom_path,
               config.cdx2_rom_bank) == 0);
    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    for (int row = 0; row < 8; ++row)
        send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.cdx2);
    assert(!msx_cdx2_connected(&msx));
    (void)overlay_take_machine_reset_request(&overlay);
    send_key(&overlay, SDLK_ESCAPE);
    send_key(&overlay, SDLK_N);
    assert(config.cdx2);
    assert(msx_cdx2_connected(&msx));
    assert(msx_cdx2_slot(&msx) == 0);
    assert(overlay_take_machine_reset_request(&overlay));

    /* RDF600 follows the same cartridge-slot lifecycle while exposing a
     * separate TC8566AF-compatible controller. */
    assert(msx_eject_cdx2(&msx) == 0);
    config_defaults(&config);
    config.extra_hardware = true;
    config.tinker = true;
    msx_configure(&msx, config.model, config.region,
                  config.memory_kb);
    overlay_init(&overlay, &config, &models, &display, &msx, NULL, NULL);
    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    for (int row = 0; row < 10; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 10); /* EXTENSION_RDF600 */
    overlay.dialog_target = OVERLAY_DIALOG_RDF600_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", rdf600_rom_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(config.rdf600);
    assert(strcmp(config.rdf600_rom_path, rdf600_rom_path) == 0);
    assert(msx_rdf600_connected(&msx));
    assert(msx_rdf600_slot(&msx) == 0);
    assert(msx_floppy_supported(&msx));
    assert(overlay_take_machine_reset_request(&overlay));
    send_key(&overlay, SDLK_RETURN);
    assert(!config.rdf600);
    assert(!msx_rdf600_connected(&msx));
    assert(!msx_floppy_supported(&msx));
    assert(overlay_take_machine_reset_request(&overlay));

    /* SD Mapper setup keeps controller firmware separate from card media. */
    config_defaults(&config);
    config.extra_hardware = true;
    config.tinker = true;
    msx_configure(&msx, config.model, config.region,
                  config.memory_kb);
    overlay_init(&overlay, &config, &models, &display, &msx, NULL, NULL);
    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 1);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_SD_MAPPER_SETUP);
    overlay.dialog_target = OVERLAY_DIALOG_SD_MAPPER_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sd_mapper_rom_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(overlay.sd_mapper_setup_row == 1);
    overlay.dialog_target = OVERLAY_DIALOG_SD_CARD_A;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sd_image_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(overlay.sd_mapper_setup_row == 2);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!overlay.pending_sd_mapper_ram);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.pending_sd_mapper_alternate_driver);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.sd_mapper);
    assert(!config.sd_mapper_ram);
    assert(config.sd_mapper_alternate_driver);
    assert(msx_sd_mapper_connected(&msx));
    assert(msx_sd_mapper_slot(&msx) == 0);
    assert(msx_sd_card_mounted(&msx, 0));
    assert(!msx_sd_card_writable(&msx, 0));
    assert(strcmp(config.sd_mapper_rom_path,
                  sd_mapper_rom_path) == 0);
    assert(strcmp(config.sd_card_path[0],
                  sd_image_path) == 0);
    assert(leds_get_cartridge_state(0).present);

    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_SD_MAPPER_SETUP);
    assert(overlay.extension_setup_editing);
    assert(strcmp(overlay.pending_sd_mapper_rom_path,
                  sd_mapper_rom_path) == 0);
    assert(strcmp(overlay.pending_sd_card_path[0],
                  sd_image_path) == 0);
    assert(!overlay.pending_sd_mapper_ram);
    assert(overlay.pending_sd_mapper_alternate_driver);
    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.sd_mapper);
    assert(msx_sd_mapper_connected(&msx));

    send_key(&overlay, SDLK_SPACE);
    for (int row = 0; row < 3; ++row)
        send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.pending_sd_mapper_ram);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!overlay.pending_sd_mapper_alternate_driver);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.sd_mapper);
    assert(config.sd_mapper_ram);
    assert(!config.sd_mapper_alternate_driver);
    assert(msx_sd_mapper_connected(&msx));
    assert(msx_sd_mapper_slot(&msx) == 0);
    assert(msx_sd_card_mounted(&msx, 0));

    snprintf(config.sunrise_rom_path,
             sizeof(config.sunrise_rom_path), "%s", sunrise_rom_path);
    send_key(&overlay, SDLK_UP);
    assert(overlay.row == 0);
    send_key(&overlay, SDLK_RETURN);
    assert(config.sunrise_ide);
    assert(msx_sunrise_slot(&msx) == 0);
    assert(msx_sd_mapper_slot(&msx) == 1);
    assert(msx_sd_card_mounted(&msx, 0));
    send_key(&overlay, SDLK_RETURN);
    assert(!config.sunrise_ide);
    assert(msx_sd_mapper_slot(&msx) == 0);
    assert(msx_sd_card_mounted(&msx, 0));
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 1);

    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_MEDIA);
    send_key(&overlay, SDLK_UP);
    send_key(&overlay, SDLK_UP);
    assert(overlay.row == 6);
    send_key(&overlay, SDLK_DELETE);
    assert(!msx_sd_card_mounted(&msx, 0));
    assert(!config.sd_card_path[0][0]);
    overlay.dialog_target = OVERLAY_DIALOG_SD_CARD_A;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sd_image_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(msx_sd_card_mounted(&msx, 0));

    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_ADVANCED);
    for (int row = 0; row < 4; ++row)
        send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 4);
    send_key(&overlay, SDLK_RETURN);
    assert(config.sd_image_mode == SD_IMAGE_READ_WRITE);
    assert(msx_sd_card_writable(&msx, 0));
    send_key(&overlay, SDLK_LEFT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.sd_mapper);
    assert(!msx_sd_mapper_connected(&msx));
    assert(config.sd_mapper_rom_path[0]);
    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_SD_MAPPER_SETUP);
    assert(overlay.extension_setup_editing);
    assert(strcmp(overlay.pending_sd_mapper_rom_path,
                  config.sd_mapper_rom_path) == 0);
    send_key(&overlay, SDLK_ESCAPE);
    send_key(&overlay, SDLK_DELETE);
    assert(!config.sd_mapper_rom_path[0]);
    assert(!config.sd_card_path[0][0]);
    assert(!config.sd_card_path[1][0]);
    assert(config.sd_mapper_ram);
    assert(!config.sd_mapper_alternate_driver);
    send_key(&overlay, SDLK_F9);
    assert(!overlay.visible);

    /* MegaFlash setup owns one slot but exposes two removable SD cards. */
    config_defaults(&config);
    snprintf(config.path, sizeof(config.path), "%s",
             megaflash_config_path);
    config.extra_hardware = true;
    config.tinker = true;
    char megaflash_state_path[PATH_MAX];
    char megaflash_pending_path[PATH_MAX];
    char megaflash_pending_path_2[PATH_MAX];
    {
        Config state_config = config;

        state_config.megaflash = true;
        snprintf(state_config.megaflash_rom_path,
                 sizeof(state_config.megaflash_rom_path), "%s",
                 megaflash_rom_path);
        assert(config_megaflash_state_path(
                   &state_config, megaflash_state_path,
                   sizeof(megaflash_state_path)) == 0);
        assert(config_megaflash_pending_state_path(
                   &state_config, megaflash_pending_path,
                   sizeof(megaflash_pending_path)) == 0);
        snprintf(state_config.megaflash_rom_path,
                 sizeof(state_config.megaflash_rom_path), "%s",
                 megaflash_rom_path_2);
        assert(config_megaflash_pending_state_path(
                   &state_config, megaflash_pending_path_2,
                   sizeof(megaflash_pending_path_2)) == 0);
        (void)remove(megaflash_config_path);
        (void)remove(megaflash_state_path);
        (void)remove(megaflash_pending_path);
        (void)remove(megaflash_pending_path_2);
    }
    msx_configure(&msx, config.model, config.region,
                  config.memory_kb);
    overlay_init(&overlay, &config, &models, &display, &msx, NULL, NULL);
    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    assert(overlay.section == OVERLAY_EXTENSIONS);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    assert(overlay.row == 2);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MEGAFLASH_SETUP);
    overlay.dialog_target = OVERLAY_DIALOG_MEGAFLASH_ROM;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", megaflash_rom_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(overlay.megaflash_setup_row == 1);
    overlay.dialog_target = OVERLAY_DIALOG_MEGAFLASH_SD_A;
    snprintf(overlay.dialog_path, sizeof(overlay.dialog_path),
             "%s", sd_image_path);
    overlay.dialog_ready = true;
    overlay_tick(&overlay);
    assert(overlay.megaflash_setup_row == 2);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.megaflash);
    assert(msx_megaflash_connected(&msx));
    assert(msx_megaflash_slot(&msx) == 0);
    assert(msx_megaflash_card_mounted(&msx, 0));
    assert(strcmp(config.megaflash_rom_path,
                  megaflash_rom_path) == 0);
    assert(strcmp(config.megaflash_card_path[0],
                  sd_image_path) == 0);
    assert(overlay.megaflash_state_pending);
    fixture = fopen(megaflash_pending_path, "rb");
    assert(fixture);
    assert(fclose(fixture) == 0);
    assert(!fopen(megaflash_state_path, "rb"));

    assert(!send_quit(&overlay));
    assert(!overlay.visible);
    assert(!overlay.megaflash_state_pending);
    fixture = fopen(megaflash_state_path, "rb");
    assert(fixture);
    assert(fgetc(fixture) == 0xff);
    assert(fclose(fixture) == 0);
    assert(!fopen(megaflash_pending_path, "rb"));

    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_SPACE);
    snprintf(overlay.pending_megaflash_rom_path,
             sizeof(overlay.pending_megaflash_rom_path), "%s",
             megaflash_rom_path_2);
    overlay.megaflash_setup_row = 3;
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(overlay.megaflash_state_pending);
    assert(strcmp(overlay.megaflash_pending_state_path,
                  megaflash_pending_path_2) == 0);
    assert(msx.megaflash.flash[0] == 0x44);
    fixture = fopen(megaflash_state_path, "rb");
    assert(fixture);
    assert(fgetc(fixture) == 0xff);
    assert(fclose(fixture) == 0);
    send_key(&overlay, SDLK_ESCAPE);
    assert(overlay.state == OVERLAY_STATE_CONFIRM);
    send_key(&overlay, SDLK_N);
    assert(!overlay.visible);
    assert(!overlay.megaflash_state_pending);
    assert(strcmp(config.megaflash_rom_path,
                  megaflash_rom_path) == 0);
    assert(msx.megaflash.flash[0] == 0xff);
    assert(!fopen(megaflash_pending_path, "rb"));
    assert(!fopen(megaflash_pending_path_2, "rb"));

    send_key(&overlay, SDLK_F9);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);

    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_MEGAFLASH_SETUP);
    assert(overlay.extension_setup_editing);
    assert(strcmp(overlay.pending_megaflash_rom_path,
                  megaflash_rom_path) == 0);
    assert(strcmp(overlay.pending_megaflash_card_path[0],
                  sd_image_path) == 0);
    send_key(&overlay, SDLK_ESCAPE);
    assert(config.megaflash);
    assert(msx_megaflash_connected(&msx));

    send_key(&overlay, SDLK_SPACE);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DELETE);
    assert(!overlay.pending_megaflash_card_path[0][0]);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(overlay.state == OVERLAY_STATE_MENU);
    assert(config.megaflash);
    assert(msx_megaflash_connected(&msx));
    assert(msx_megaflash_slot(&msx) == 0);
    assert(!msx_megaflash_card_mounted(&msx, 0));
    assert(!config.megaflash_card_path[0][0]);

    send_key(&overlay, SDLK_LEFT);
    send_key(&overlay, SDLK_UP);
    send_key(&overlay, SDLK_UP);
    assert(overlay.row == 6);
    send_key(&overlay, SDLK_DELETE);
    assert(!msx_megaflash_card_mounted(&msx, 0));
    assert(!config.megaflash_card_path[0][0]);
    send_key(&overlay, SDLK_RIGHT);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_DOWN);
    send_key(&overlay, SDLK_RETURN);
    assert(!config.megaflash);
    assert(!msx_megaflash_connected(&msx));
    send_key(&overlay, SDLK_SPACE);
    assert(overlay.state == OVERLAY_STATE_MEGAFLASH_SETUP);
    assert(overlay.extension_setup_editing);
    assert(strcmp(overlay.pending_megaflash_rom_path,
                  config.megaflash_rom_path) == 0);
    send_key(&overlay, SDLK_ESCAPE);
    send_key(&overlay, SDLK_DELETE);
    assert(!config.megaflash_rom_path[0]);
    assert(!config.megaflash_card_path[0][0]);
    assert(!config.megaflash_card_path[1][0]);
    send_key(&overlay, SDLK_F9);
    assert(!overlay.visible);

    /* General applies an editor-defined firmware set without file pickers. */
    {
        size_t model_index = model_catalog_index(
            &models, "custom-floppy-msx2");
        size_t cbios_index = model_catalog_index(&models, "cbios");
        size_t nms8250_index = model_catalog_index(&models, "nms8250");
        char custom_bios_path[PATH_MAX];
        char loaded_bios_path[PATH_MAX];
        char blocked_rtc_path[PATH_MAX];
        u8 active_bios[MSX_BIOS_SIZE];
        bool active_dirty;

        assert(model_index < models.count);
        assert(cbios_index < models.count);
        assert(nms8250_index < models.count);
        config_defaults(&config);
        config.tinker = true;
        msx_configure(&msx, config.model, config.region,
                      config.memory_kb);
        assert(msx_install_cartridge_slot(
                   &msx, 1, cartridge, sizeof(cartridge),
                   MSX_CART_MAPPER_LINEAR) == 0);
        snprintf(config.cartridge_path[1],
                 sizeof(config.cartridge_path[1]),
                 "test-cartridge-2.rom");
        overlay_init(&overlay, &config, &models, &display, &msx, NULL, NULL);
        send_key(&overlay, SDLK_F9);
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MACHINE);
        overlay.machine_row = (int)model_index;
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MENU);
        assert(overlay.dialog_target == OVERLAY_DIALOG_NONE);
        assert(config.model == MSX_MODEL_GENERIC_MSX2);
        assert(config.vdp_type == MSX_VDP_V9958);
        assert(msx.vdp.type == MSX_VDP_V9958);
        assert(strcmp(config.machine_id, "custom-floppy-msx2") == 0);
        assert(config.memory_kb == 128);
        assert(msx.bios_loaded);
        assert(msx.subrom_loaded);
        assert(msx.disk_rom_loaded);
        assert(msx_floppy_supported(&msx));
        assert(strcmp(config.disk_rom_path,
                      models.entries[model_index].disk_rom_path) == 0);
        assert(strcmp(config_cartridge_slot_owner(&config, 1),
                      "Floppy controller") == 0);
        assert(!config.cartridge_path[1][0]);
        assert(!msx_get_cartridge(&msx, 1)->loaded);

        send_key(&overlay, SDLK_DOWN);
        send_key(&overlay, SDLK_DOWN);
        assert(overlay.row == 2);
        send_key(&overlay, SDLK_RETURN);
        assert(config.vdp_type == MSX_VDP_V9938);
        assert(msx.vdp.type == MSX_VDP_V9938);
        send_key(&overlay, SDLK_UP);
        send_key(&overlay, SDLK_UP);
        assert(overlay.row == 0);

        snprintf(loaded_bios_path, sizeof(loaded_bios_path), "%s",
                 config.bios_path);
        snprintf(custom_bios_path, sizeof(custom_bios_path), "%s",
                 config.bios_path);
        send_key(&overlay, SDLK_DELETE);
        assert(strcmp(config.bios_path, loaded_bios_path) == 0);
        assert(msx.bios_loaded);

        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MACHINE);
        overlay.machine_row = (int)cbios_index;
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MENU);
        assert(config.model == MSX_MODEL_GENERIC_MSX1);
        assert(config.vdp_type == MSX_VDP_TMS9918);
        assert(strcmp(config.machine_id, "cbios") == 0);
        assert(config.memory_kb == 64);
        assert(msx.profile->model == MSX_MODEL_GENERIC_MSX1);
        assert(msx.bios_loaded);
        assert(!msx.subrom_loaded);
        assert(!msx.disk_rom_loaded);

        /* Editing the selected catalogue entry applies its hardware to the
         * live Config/MsxMachine instead of waiting for process restart,
         * while preserving the user's runtime RAM expansion. */
        config.memory_kb = 256;
        msx_configure(&msx, config.model, config.region,
                      config.memory_kb);
        assert(msx.ram_kb == 256);
        send_key(&overlay, SDLK_LEFT);
        assert(overlay.section == OVERLAY_ADVANCED);
        assert(overlay.row == 0);
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
        assert(overlay.model_editor_row == (int)cbios_index);
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MODEL_EDIT);
        overlay.model_edit.floppy.controller =
            MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793;
        overlay.model_edit.floppy.primary_slot = 2;
        overlay.model_edit.floppy.secondary_slot = -1;
        snprintf(overlay.model_edit.disk_rom_path,
                 sizeof(overlay.model_edit.disk_rom_path), "%s",
                 machine_disk_rom_path);
        msx.instructions = 123;
        send_key(&overlay, SDLK_F2);
        assert(overlay.state == OVERLAY_STATE_MENU);
        assert(config.floppy.controller ==
               MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793);
        assert(msx_floppy_supported(&msx));
        assert(msx.disk_rom_loaded);
        assert(config.memory_kb == 256);
        assert(msx.ram_kb == 256);
        assert(msx.instructions == 0);
        assert(overlay_take_machine_reset_request(&overlay));

        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MODEL_LIST);
        assert(overlay.model_editor_row == (int)cbios_index);
        send_key(&overlay, SDLK_RETURN);
        overlay.model_edit.floppy.controller =
            MSX_FLOPPY_CONTROLLER_NONE;
        overlay.model_edit.floppy.primary_slot = -1;
        overlay.model_edit.floppy.secondary_slot = -1;
        overlay.model_edit.disk_rom_path[0] = '\0';
        msx.instructions = 123;
        send_key(&overlay, SDLK_F2);
        assert(overlay.state == OVERLAY_STATE_MENU);
        assert(config.floppy.controller ==
               MSX_FLOPPY_CONTROLLER_NONE);
        assert(!msx_floppy_supported(&msx));
        assert(!msx.disk_rom_loaded);
        assert(config.memory_kb == 256);
        assert(msx.ram_kb == 256);
        assert(msx.instructions == 0);
        assert(overlay_take_machine_reset_request(&overlay));
        send_key(&overlay, SDLK_RIGHT);
        assert(overlay.section == OVERLAY_GENERAL);

        snprintf(loaded_bios_path, sizeof(loaded_bios_path), "%s",
                 config.bios_path);
        memcpy(active_bios, msx.bios, sizeof(active_bios));
        active_dirty = overlay.dirty;
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MACHINE);
        overlay.machine_row = (int)model_index;
        snprintf(models.entries[model_index].bios_path,
                 sizeof(models.entries[model_index].bios_path),
                 "diagnostics/missing-machine-bios.rom");
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MACHINE);
        assert(overlay.dialog_target == OVERLAY_DIALOG_NONE);
        assert(config.model == MSX_MODEL_GENERIC_MSX1);
        assert(strcmp(config.machine_id, "cbios") == 0);
        assert(config.memory_kb == 256);
        assert(strcmp(config.bios_path, loaded_bios_path) == 0);
        assert(msx.profile->model == MSX_MODEL_GENERIC_MSX1);
        assert(msx.bios_loaded);
        assert(!msx.subrom_loaded);
        assert(!msx.disk_rom_loaded);
        assert(memcmp(active_bios, msx.bios, sizeof(active_bios)) == 0);
        assert(overlay.dirty == active_dirty);

        snprintf(models.entries[model_index].bios_path,
                 sizeof(models.entries[model_index].bios_path), "%s",
                 custom_bios_path);
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MENU);
        assert(config.model == MSX_MODEL_GENERIC_MSX2);
        assert(strcmp(config.machine_id, "custom-floppy-msx2") == 0);
        memcpy(active_bios, msx.bios, sizeof(active_bios));
        snprintf(blocked_rtc_path, sizeof(blocked_rtc_path), "%s/rtc",
                 machine_bios_path);
        assert(msx_set_rtc_persistence(
                   &msx, blocked_rtc_path, 1000) != 0);
        assert(msx_rtc_persistence_dirty(&msx));
        assert(strcmp(msx_rtc_persistence_path(&msx),
                      blocked_rtc_path) == 0);
        active_dirty = overlay.dirty;

        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MACHINE);
        overlay.machine_row = (int)nms8250_index;
        send_key(&overlay, SDLK_RETURN);
        assert(overlay.state == OVERLAY_STATE_MACHINE);
        assert(overlay.dialog_target == OVERLAY_DIALOG_NONE);
        assert(config.model == MSX_MODEL_GENERIC_MSX2);
        assert(strcmp(config.machine_id, "custom-floppy-msx2") == 0);
        assert(msx.profile->model == MSX_MODEL_GENERIC_MSX2);
        assert(memcmp(active_bios, msx.bios, sizeof(active_bios)) == 0);
        assert(overlay.dirty == active_dirty);
    }

    assert(remove(editor_path) == 0);
    assert(remove(machine_bios_path) == 0);
    assert(remove(machine_subrom_path) == 0);
    assert(remove(machine_disk_rom_path) == 0);
    assert(remove(sunrise_rom_path) == 0);
    assert(remove(sunrise_rom_path_2) == 0);
    assert(remove(ide_image_path) == 0);
    assert(remove(sd_mapper_rom_path) == 0);
    assert(remove(sd_image_path) == 0);
    assert(remove(megaflash_rom_path) == 0);
    assert(remove(megaflash_rom_path_2) == 0);
    assert(remove(megaflash_config_path) == 0);
    assert(remove(megaflash_state_path) == 0);
    assert(TEST_RMDIR("diagnostics/test-overlay-state/flash") == 0);
    assert(TEST_RMDIR("diagnostics/test-overlay-state") == 0);
    assert(remove(cassette_path) == 0);
    assert(remove(rs232_rom_path) == 0);
    assert(remove(cdx2_rom_path) == 0);
    assert(remove(rdf600_rom_path) == 0);

    display_quit(&display);
    msx_destroy(&msx);
    puts("overlay machine chooser and model editor tests passed");
    return 0;
}
