#include <SDL3/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "display.h"
#include "kbd.h"
#include "leds.h"
#include "models.h"
#include "msx.h"
#include "notify.h"
#include "overlay.h"
#include "ui.h"

#ifndef PROG_GIT_COMMIT
#define PROG_GIT_COMMIT "unknown"
#endif

typedef struct {
    const char *config_path;
    const char *models_path;
    const char *bios_path;
    const char *logo_path;
    const char *subrom_path;
    const char *disk_rom_path;
    const char *cartridge_path[MSX_CARTRIDGE_SLOTS];
    MsxCartridgeMapper cartridge_mapper[MSX_CARTRIDGE_SLOTS];
    bool cartridge_mapper_set[MSX_CARTRIDGE_SLOTS];
    const char *model_name;
    int region;
    int scale;
    int exit_after;
    bool headless;
    bool unthrottled;
    bool dump_state;
} Cli;

static const char *usage =
    "Usage: 1983 [options]\n"
    "  --config PATH       use an alternative configuration file\n"
    "  --models PATH       use an alternative machine catalogue\n"
    "  --model NAME        select a model ID from the machine catalogue\n"
    "  --region pal|ntsc   override the configured video standard\n"
    "  --bios PATH         load a 32 KB MSX BIOS ROM\n"
    "  --logo PATH         load a 16 KB C-BIOS logo ROM in slot 0/page 2\n"
    "  --subrom PATH       load a 16 KB MSX2 Sub-ROM in slot 3-0\n"
    "  --disk-rom PATH     load a 16 KB disk ROM in slot 3-3/page 1\n"
    "  --cart PATH         alias for --cart1\n"
    "  --cart1 PATH        load a cartridge ROM in primary slot 1\n"
    "  --cart2 PATH        load a cartridge ROM in primary slot 2\n"
    "  --mapper NAME       alias for --mapper1\n"
    "  --mapper1 NAME      slot 1 mapper: auto, linear, ascii8, ascii16,\n"
    "                      konami, or konami-scc\n"
    "  --mapper2 NAME      slot 2 mapper (same names as --mapper1)\n"
    "  --scale N           initial window scale (1 through 4)\n"
    "  --headless          use SDL's offscreen video backend\n"
    "  --exit-after N      exit after N host frames (for smoke tests)\n"
    "  --unthrottled       disable 50/60 Hz frame pacing\n"
    "  --dump-state        print CPU/bus/VDP state on exit\n"
    "  -h, --help          show this help\n"
    "  --version           show version information\n";

static int parse_integer(const char *text, int minimum, int maximum,
                         const char *option) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || !end || *end || value < minimum || value > maximum) {
        fprintf(stderr, "%s: expected an integer from %d through %d\n",
                option, minimum, maximum);
        return -1;
    }
    return (int)value;
}

static int parse_region(const char *text) {
    if (strcmp(text, "pal") == 0)
        return MSX_REGION_PAL;
    if (strcmp(text, "ntsc") == 0)
        return MSX_REGION_NTSC;
    fprintf(stderr, "--region: expected pal or ntsc\n");
    return -1;
}

static int parse_mapper(const char *text, const char *option,
                        MsxCartridgeMapper *mapper) {
    if (msx_cartridge_mapper_from_name(text, mapper))
        return 0;
    fprintf(stderr,
            "%s: expected auto, linear, ascii8, ascii16, konami, "
            "or konami-scc\n", option);
    return -1;
}

static int parse_cli(int argc, char **argv, Cli *cli) {
    memset(cli, 0, sizeof(*cli));
    cli->region = -1;
    cli->scale = -1;
    cli->exit_after = -1;

    for (int i = 1; i < argc; ++i) {
        const char *argument = argv[i];
        if (strcmp(argument, "-h") == 0 ||
            strcmp(argument, "--help") == 0) {
            fputs(usage, stdout);
            return 1;
        }
        if (strcmp(argument, "--version") == 0) {
            printf("1983 0.1.0 (git %s)\n", PROG_GIT_COMMIT);
            return 1;
        }
        if (strcmp(argument, "--headless") == 0) {
            cli->headless = true;
            continue;
        }
        if (strcmp(argument, "--unthrottled") == 0) {
            cli->unthrottled = true;
            continue;
        }
        if (strcmp(argument, "--dump-state") == 0) {
            cli->dump_state = true;
            continue;
        }
        if ((strcmp(argument, "--config") == 0 ||
             strcmp(argument, "--models") == 0 ||
             strcmp(argument, "--model") == 0 ||
             strcmp(argument, "--region") == 0 ||
             strcmp(argument, "--bios") == 0 ||
             strcmp(argument, "--logo") == 0 ||
             strcmp(argument, "--subrom") == 0 ||
             strcmp(argument, "--disk-rom") == 0 ||
             strcmp(argument, "--cart") == 0 ||
             strcmp(argument, "--cart1") == 0 ||
             strcmp(argument, "--cart2") == 0 ||
             strcmp(argument, "--mapper") == 0 ||
             strcmp(argument, "--mapper1") == 0 ||
             strcmp(argument, "--mapper2") == 0 ||
             strcmp(argument, "--scale") == 0 ||
             strcmp(argument, "--exit-after") == 0) &&
            i + 1 >= argc) {
            fprintf(stderr, "%s requires a value\n", argument);
            return -1;
        }
        if (strcmp(argument, "--config") == 0) {
            cli->config_path = argv[++i];
        } else if (strcmp(argument, "--models") == 0) {
            cli->models_path = argv[++i];
        } else if (strcmp(argument, "--bios") == 0) {
            cli->bios_path = argv[++i];
        } else if (strcmp(argument, "--logo") == 0) {
            cli->logo_path = argv[++i];
        } else if (strcmp(argument, "--subrom") == 0) {
            cli->subrom_path = argv[++i];
        } else if (strcmp(argument, "--disk-rom") == 0) {
            cli->disk_rom_path = argv[++i];
        } else if (strcmp(argument, "--cart") == 0 ||
                   strcmp(argument, "--cart1") == 0) {
            cli->cartridge_path[0] = argv[++i];
        } else if (strcmp(argument, "--cart2") == 0) {
            cli->cartridge_path[1] = argv[++i];
        } else if (strcmp(argument, "--mapper") == 0 ||
                   strcmp(argument, "--mapper1") == 0) {
            if (parse_mapper(argv[++i], argument,
                             &cli->cartridge_mapper[0]) != 0)
                return -1;
            cli->cartridge_mapper_set[0] = true;
        } else if (strcmp(argument, "--mapper2") == 0) {
            if (parse_mapper(argv[++i], argument,
                             &cli->cartridge_mapper[1]) != 0)
                return -1;
            cli->cartridge_mapper_set[1] = true;
        } else if (strcmp(argument, "--model") == 0) {
            cli->model_name = argv[++i];
        } else if (strcmp(argument, "--region") == 0) {
            cli->region = parse_region(argv[++i]);
            if (cli->region < 0)
                return -1;
        } else if (strcmp(argument, "--scale") == 0) {
            cli->scale = parse_integer(argv[++i], 1, 4, "--scale");
            if (cli->scale < 0)
                return -1;
        } else if (strcmp(argument, "--exit-after") == 0) {
            cli->exit_after =
                parse_integer(argv[++i], 0, 1000000000, "--exit-after");
            if (cli->exit_after < 0)
                return -1;
        } else {
            fprintf(stderr, "unknown option: %s\n", argument);
            fputs(usage, stderr);
            return -1;
        }
    }
    return 0;
}

static void track_led_mouse(Display *display, SDL_WindowID window_id,
                            const SDL_Event *event) {
    float x;
    float y;

    if (event->type == SDL_EVENT_MOUSE_MOTION &&
        event->motion.windowID == window_id &&
        SDL_RenderCoordinatesFromWindow(display->renderer,
                                        event->motion.x, event->motion.y,
                                        &x, &y)) {
        leds_set_mouse_position(x, y, true);
    } else if ((event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
                event->type == SDL_EVENT_WINDOW_FOCUS_LOST) &&
               event->window.windowID == window_id) {
        leds_set_mouse_position(0.0f, 0.0f, false);
    }
}

static void draw_debug(const Config *config, const MsxMachine *msx,
                       Display *display) {
    static Uint64 last_ns;
    static float fps_smooth;
    static unsigned fps_samples;
    char text[160];
    Uint64 now;

    if (!config->debug) {
        last_ns = 0;
        fps_smooth = 0.0f;
        fps_samples = 0;
        return;
    }

    now = SDL_GetTicksNS();
    if (last_ns && now > last_ns) {
        float fps = 1000000000.0f / (float)(now - last_ns);
        if (!fps_samples)
            fps_smooth = fps;
        else
            fps_smooth = fps_smooth * 0.95f + fps * 0.05f;
        ++fps_samples;
    }
    last_ns = now;

    snprintf(text, sizeof(text),
             "DBG %.1f/%d fps frame=%llu PC=%04X slot=%02X cycles=%llu",
             (double)fps_smooth, msx->frame_hz,
             (unsigned long long)msx->frame, msx->cpu.pc,
             msx->primary_slot,
             (unsigned long long)msx->cycles);
    ui_fill_rect(display->renderer, 6.0f, 448.0f,
                 (float)strlen(text) * 8.0f + 12.0f, 20.0f,
                 0, 0, 0, 210);
    ui_draw_text(display->renderer, 12.0f, 454.0f, text,
                 255, 192, 64);
}

static void draw_paused(const MsxMachine *msx, Display *display) {
    const char *text = "PAUSED";
    if (!msx->paused)
        return;
    ui_fill_rect(display->renderer, 272.0f, 210.0f, 96.0f, 44.0f,
                 0, 0, 0, 210);
    ui_draw_rect(display->renderer, 272.0f, 210.0f, 96.0f, 44.0f,
                 240, 240, 240);
    ui_draw_text(display->renderer, 296.0f, 228.0f, text,
                 255, 255, 255);
}

int main(int argc, char **argv) {
    Cli cli;
    Config config;
    ModelCatalog models;
    const ModelDefinition *definition;
    MsxMachine msx;
    KbdHost keyboard;
    static Display display;
    AudioOutput audio;
    Overlay overlay;
    SDL_WindowID window_id;
    bool running = true;
    int host_frame = 0;
    Uint64 next_frame_ns;
    unsigned frame_ns_remainder = 0;
    int paced_frame_hz;
    int cli_result = parse_cli(argc, argv, &cli);

    if (cli_result != 0)
        return cli_result > 0 ? 0 : 1;

    config_load(&config, cli.config_path);
    if (model_catalog_load(&models, cli.models_path) != 0) {
        if (cli.models_path) {
            fprintf(stderr, "cannot load machine catalogue: %s\n",
                    cli.models_path);
            return 1;
        }
        fprintf(stderr,
                "warning: 1983-models.conf not found or invalid; "
                "using built-in models\n");
    }
    definition = model_catalog_find(&models, config.machine_id);
    if (!definition)
        definition = model_catalog_find_hardware(&models, config.model);
    if (definition) {
        config.model = definition->hardware;
        snprintf(config.machine_id, sizeof(config.machine_id),
                 "%s", definition->id);
        if (!config.bios_path[0])
            snprintf(config.bios_path, sizeof(config.bios_path),
                     "%s", definition->bios_path);
        if (!config.logo_path[0])
            snprintf(config.logo_path, sizeof(config.logo_path),
                     "%s", definition->logo_path);
        if (!config.subrom_path[0])
            snprintf(config.subrom_path, sizeof(config.subrom_path),
                     "%s", definition->subrom_path);
        if (!config.disk_rom_path[0])
            snprintf(config.disk_rom_path, sizeof(config.disk_rom_path),
                     "%s", definition->disk_rom_path);
    }
    if (cli.model_name) {
        MsxModel hardware;

        definition = model_catalog_find(&models, cli.model_name);
        if (!definition &&
            msx_model_from_name(cli.model_name, &hardware))
            definition = model_catalog_find_hardware(&models, hardware);
        if (!definition) {
            fprintf(stderr, "--model: unknown catalogue model: %s\n",
                    cli.model_name);
            return 1;
        }
        config.model = definition->hardware;
        snprintf(config.machine_id, sizeof(config.machine_id),
                 "%s", definition->id);
        config.memory_kb = msx_default_ram_kb(config.model);
        snprintf(config.bios_path, sizeof(config.bios_path),
                 "%s", definition->bios_path);
        snprintf(config.logo_path, sizeof(config.logo_path),
                 "%s", definition->logo_path);
        snprintf(config.subrom_path, sizeof(config.subrom_path),
                 "%s", definition->subrom_path);
        snprintf(config.disk_rom_path, sizeof(config.disk_rom_path),
                 "%s", definition->disk_rom_path);
    }
    if (cli.region >= 0)
        config.region = (MsxRegion)cli.region;
    if (cli.scale >= 0)
        config.scale = cli.scale;
    if (cli.bios_path)
        snprintf(config.bios_path, sizeof(config.bios_path),
                 "%s", cli.bios_path);
    if (cli.logo_path)
        snprintf(config.logo_path, sizeof(config.logo_path),
                 "%s", cli.logo_path);
    if (cli.subrom_path)
        snprintf(config.subrom_path, sizeof(config.subrom_path),
                 "%s", cli.subrom_path);
    if (cli.disk_rom_path)
        snprintf(config.disk_rom_path, sizeof(config.disk_rom_path),
                 "%s", cli.disk_rom_path);
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        if (cli.cartridge_path[slot])
            snprintf(config.cartridge_path[slot],
                     sizeof(config.cartridge_path[slot]), "%s",
                     cli.cartridge_path[slot]);
        if (cli.cartridge_mapper_set[slot])
            config.cartridge_mapper[slot] = cli.cartridge_mapper[slot];
    }
    config_normalize(&config);

    if (cli.headless) {
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen",
                                SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy",
                                SDL_HINT_OVERRIDE);
    }

    msx_init(&msx, config.model, config.region, config.memory_kb);
    kbd_init(&keyboard);
    if (config.bios_path[0] &&
        msx_load_firmware_set(
            &msx, config.bios_path, config.logo_path,
            config.subrom_path, config.disk_rom_path) < 0) {
        fprintf(stderr, "cannot load firmware set for %s\n",
                config.machine_id);
        if (cli.bios_path || cli.logo_path ||
            cli.subrom_path || cli.disk_rom_path ||
            cli.model_name) {
            msx_destroy(&msx);
            return 1;
        }
    }
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        const char *path = config.cartridge_path[slot];

        if (!path[0])
            continue;
        if (msx_load_cartridge_slot(
                &msx, slot, path, config.cartridge_mapper[slot]) == 0)
            continue;
        fprintf(stderr, "cannot load cartridge %u ROM: %s\n",
                slot + 1, path);
        if (cli.cartridge_path[slot] ||
            cli.cartridge_mapper_set[slot]) {
            msx_destroy(&msx);
            return 1;
        }
    }
    definition = model_catalog_find(&models, config.machine_id);
    if (display_init(&display, &config, &msx,
                     definition ? definition->name : NULL) < 0) {
        display_quit(&display);
        msx_destroy(&msx);
        return 1;
    }
    audio_output_init(&audio, !cli.headless && !cli.unthrottled);
    window_id = SDL_GetWindowID(display.window);

    notify_init();
    notify_set_mode(config.notifications);
    leds_init();
    overlay_init(&overlay, &config, &models, &display, &msx);
    if (msx_can_boot(&msx))
        notify_post("Firmware running - F9 opens options");
    else
        notify_post("Select a BIOS ROM to boot - F9 opens options");

    printf("1983 - MSX / MSX2 emulator (git %s)\n",
           PROG_GIT_COMMIT);
    printf("Machine catalogue: %s\n",
           models.path[0] ? models.path : "built-in defaults");
    printf("%s, %s, %d KB RAM, %d KB VRAM, %s, %s PSG\n",
           definition ? definition->name : msx.profile->name,
           msx_region_name(msx.region),
           msx.ram_kb, msx.profile->vram_kb, msx_vdp_name(&msx),
           msx.profile->psg_variant == PSG_VARIANT_YM2149
           ? "YM2149" : "AY-3-8910");
    printf("F4 screenshot, F5 reset, F9 options, F11 fullscreen, F12 quit\n");
    printf("Shift+F1..F5 = MSX F1..F5, Shift+F7 = SELECT, "
           "Shift+F8 = STOP\n");
    if (msx_can_boot(&msx))
        printf("BIOS loaded%s%s%s%s%s\n",
               msx.logo_loaded ? ", logo ROM loaded" : "",
               msx.subrom_loaded ? ", Sub-ROM loaded" : "",
               msx.disk_rom_loaded ? ", disk ROM loaded" : "",
               msx_get_cartridge(&msx, 0)->loaded
               ? ", cartridge 1 loaded" : "",
               msx_get_cartridge(&msx, 1)->loaded
               ? ", cartridge 2 loaded" : "");
    else
        printf("No BIOS loaded; use --bios PATH (and --logo PATH for C-BIOS)\n");

    next_frame_ns = SDL_GetTicksNS();
    paced_frame_hz = msx.frame_hz;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            bool overlay_was_visible = overlay.visible;

            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
                event.window.windowID == window_id)
                kbd_release_all(&keyboard, &msx);
            if (overlay_handle_event(&overlay, &event)) {
                if (!overlay_was_visible && overlay.visible)
                    kbd_release_all(&keyboard, &msx);
                if (overlay.visible)
                    leds_set_mouse_position(0.0f, 0.0f, false);
                continue;
            }
            track_led_mouse(&display, window_id, &event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                continue;
            }
            if (event.type == SDL_EVENT_KEY_UP) {
                kbd_handle(&keyboard, &msx, &event.key);
                continue;
            }
            if (event.type != SDL_EVENT_KEY_DOWN)
                continue;

            if (kbd_handle_guest_function(&keyboard, &msx, &event.key))
                continue;

            if ((event.key.mod & SDL_KMOD_CTRL) && !display.fullscreen) {
                bool increase =
                    event.key.scancode == SDL_SCANCODE_EQUALS ||
                    event.key.scancode == SDL_SCANCODE_KP_PLUS;
                bool decrease =
                    event.key.scancode == SDL_SCANCODE_MINUS ||
                    event.key.scancode == SDL_SCANCODE_KP_MINUS;
                if (increase || decrease) {
                    int scale = display.scale + (increase ? 1 : -1);
                    if (scale < 1)
                        scale = 1;
                    if (scale > 4)
                        scale = 4;
                    display_set_scale(&display, scale);
                    config.scale = scale;
                    continue;
                }
            }

            switch (event.key.key) {
                case SDLK_F4: {
                    char path[64];
                    snprintf(path, sizeof(path), "1983-%05d.ppm",
                             host_frame);
                    if (display_save_ppm(&display, path) == 0)
                        notify_post("Screenshot saved: %s", path);
                    else
                        notify_post("Screenshot failed: %s", path);
                    break;
                }
                case SDLK_F5:
                    kbd_release_all(&keyboard, &msx);
                    msx_reset(&msx);
                    psg_set_volume(&msx.psg, config.audio_volume);
                    audio_output_clear(&audio);
                    leds_set_state(LED_CAPS, false);
                    leds_set_state(LED_KANA, false);
                    notify_post("Machine reset");
                    break;
                case SDLK_F6:
                    notify_post("Animated capture is planned");
                    break;
                case SDLK_F8:
                    notify_post("Monitor/disassembler is planned");
                    break;
                case SDLK_F11:
                    display_toggle_fullscreen(&display);
                    config.fullscreen = display.fullscreen;
                    break;
                case SDLK_F12:
                    running = false;
                    break;
                case SDLK_PAUSE:
                    msx.paused = !msx.paused;
                    if (msx.paused)
                        audio_output_clear(&audio);
                    notify_post(msx.paused ? "Paused" : "Running");
                    break;
                default:
                    kbd_handle(&keyboard, &msx, &event.key);
                    break;
            }
        }

        overlay_tick(&overlay);
        msx_run_frame(&msx);
        audio_output_submit(&audio, msx.audio_samples,
                            msx.audio_sample_count);
        leds_set_state(LED_CAPS, msx.caps_led);
        leds_set_state(LED_KANA, msx.kana_led);
        notify_tick(1000 / msx.frame_hz);
        display_draw(&display, &msx);
        draw_debug(&config, &msx, &display);
        draw_paused(&msx, &display);
        overlay_render(&overlay);
        notify_render(display.renderer, DISPLAY_SCREEN_H);
        leds_render_hover(display.renderer);
        display_present(&display);

        if (cli.exit_after >= 0 && host_frame >= cli.exit_after)
            running = false;
        if (!cli.unthrottled) {
            Uint64 frame_numerator;
            Uint64 frame_ns;
            Uint64 now;

            if (paced_frame_hz != msx.frame_hz) {
                next_frame_ns = SDL_GetTicksNS();
                frame_ns_remainder = 0;
                paced_frame_hz = msx.frame_hz;
            }
            frame_numerator = 1000000000ULL + frame_ns_remainder;
            frame_ns = frame_numerator / (unsigned)msx.frame_hz;
            frame_ns_remainder =
                (unsigned)(frame_numerator % (unsigned)msx.frame_hz);
            next_frame_ns += frame_ns;
            now = SDL_GetTicksNS();
            if (now < next_frame_ns)
                SDL_DelayNS(next_frame_ns - now);
            else if (now > next_frame_ns + frame_ns * 3) {
                /*
                 * Preserve cadence across an occasional late frame, but do
                 * not try to catch up after a debugger stop or long host
                 * stall.
                 */
                next_frame_ns = now;
                frame_ns_remainder = 0;
            }
        }
        ++host_frame;
    }

    config.fullscreen = display.fullscreen;
    config.scale = display.scale;
    config_save(&config);
    if (cli.dump_state) {
        size_t nonzero_vram = 0;
        for (size_t i = 0; i < sizeof(msx.vdp.vram); ++i)
            if (msx.vdp.vram[i])
                ++nonzero_vram;
        printf("state frame=%llu pc=%04X sp=%04X slot=%02X "
               "subslot=%02X mapper=%02X,%02X,%02X,%02X "
               "cycles=%llu instructions=%llu vram_nonzero=%zu "
               "vdp_r0=%02X vdp_r1=%02X\n",
               (unsigned long long)msx.frame, msx.cpu.pc, msx.cpu.sp,
               msx.primary_slot, msx.secondary_slot[3],
               msx.mapper_segment[0], msx.mapper_segment[1],
               msx.mapper_segment[2], msx.mapper_segment[3],
               (unsigned long long)msx.cycles,
               (unsigned long long)msx.instructions, nonzero_vram,
               msx.vdp.registers[0], msx.vdp.registers[1]);
    }
    audio_output_quit(&audio);
    display_quit(&display);
    msx_destroy(&msx);
    return 0;
}
