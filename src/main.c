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
#include "msx.h"
#include "notify.h"
#include "overlay.h"
#include "ui.h"

#ifndef PROG_GIT_COMMIT
#define PROG_GIT_COMMIT "unknown"
#endif

typedef struct {
    const char *config_path;
    const char *bios_path;
    const char *logo_path;
    const char *cartridge_path;
    int model;
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
    "  --model msx1|msx2   override the configured generic machine\n"
    "  --region pal|ntsc   override the configured video standard\n"
    "  --bios PATH         load a 32 KB MSX BIOS ROM\n"
    "  --logo PATH         load a 16 KB C-BIOS logo ROM in slot 0/page 2\n"
    "  --cart PATH         load a plain cartridge ROM in primary slot 1\n"
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

static int parse_model(const char *text) {
    if (strcmp(text, "msx1") == 0)
        return MSX_MODEL_GENERIC_MSX1;
    if (strcmp(text, "msx2") == 0)
        return MSX_MODEL_GENERIC_MSX2;
    fprintf(stderr, "--model: expected msx1 or msx2\n");
    return -1;
}

static int parse_region(const char *text) {
    if (strcmp(text, "pal") == 0)
        return MSX_REGION_PAL;
    if (strcmp(text, "ntsc") == 0)
        return MSX_REGION_NTSC;
    fprintf(stderr, "--region: expected pal or ntsc\n");
    return -1;
}

static int parse_cli(int argc, char **argv, Cli *cli) {
    memset(cli, 0, sizeof(*cli));
    cli->model = -1;
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
             strcmp(argument, "--model") == 0 ||
             strcmp(argument, "--region") == 0 ||
             strcmp(argument, "--bios") == 0 ||
             strcmp(argument, "--logo") == 0 ||
             strcmp(argument, "--cart") == 0 ||
             strcmp(argument, "--scale") == 0 ||
             strcmp(argument, "--exit-after") == 0) &&
            i + 1 >= argc) {
            fprintf(stderr, "%s requires a value\n", argument);
            return -1;
        }
        if (strcmp(argument, "--config") == 0) {
            cli->config_path = argv[++i];
        } else if (strcmp(argument, "--bios") == 0) {
            cli->bios_path = argv[++i];
        } else if (strcmp(argument, "--logo") == 0) {
            cli->logo_path = argv[++i];
        } else if (strcmp(argument, "--cart") == 0) {
            cli->cartridge_path = argv[++i];
        } else if (strcmp(argument, "--model") == 0) {
            cli->model = parse_model(argv[++i]);
            if (cli->model < 0)
                return -1;
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
    char text[128];

    if (!config->debug)
        return;
    snprintf(text, sizeof(text),
             "DBG frame=%llu PC=%04X slot=%02X cycles=%llu",
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
    MsxMachine msx;
    KbdHost keyboard;
    static Display display;
    AudioOutput audio;
    Overlay overlay;
    SDL_WindowID window_id;
    bool running = true;
    int host_frame = 0;
    Uint64 next_tick;
    int cli_result = parse_cli(argc, argv, &cli);

    if (cli_result != 0)
        return cli_result > 0 ? 0 : 1;

    config_load(&config, cli.config_path);
    if (cli.model >= 0) {
        config.model = (MsxModel)cli.model;
        config.memory_kb = msx_default_ram_kb(config.model);
    }
    if (cli.region >= 0)
        config.region = (MsxRegion)cli.region;
    if (cli.scale >= 0)
        config.scale = cli.scale;
    config_normalize(&config);

    if (cli.headless) {
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen",
                                SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy",
                                SDL_HINT_OVERRIDE);
    }

    msx_init(&msx, config.model, config.region, config.memory_kb);
    kbd_init(&keyboard);
    if (cli.bios_path && msx_load_bios(&msx, cli.bios_path) < 0) {
        fprintf(stderr, "cannot load 32 KB BIOS ROM: %s\n", cli.bios_path);
        return 1;
    }
    if (cli.logo_path && msx_load_logo(&msx, cli.logo_path) < 0) {
        fprintf(stderr, "cannot load 16 KB logo ROM: %s\n", cli.logo_path);
        return 1;
    }
    if (cli.cartridge_path &&
        msx_load_cartridge(&msx, cli.cartridge_path) < 0) {
        fprintf(stderr, "cannot load cartridge ROM: %s\n",
                cli.cartridge_path);
        return 1;
    }
    if (display_init(&display, &config, &msx) < 0) {
        display_quit(&display);
        return 1;
    }
    audio_output_init(&audio, !cli.headless && !cli.unthrottled);
    window_id = SDL_GetWindowID(display.window);

    notify_init();
    notify_set_mode(config.notifications);
    leds_init();
    overlay_init(&overlay, &config, &display, &msx);
    if (msx_can_boot(&msx))
        notify_post("Firmware running - F9 opens options");
    else
        notify_post("Select a BIOS ROM to boot - F9 opens options");

    printf("1983 - MSX / MSX2 emulator (git %s)\n",
           PROG_GIT_COMMIT);
    printf("%s, %s, %d KB RAM, %d KB VRAM, %s, %s PSG\n",
           msx.profile->name, msx_region_name(msx.region),
           msx.ram_kb, msx.profile->vram_kb, msx_vdp_name(&msx),
           msx.profile->psg_variant == PSG_VARIANT_YM2149
           ? "YM2149" : "AY-3-8910");
    printf("F4 screenshot, F5 reset, F9 options, F11 fullscreen, F12 quit\n");
    printf("Shift+F1..F5 = MSX F1..F5, Shift+F7 = SELECT, "
           "Shift+F8 = STOP\n");
    if (msx_can_boot(&msx))
        printf("BIOS loaded%s%s\n",
               msx.logo_loaded ? ", logo ROM loaded" : "",
               msx.cartridge_loaded ? ", cartridge loaded" : "");
    else
        printf("No BIOS loaded; use --bios PATH (and --logo PATH for C-BIOS)\n");

    next_tick = SDL_GetTicks();
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
            Uint64 frame_ms = (Uint64)(1000 / msx.frame_hz);
            Uint64 now;
            next_tick += frame_ms;
            now = SDL_GetTicks();
            if (now < next_tick)
                SDL_Delay((Uint32)(next_tick - now));
            else
                next_tick = now;
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
               "cycles=%llu instructions=%llu vram_nonzero=%zu "
               "vdp_r0=%02X vdp_r1=%02X\n",
               (unsigned long long)msx.frame, msx.cpu.pc, msx.cpu.sp,
               msx.primary_slot, (unsigned long long)msx.cycles,
               (unsigned long long)msx.instructions, nonzero_vram,
               msx.vdp.registers[0], msx.vdp.registers[1]);
    }
    audio_output_quit(&audio);
    display_quit(&display);
    return 0;
}
