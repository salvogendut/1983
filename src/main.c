#include <SDL3/SDL.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "config.h"
#include "display.h"
#include "gamepad.h"
#include "gifcap.h"
#include "ffmpeg_gif.h"
#include "kbd.h"
#include "leds.h"
#include "models.h"
#include "msx.h"
#include "notify.h"
#include "overlay.h"
#include "paste.h"
#include "rs232_dev.h"
#include "shutter_wav.h"
#include "ui.h"
#include "unapinet.h"

#ifndef PROG_GIT_COMMIT
#define PROG_GIT_COMMIT "unknown"
#endif

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

typedef struct {
    const char *config_path;
    const char *models_path;
    const char *bios_path;
    const char *logo_path;
    const char *subrom_path;
    const char *disk_rom_path;
    const char *sunrise_rom_path;
    const char *sd_mapper_rom_path;
    const char *sd_card_path[MSX_SD_MAPPER_CARDS];
    const char *megaflash_rom_path;
    const char *rs232_rom_path;
    const char *cdx2_rom_path;
    const char *megaflash_card_path[MSX_MEGAFLASH_CARDS];
    const char *drive_a_path;
    const char *drive_b_path;
    const char *ide_image_path;
    const char *cassette_path;
    const char *screenshot_path;
    const char *gif_out;
    const char *cartridge_path[MSX_CARTRIDGE_SLOTS];
    MsxCartridgeMapper cartridge_mapper[MSX_CARTRIDGE_SLOTS];
    bool cartridge_mapper_set[MSX_CARTRIDGE_SLOTS];
    const char *model_name;
    int region;
    int floppy_image_mode;
    int ide_image_mode;
    int sd_image_mode;
    int tcpip_unapi;
    int rs232;
    int cdx2;
    int scale;
    int exit_after;
    const char *paste_text;
    int paste_at;
    int paste_repeat;
    const char *dump_ram_spec;
    bool headless;
    bool unthrottled;
    bool dump_state;
} Cli;

typedef struct {
    GifCap  *encoder;
    uint64_t interval_ns;
    uint64_t elapsed_ns;
    bool     first_frame;
    bool     optimize;
    char    *path;
} GifCapture;

static bool gif_capture_width_supported(int width) {
    return width == 720 || width == 540 || width == 360 ||
           width == 240 || width == 180;
}

static bool gif_capture_fps_supported(int fps) {
    return fps == 25 || fps == 20 || fps == 10 || fps == 5;
}

/* The MsxMachine exposes a single optional-I/O slot; route both the UNAPI
 * bridge and the RS-232C interface through one dispatcher. */
static UnapiNet *g_unapinet;
static Rs232Device *g_rs232dev;

static bool g_io_read(void *context, u16 port, u8 *value) {
    (void)context;
    if (g_rs232dev && rs232dev_io_read(g_rs232dev, port, value))
        return true;
    if (g_unapinet && unapinet_io_read(g_unapinet, port, value))
        return true;
    return false;
}
static bool g_io_write(void *context, u16 port, u8 value) {
    (void)context;
    if (g_rs232dev && rs232dev_io_write(g_rs232dev, port, value))
        return true;
    if (g_unapinet && unapinet_io_write(g_unapinet, port, value))
        return true;
    return false;
}
static void g_io_reset(void *context) {
    (void)context;
    if (g_rs232dev) rs232dev_io_reset(g_rs232dev);
    if (g_unapinet) unapinet_io_reset(g_unapinet);
}
static void g_io_advance(void *context, unsigned cycles) {
    (void)context;
    if (g_rs232dev) rs232dev_io_advance(g_rs232dev, cycles);
}

static bool gif_capture_start(GifCapture *capture, const char *path,
                              const Config *cfg) {
    if (!capture || !path || !path[0] || capture->encoder)
        return false;

    int width = gif_capture_width_supported(cfg->gif_width)
              ? cfg->gif_width : GIF_CAPTURE_WIDTH_DEFAULT;
    int fps = gif_capture_fps_supported(cfg->gif_fps)
            ? cfg->gif_fps : GIF_CAPTURE_FPS_DEFAULT;
    int height = (width * 3) / 4;

    capture->encoder = gifcap_open(path, DISPLAY_FB_W, DISPLAY_FB_H,
                                   width, height, 100 / fps);
    if (!capture->encoder)
        return false;

    capture->optimize = cfg->gif_ffmpeg && FFMPEG_GIF_SUPPORTED;
    if (capture->optimize) {
        size_t path_len = strlen(path) + 1;
        capture->path = malloc(path_len);
        if (capture->path)
            memcpy(capture->path, path, path_len);
        else
            capture->optimize = false;
    }
    capture->interval_ns = 1000000000ULL / (uint64_t)fps;
    capture->elapsed_ns = 0;
    capture->first_frame = true;
    fprintf(stderr, "[videocap] recording to %s\n", path);
    return true;
}

static void gif_capture_stop(GifCapture *capture) {
    if (!capture || !capture->encoder)
        return;

    int frames = gifcap_frame_count(capture->encoder);
    gifcap_close(capture->encoder);
    capture->encoder = NULL;
    fprintf(stderr, "[videocap] GIF stopped (%d frames)\n", frames);
    if (capture->optimize && capture->path)
        ffmpeg_gif_optimize(capture->path);

    free(capture->path);
    capture->path = NULL;
    capture->optimize = false;
    capture->interval_ns = 0;
    capture->elapsed_ns = 0;
    capture->first_frame = false;
}

static void gif_capture_frame(GifCapture *capture, const u32 *pixels,
                              uint64_t emulated_frame_ns) {
    if (!capture || !capture->encoder)
        return;

    bool due = capture->first_frame;
    capture->first_frame = false;
    if (!due) {
        capture->elapsed_ns += emulated_frame_ns;
        if (capture->elapsed_ns >= capture->interval_ns) {
            capture->elapsed_ns %= capture->interval_ns;
            due = true;
        }
    }
    if (due && !gifcap_frame(capture->encoder, pixels))
        gif_capture_stop(capture);
}

static void gif_capture_toggle(GifCapture *capture, const Config *cfg) {
    if (capture->encoder) {
        gif_capture_stop(capture);
    } else {
        char path[256];
        time_t t = time(NULL);
        struct tm *lt = localtime(&t);
        if (lt)
            strftime(path, sizeof(path), "1983-%Y%m%d-%H%M%S.gif", lt);
        else
            snprintf(path, sizeof(path), "1983-capture.gif");
        if (!gif_capture_start(capture, path, cfg))
            fprintf(stderr,
                    "[videocap] GIF open failed for '%s'\n", path);
    }
}

static const char *usage =
    "Usage: 1983 [options]\n"
    "  --config PATH       use an alternative configuration file\n"
    "  --models PATH       use an alternative machine catalogue\n"
    "  --model NAME        select a model ID from the machine catalogue\n"
    "  --region pal|ntsc   override the configured video standard\n"
    "  --bios PATH         load a 32 KB MSX BIOS ROM\n"
    "  --logo PATH         load a 16 KB C-BIOS logo ROM in slot 0/page 2\n"
    "  --subrom PATH       load a 16 KB MSX2 Sub-ROM in slot 3-0\n"
    "  --disk-rom PATH     load a 16 KB floppy-controller disk ROM\n"
    "  --sunrise-rom PATH  load a 128 KB Sunrise IDE/Nextor kernel ROM\n"
    "  --sd-mapper-rom PATH load a 128/256 KB MSX SD Mapper V2 ROM\n"
    "  --sd-a PATH          insert a raw image in SD Mapper card A\n"
    "  --sd-b PATH          insert a raw image in SD Mapper card B\n"
    "  --megaflash-rom PATH load a MegaFlashROM image (max 8 MiB)\n"
    "  --megaflash-sd-a PATH insert its first raw SD-card image\n"
    "  --megaflash-sd-b PATH insert its second raw SD-card image\n"
    "  --unapi             enable the MSX TCP/IP UNAPI host bridge\n"
    "  --no-unapi          disable the MSX TCP/IP UNAPI host bridge\n"
    "  --rs232             enable the MSX RS-232C serial interface\n"
    "  --no-rs232          disable the MSX RS-232C serial interface\n"
    "  --rs232-rom PATH    load a user-provided RS-232C EXTBIO/driver ROM\n"
    "  --cdx2              enable the Microsol CDX-2 port-based FDC\n"
    "  --no-cdx2           disable the Microsol CDX-2 port-based FDC\n"
    "  --cdx2-rom PATH     load a user-provided 16 KB CDX-2 ROM\n"
    "  --sd-mode MODE       SD access: read-only (default) or read-write\n"
    "  --disk-a PATH       insert a raw MSX or CPCEMU DSK in Drive A\n"
    "  --disk-b PATH       insert a raw MSX or CPCEMU DSK in Drive B\n"
    "  --floppy-mode MODE  DSK access: read-only (default) or read-write\n"
    "  --ide PATH          mount a raw IDE disk image\n"
    "  --ide-mode MODE     image access: read-only (default) or read-write\n"
    "  --cassette PATH     insert a standard MSX CAS cassette image\n"
    "  --screenshot PATH   save the final rendered frame as a PPM file\n"
    "  --cart PATH         alias for --cart1\n"
    "  --cart1 PATH        load a cartridge ROM in primary slot 1\n"
    "  --cart2 PATH        load a cartridge ROM in primary slot 2\n"
    "  --mapper NAME       alias for --mapper1\n"
    "  --mapper1 NAME      slot 1 mapper: auto, linear, ascii8, ascii16,\n"
    "                      konami, or konami-scc\n"
    "  --mapper2 NAME      slot 2 mapper (same names as --mapper1)\n"
    "  --scale N           initial window scale (1 through 4)\n"
    "  --headless          use SDL's headless video backend\n"
    "  --exit-after N      exit after N host frames (for smoke tests)\n"
    "  --paste-text TEXT   queue TEXT for the paste queue (for smoke tests)\n"
    "  --paste-at N        start --paste-text at host frame N (default 60)\n"
    "  --paste-repeat N    requeue --paste-text every N frames (default 0)\n"
    "  --dump-ram A:N      print N guest RAM bytes from address A on exit\n"
    "  --gif-out PATH       record a GIF to PATH using the capture profile\n"
    "  --unthrottled       disable 50/60 Hz frame pacing\n"
    "  --dump-state        print CPU/bus/VDP state on exit\n"
    "  -h, --help          show this help\n"
    "  --version           show version information\n";

static void startup_info(NotifyMode mode, const char *format, ...) {
    va_list args;

    if (mode == NOTIFY_MODE_OFF)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

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

static int parse_ide_mode(const char *text) {
    if (strcmp(text, "read-only") == 0 ||
        strcmp(text, "ro") == 0)
        return ATA_IMAGE_READ_ONLY;
    if (strcmp(text, "read-write") == 0 ||
        strcmp(text, "rw") == 0)
        return ATA_IMAGE_READ_WRITE;
    fprintf(stderr,
            "--ide-mode: expected read-only or read-write\n");
    return -1;
}

static int parse_floppy_mode(const char *text) {
    if (strcmp(text, "read-only") == 0 ||
        strcmp(text, "ro") == 0)
        return FLOPPY_IMAGE_READ_ONLY;
    if (strcmp(text, "read-write") == 0 ||
        strcmp(text, "rw") == 0)
        return FLOPPY_IMAGE_READ_WRITE;
    fprintf(stderr,
            "--floppy-mode: expected read-only or read-write\n");
    return -1;
}

static int parse_sd_mode(const char *text) {
    if (strcmp(text, "read-only") == 0 ||
        strcmp(text, "ro") == 0)
        return SD_IMAGE_READ_ONLY;
    if (strcmp(text, "read-write") == 0 ||
        strcmp(text, "rw") == 0)
        return SD_IMAGE_READ_WRITE;
    fprintf(stderr,
            "--sd-mode: expected read-only or read-write\n");
    return -1;
}

static int parse_cli(int argc, char **argv, Cli *cli) {
    memset(cli, 0, sizeof(*cli));
    cli->region = -1;
    cli->floppy_image_mode = -1;
    cli->ide_image_mode = -1;
    cli->sd_image_mode = -1;
    cli->tcpip_unapi = -1;
    cli->rs232 = -1;
    cli->cdx2 = -1;
    cli->scale = -1;
    cli->exit_after = -1;
    cli->paste_at = 60;

    for (int i = 1; i < argc; ++i) {
        const char *argument = argv[i];
        if (strcmp(argument, "-h") == 0 ||
            strcmp(argument, "--help") == 0) {
            fputs(usage, stdout);
            return 1;
        }
        if (strcmp(argument, "--version") == 0) {
            printf("1983 %s (git %s)\n",
                   PACKAGE_VERSION, PROG_GIT_COMMIT);
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
        if (strcmp(argument, "--unapi") == 0) {
            cli->tcpip_unapi = 1;
            continue;
        }
        if (strcmp(argument, "--no-unapi") == 0) {
            cli->tcpip_unapi = 0;
            continue;
        }
        if (strcmp(argument, "--rs232") == 0) {
            cli->rs232 = 1;
            continue;
        }
        if (strcmp(argument, "--no-rs232") == 0) {
            cli->rs232 = 0;
            continue;
        }
        if (strcmp(argument, "--cdx2") == 0) {
            cli->cdx2 = 1;
            continue;
        }
        if (strcmp(argument, "--no-cdx2") == 0) {
            cli->cdx2 = 0;
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
             strcmp(argument, "--sunrise-rom") == 0 ||
             strcmp(argument, "--sd-mapper-rom") == 0 ||
             strcmp(argument, "--sd-a") == 0 ||
             strcmp(argument, "--sd-b") == 0 ||
             strcmp(argument, "--megaflash-rom") == 0 ||
             strcmp(argument, "--megaflash-sd-a") == 0 ||
             strcmp(argument, "--megaflash-sd-b") == 0 ||
             strcmp(argument, "--rs232-rom") == 0 ||
             strcmp(argument, "--cdx2-rom") == 0 ||
             strcmp(argument, "--sd-mode") == 0 ||
             strcmp(argument, "--disk-a") == 0 ||
             strcmp(argument, "--disk-b") == 0 ||
             strcmp(argument, "--floppy-mode") == 0 ||
             strcmp(argument, "--ide") == 0 ||
             strcmp(argument, "--ide-mode") == 0 ||
             strcmp(argument, "--cassette") == 0 ||
             strcmp(argument, "--screenshot") == 0 ||
             strcmp(argument, "--gif-out") == 0 ||
             strcmp(argument, "--paste-text") == 0 ||
             strcmp(argument, "--paste-at") == 0 ||
             strcmp(argument, "--paste-repeat") == 0 ||
             strcmp(argument, "--dump-ram") == 0 ||
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
        } else if (strcmp(argument, "--sunrise-rom") == 0) {
            cli->sunrise_rom_path = argv[++i];
        } else if (strcmp(argument, "--sd-mapper-rom") == 0) {
            cli->sd_mapper_rom_path = argv[++i];
        } else if (strcmp(argument, "--sd-a") == 0) {
            cli->sd_card_path[0] = argv[++i];
        } else if (strcmp(argument, "--sd-b") == 0) {
            cli->sd_card_path[1] = argv[++i];
        } else if (strcmp(argument, "--megaflash-rom") == 0) {
            cli->megaflash_rom_path = argv[++i];
        } else if (strcmp(argument, "--megaflash-sd-a") == 0) {
            cli->megaflash_card_path[0] = argv[++i];
        } else if (strcmp(argument, "--megaflash-sd-b") == 0) {
            cli->megaflash_card_path[1] = argv[++i];
        } else if (strcmp(argument, "--rs232-rom") == 0) {
            cli->rs232_rom_path = argv[++i];
        } else if (strcmp(argument, "--cdx2-rom") == 0) {
            cli->cdx2_rom_path = argv[++i];
        } else if (strcmp(argument, "--sd-mode") == 0) {
            cli->sd_image_mode = parse_sd_mode(argv[++i]);
            if (cli->sd_image_mode < 0)
                return -1;
        } else if (strcmp(argument, "--disk-a") == 0) {
            cli->drive_a_path = argv[++i];
        } else if (strcmp(argument, "--disk-b") == 0) {
            cli->drive_b_path = argv[++i];
        } else if (strcmp(argument, "--floppy-mode") == 0) {
            cli->floppy_image_mode = parse_floppy_mode(argv[++i]);
            if (cli->floppy_image_mode < 0)
                return -1;
        } else if (strcmp(argument, "--ide") == 0) {
            cli->ide_image_path = argv[++i];
        } else if (strcmp(argument, "--ide-mode") == 0) {
            cli->ide_image_mode = parse_ide_mode(argv[++i]);
            if (cli->ide_image_mode < 0)
                return -1;
        } else if (strcmp(argument, "--cassette") == 0) {
            cli->cassette_path = argv[++i];
        } else if (strcmp(argument, "--screenshot") == 0) {
            cli->screenshot_path = argv[++i];
        } else if (strcmp(argument, "--gif-out") == 0) {
            cli->gif_out = argv[++i];
        } else if (strcmp(argument, "--paste-text") == 0) {
            cli->paste_text = argv[++i];
        } else if (strcmp(argument, "--paste-at") == 0) {
            cli->paste_at = parse_integer(argv[++i], 0, 1000000, "--paste-at");
            if (cli->paste_at < 0)
                return -1;
        } else if (strcmp(argument, "--paste-repeat") == 0) {
            cli->paste_repeat =
                parse_integer(argv[++i], 0, 1000000, "--paste-repeat");
            if (cli->paste_repeat < 0)
                return -1;
        } else if (strcmp(argument, "--dump-ram") == 0) {
            cli->dump_ram_spec = argv[++i];
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

static unsigned main_input_port(const Config *config) {
    return config->main_input == INPUT_PORT_B ? 1u : 0u;
}

static bool mouse_input_enabled(const Config *config) {
    unsigned port = main_input_port(config);

    return config->joy_port_device[port] == JOY_PORT_MOUSE;
}

static void sync_mouse_ports(MsxMachine *msx, const Config *config) {
    for (unsigned port = 0; port < MSX_JOYSTICK_PORTS; ++port)
        msx_mouse_set_enabled(
            msx, port,
            config->joy_port_device[port] == JOY_PORT_MOUSE);
}

static void clear_mouse_input(MsxMachine *msx) {
    for (unsigned port = 0; port < MSX_JOYSTICK_PORTS; ++port)
        msx_mouse_clear_input(msx, port);
}

static void set_mouse_capture(Display *display, MsxMachine *msx,
                              bool captured) {
    if (!display_set_mouse_capture(display, captured))
        return;
    if (!captured)
        clear_mouse_input(msx);
}

static void reset_machine(MsxMachine *msx, const Config *config,
                          Paste *paste, KbdHost *keyboard,
                          Display *display, AudioOutput *audio) {
    paste_cancel(paste, msx);
    kbd_release_all(keyboard, msx);
    set_mouse_capture(display, msx, false);
    msx_reset(msx);
    psg_set_volume(&msx->psg, config->audio_volume);
    audio_output_clear(audio);
    leds_set_state(LED_CAPS, false);
    leds_set_state(LED_KANA, false);
}

static void track_led_mouse(Display *display, SDL_WindowID window_id,
                            const SDL_Event *event) {
    float x;
    float y;

    if (display->mouse_captured) {
        if (event->type == SDL_EVENT_MOUSE_MOTION ||
            event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
            event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
            leds_set_mouse_position(0.0f, 0.0f, false);
        return;
    }
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

static void recover_pending_megaflash_state(const Config *config) {
    Config state_config = *config;
    char state_path[PATH_MAX];
    char pending_path[PATH_MAX];
    FILE *pending;

    state_config.megaflash = true;
    if (config_megaflash_state_path(
            &state_config, state_path, sizeof(state_path)) != 0 ||
        config_megaflash_pending_state_path(
            config, pending_path, sizeof(pending_path)) != 0 ||
        !state_path[0] || !pending_path[0])
        return;
    pending = fopen(pending_path, "rb");
    if (!pending)
        return;
    if (fclose(pending) != 0 ||
        msx_commit_megaflash_state(
            NULL, pending_path, state_path) != 0)
        fprintf(stderr,
                "warning: cannot recover pending MegaFlash state\n");
}

int main(int argc, char **argv) {
    Cli cli;
    Config config;
    ModelCatalog models;
    const ModelDefinition *definition;
    MsxMachine msx;
    KbdHost keyboard;
    Paste paste;
    static Display display;
    AudioOutput audio;
    SDL_AudioStream *sfx_stream;
    Uint8  *sfx_buf;
    Uint32  sfx_buf_len;
    GamepadInput gamepad;
    Overlay overlay;
    GifCapture capture;
    UnapiNet *unapinet = NULL;
    Rs232Device *rs232dev = NULL;
    char rtc_path[PATH_MAX];
    char megaflash_state_path[PATH_MAX];
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
    recover_pending_megaflash_state(&config);
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
        config.floppy = definition->floppy;
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
        config.floppy = definition->floppy;
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
    if (cli.sunrise_rom_path) {
        snprintf(config.sunrise_rom_path,
                 sizeof(config.sunrise_rom_path),
                 "%s", cli.sunrise_rom_path);
        config.extra_hardware = true;
        config.sunrise_ide = true;
    }
    if (cli.sd_mapper_rom_path) {
        snprintf(config.sd_mapper_rom_path,
                 sizeof(config.sd_mapper_rom_path),
                 "%s", cli.sd_mapper_rom_path);
        config.extra_hardware = true;
        config.sd_mapper = true;
    }
    for (unsigned card = 0; card < MSX_SD_MAPPER_CARDS; ++card) {
        if (!cli.sd_card_path[card])
            continue;
        snprintf(config.sd_card_path[card],
                 sizeof(config.sd_card_path[card]),
                 "%s", cli.sd_card_path[card]);
        config.extra_hardware = true;
        config.sd_mapper = true;
    }
    if (cli.megaflash_rom_path) {
        snprintf(config.megaflash_rom_path,
                 sizeof(config.megaflash_rom_path),
                 "%s", cli.megaflash_rom_path);
        config.extra_hardware = true;
        config.megaflash = true;
    }
    if (cli.rs232_rom_path) {
        snprintf(config.rs232_rom_path,
                 sizeof(config.rs232_rom_path),
                 "%s", cli.rs232_rom_path);
        config.extra_hardware = true;
        config.rs232 = true;
    }
    if (cli.cdx2_rom_path) {
        snprintf(config.cdx2_rom_path,
                 sizeof(config.cdx2_rom_path),
                 "%s", cli.cdx2_rom_path);
        config.extra_hardware = true;
        config.cdx2 = true;
    }
    for (unsigned card = 0; card < MSX_MEGAFLASH_CARDS; ++card) {
        if (!cli.megaflash_card_path[card])
            continue;
        snprintf(config.megaflash_card_path[card],
                 sizeof(config.megaflash_card_path[card]),
                 "%s", cli.megaflash_card_path[card]);
        config.extra_hardware = true;
        config.megaflash = true;
    }
    if (cli.ide_image_path) {
        snprintf(config.ide_image_path,
                 sizeof(config.ide_image_path),
                 "%s", cli.ide_image_path);
        config.extra_hardware = true;
        config.sunrise_ide = true;
    }
    if (cli.drive_a_path)
        snprintf(config.drive_a_path,
                 sizeof(config.drive_a_path),
                 "%s", cli.drive_a_path);
    if (cli.drive_b_path) {
        snprintf(config.drive_b_path,
                 sizeof(config.drive_b_path),
                 "%s", cli.drive_b_path);
        config.second_drive = true;
    }
    if (cli.floppy_image_mode >= 0)
        config.floppy_image_mode =
            (FloppyImageMode)cli.floppy_image_mode;
    if (cli.ide_image_mode >= 0)
        config.ide_image_mode =
            (AtaImageMode)cli.ide_image_mode;
    if (cli.sd_image_mode >= 0)
        config.sd_image_mode =
            (SdImageMode)cli.sd_image_mode;
    if (cli.tcpip_unapi >= 0) {
        config.tcpip_unapi = cli.tcpip_unapi != 0;
        if (config.tcpip_unapi)
            config.extra_hardware = true;
    }
    if (cli.rs232 >= 0) {
        config.rs232 = cli.rs232 != 0;
        if (config.rs232)
            config.extra_hardware = true;
    }
    if (cli.cdx2 >= 0) {
        config.cdx2 = cli.cdx2 != 0;
        if (config.cdx2)
            config.extra_hardware = true;
    }
    if (cli.cassette_path)
        snprintf(config.cassette_path,
                 sizeof(config.cassette_path),
                 "%s", cli.cassette_path);
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
#ifdef __APPLE__
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy",
                                SDL_HINT_OVERRIDE);
#else
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen",
                                SDL_HINT_OVERRIDE);
#endif
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy",
                                SDL_HINT_OVERRIDE);
    }

    msx_init(&msx, config.model, config.region, config.memory_kb);
    if (msx_configure_floppy(&msx, &config.floppy) != 0) {
        fprintf(stderr,
                "cannot configure floppy controller for %s\n",
                config.machine_id);
        msx_destroy(&msx);
        return 1;
    }
    if (config_rtc_path(&config, rtc_path, sizeof(rtc_path)) != 0) {
        fprintf(stderr, "warning: RTC persistence path is too long\n");
    } else if (rtc_path[0] &&
               msx_set_rtc_persistence(
                   &msx, rtc_path, rtc_host_seconds()) != 0) {
        fprintf(stderr, "warning: cannot load RTC CMOS: %s\n",
                msx_rtc_persistence_error(&msx));
    }
    sync_mouse_ports(&msx, &config);
    kbd_init(&keyboard);
    paste_init(&paste);
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
    if (config.cdx2) {
        int cdx2_slot = -1;

        for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
            const char *owner =
                config_cartridge_slot_owner(&config, slot);

            if (owner && strcmp(owner, "CDX-2 FDC") == 0) {
                cdx2_slot = (int)slot;
                break;
            }
        }
        if (cdx2_slot < 0 || !config.cdx2_rom_path[0] ||
            msx_load_cdx2(
                &msx, (unsigned)cdx2_slot,
                config.cdx2_rom_path) != 0) {
            fprintf(stderr,
                    "cannot load 16 KB Microsol CDX-2 ROM: %s\n",
                    config.cdx2_rom_path[0]
                    ? config.cdx2_rom_path : "[not configured]");
            if (cli.cdx2 > 0 || cli.cdx2_rom_path) {
                msx_destroy(&msx);
                return 1;
            }
            config.cdx2 = false;
            config_normalize(&config);
        }
    }
    if (config.cassette_path[0] &&
        msx_load_cassette(&msx, config.cassette_path) != 0) {
        fprintf(stderr, "cannot load MSX CAS cassette image: %s\n",
                config.cassette_path);
        if (cli.cassette_path) {
            msx_destroy(&msx);
            return 1;
        }
    }
    if (config.drive_a_path[0]) {
        if (!msx_floppy_supported(&msx) ||
            msx_mount_drive_a(
                &msx, config.drive_a_path,
                config.floppy_image_mode) != 0) {
            fprintf(stderr,
                    "cannot mount Floppy A DSK image %s (%s): %s\n",
                    config.drive_a_path,
                    config.floppy_image_mode ==
                        FLOPPY_IMAGE_READ_WRITE
                    ? "read/write" : "read-only",
                    msx_floppy_supported(&msx)
                    ? msx_drive_a_error(&msx)
                    : "selected machine has no floppy controller");
            if (cli.drive_a_path) {
                msx_destroy(&msx);
                return 1;
            }
        }
    }
    if (config.second_drive && config.drive_b_path[0]) {
        if (!msx_floppy_supported(&msx) ||
            msx_mount_drive_b(
                &msx, config.drive_b_path,
                config.floppy_image_mode) != 0) {
            fprintf(stderr,
                    "cannot mount Floppy B DSK image %s (%s): %s\n",
                    config.drive_b_path,
                    config.floppy_image_mode ==
                        FLOPPY_IMAGE_READ_WRITE
                    ? "read/write" : "read-only",
                    msx_floppy_supported(&msx)
                    ? msx_drive_b_error(&msx)
                    : "selected machine has no floppy controller");
            if (cli.drive_b_path) {
                msx_destroy(&msx);
                return 1;
            }
        }
    }
    if (config.sunrise_ide) {
        int sunrise_slot = -1;

        for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
            const char *owner =
                config_cartridge_slot_owner(&config, slot);

            if (owner && strcmp(owner, "Sunrise IDE") == 0) {
                sunrise_slot = (int)slot;
                break;
            }
        }
        if (sunrise_slot < 0 || !config.sunrise_rom_path[0] ||
            msx_load_sunrise_ide(
                &msx, (unsigned)sunrise_slot,
                config.sunrise_rom_path) != 0) {
            fprintf(stderr,
                    "cannot load 128 KB Sunrise IDE ROM: %s\n",
                    config.sunrise_rom_path[0]
                    ? config.sunrise_rom_path : "[not configured]");
            if (cli.sunrise_rom_path || cli.ide_image_path) {
                msx_destroy(&msx);
                return 1;
            }
            config.sunrise_ide = false;
            config_normalize(&config);
        } else if (config.ide_image_path[0] &&
                   msx_mount_sunrise_disk_mode(
                       &msx, config.ide_image_path,
                       config.ide_image_mode) != 0) {
            fprintf(stderr,
                    "cannot mount raw IDE image %s: %s (%s)\n",
                    config.ide_image_mode == ATA_IMAGE_READ_WRITE
                    ? "read/write" : "read-only",
                    config.ide_image_path,
                    msx_sunrise_disk_error(&msx));
            if (cli.ide_image_path) {
                msx_destroy(&msx);
                return 1;
            }
        }
    }
    if (config.sd_mapper) {
        int mapper_slot = -1;

        for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
            const char *owner =
                config_cartridge_slot_owner(&config, slot);

            if (owner && strcmp(owner, "SD Mapper V2") == 0) {
                mapper_slot = (int)slot;
                break;
            }
        }
        if (mapper_slot < 0 || !config.sd_mapper_rom_path[0] ||
            msx_load_sd_mapper(
                &msx, (unsigned)mapper_slot,
                config.sd_mapper_rom_path) != 0) {
            fprintf(stderr,
                    "cannot load 128/256 KB SD Mapper V2 ROM: %s\n",
                    config.sd_mapper_rom_path[0]
                    ? config.sd_mapper_rom_path : "[not configured]");
            if (cli.sd_mapper_rom_path ||
                cli.sd_card_path[0] || cli.sd_card_path[1]) {
                msx_destroy(&msx);
                return 1;
            }
            config.sd_mapper = false;
            config_normalize(&config);
        } else {
            msx_sd_mapper_set_ram_enabled(
                &msx, config.sd_mapper_ram);
            msx_sd_mapper_set_alternate_driver(
                &msx, config.sd_mapper_alternate_driver);
            for (unsigned card = 0;
                 card < MSX_SD_MAPPER_CARDS; ++card) {
                if (!config.sd_card_path[card][0])
                    continue;
                if (msx_mount_sd_card(
                        &msx, card, config.sd_card_path[card],
                        config.sd_image_mode) == 0)
                    continue;
                fprintf(stderr,
                        "cannot mount SD Mapper card %c image %s: %s\n",
                        'A' + (int)card,
                        config.sd_card_path[card],
                        msx_sd_card_error(&msx, card));
                if (cli.sd_card_path[card]) {
                    msx_destroy(&msx);
                    return 1;
                }
            }
        }
    }
    if (config.megaflash) {
        int megaflash_slot = -1;
        int state_path_result =
            config_megaflash_state_path(
                &config, megaflash_state_path,
                sizeof(megaflash_state_path));

        for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
            const char *owner =
                config_cartridge_slot_owner(&config, slot);

            if (owner &&
                strcmp(owner, "MegaFlashROM SCC+ SD") == 0) {
                megaflash_slot = (int)slot;
                break;
            }
        }
        if (megaflash_slot < 0 ||
            !config.megaflash_rom_path[0] ||
            state_path_result != 0 ||
            (megaflash_state_path[0]
             ? msx_load_megaflash_persistent(
                   &msx, (unsigned)megaflash_slot,
                   config.megaflash_rom_path,
                   megaflash_state_path)
             : msx_load_megaflash(
                   &msx, (unsigned)megaflash_slot,
                   config.megaflash_rom_path)) != 0) {
            fprintf(stderr,
                    "cannot load MegaFlashROM SCC+ SD "
                    "image %s: %s\n",
                    config.megaflash_rom_path[0]
                    ? config.megaflash_rom_path : "[not configured]",
                    msx_megaflash_flash_error(&msx));
            if (cli.megaflash_rom_path ||
                cli.megaflash_card_path[0] ||
                cli.megaflash_card_path[1]) {
                msx_destroy(&msx);
                return 1;
            }
            config.megaflash = false;
            config_normalize(&config);
        } else {
            for (unsigned card = 0;
                 card < MSX_MEGAFLASH_CARDS; ++card) {
                if (!config.megaflash_card_path[card][0])
                    continue;
                if (msx_mount_megaflash_card(
                        &msx, card,
                        config.megaflash_card_path[card],
                        config.sd_image_mode) == 0)
                    continue;
                fprintf(stderr,
                        "cannot mount MegaFlash SD %c image %s: %s\n",
                        'A' + (int)card,
                        config.megaflash_card_path[card],
                        msx_megaflash_card_error(&msx, card));
                if (cli.megaflash_card_path[card]) {
                    msx_destroy(&msx);
                    return 1;
                }
            }
        }
    }
    if (config.rs232) {
        /* The RS-232C EXTBIO/driver ROM is user-provided (rs232_rom in the
         * config or --rs232-rom). Without it the port device still works,
         * but EXTBIO 08H auto-detection is unavailable. */
        if (config.rs232_rom_path[0]) {
            int rs232_slot = -1;

            for (unsigned slot = 0;
                 slot < MSX_CARTRIDGE_SLOTS; ++slot) {
                const char *owner =
                    config_cartridge_slot_owner(&config, slot);

                if (owner && strcmp(owner, "RS-232C") == 0) {
                    rs232_slot = (int)slot;
                    break;
                }
            }
            if (rs232_slot < 0 ||
                msx_load_rs232(
                    &msx, (unsigned)rs232_slot,
                    config.rs232_rom_path) != 0) {
                fprintf(stderr,
                        "cannot load RS-232C ROM: %s\n",
                        config.rs232_rom_path);
                if (cli.rs232_rom_path) {
                    msx_destroy(&msx);
                    return 1;
                }
                config.rs232 = false;
                config_normalize(&config);
            }
        }
    }
    for (unsigned slot = 0; slot < MSX_CARTRIDGE_SLOTS; ++slot) {
        const char *path = config.cartridge_path[slot];
        const char *owner =
            config_cartridge_slot_owner(&config, slot);

        if (owner) {
            if (path[0] || cli.cartridge_mapper_set[slot])
                fprintf(stderr,
                        "cartridge slot %u unavailable: "
                        "reserved by %s\n",
                        slot + 1, owner);
            if (cli.cartridge_path[slot] ||
                cli.cartridge_mapper_set[slot]) {
                msx_destroy(&msx);
                return 1;
            }
            continue;
        }
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
    unapinet = unapinet_create();
    if (!unapinet) {
        fprintf(stderr, "cannot create MSX TCP/IP UNAPI bridge\n");
        if (cli.tcpip_unapi > 0) {
            msx_destroy(&msx);
            return 1;
        }
        config.tcpip_unapi = false;
    }
    g_unapinet = unapinet;

    rs232dev = rs232dev_create();
    if (!rs232dev) {
        fprintf(stderr, "cannot create MSX RS-232C interface\n");
        if (cli.rs232 > 0) {
            unapinet_destroy(unapinet);
            msx_destroy(&msx);
            return 1;
        }
        config.rs232 = false;
    }
    g_rs232dev = rs232dev;

    msx_set_io_extension(
        &msx, NULL, g_io_read, g_io_write, g_io_reset);
    msx_set_io_extension_advance(&msx, NULL, g_io_advance);

    if (config.tcpip_unapi &&
        !unapinet_set_enabled(unapinet, true)) {
        fprintf(stderr,
                "cannot enable MSX TCP/IP UNAPI bridge: %s\n",
                unapinet_error(unapinet));
        config.tcpip_unapi = false;
        if (cli.tcpip_unapi > 0) {
            rs232dev_destroy(rs232dev);
            unapinet_destroy(unapinet);
            msx_destroy(&msx);
            return 1;
        }
    }
    if (config.rs232)
        rs232dev_set_enabled(rs232dev, true);
    else
        rs232dev_set_enabled(rs232dev, false);
    definition = model_catalog_find(&models, config.machine_id);
    if (display_init(&display, &config, &msx,
                     definition ? definition->name : NULL) < 0) {
        display_quit(&display);
        rs232dev_destroy(rs232dev);
        unapinet_destroy(unapinet);
        msx_destroy(&msx);
        return 1;
    }
    gamepad_input_init(&gamepad);
    audio_output_init(&audio, !cli.headless && !cli.unthrottled);
    window_id = SDL_GetWindowID(display.window);

    /* Camera-shutter SFX for F4, matching 1984/1985. Decode the embedded
     * WAV once and open a dedicated stream so the shutter can replay over
     * overlapping AY audio. */
    sfx_stream = NULL;
    sfx_buf = NULL;
    sfx_buf_len = 0;
    if (!cli.headless) {
        SDL_AudioSpec sfx_spec;
        SDL_IOStream *io = SDL_IOFromConstMem(shutter_wav, shutter_wav_len);
        if (io && SDL_LoadWAV_IO(io, true, &sfx_spec,
                                 &sfx_buf, &sfx_buf_len)) {
            sfx_stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sfx_spec, NULL, NULL);
            if (sfx_stream)
                SDL_ResumeAudioStreamDevice(sfx_stream);
        }
    }

    notify_init();
    notify_set_mode(config.notifications);
    leds_init();
    overlay_init(
        &overlay, &config, &models, &display, &msx, unapinet, rs232dev);
    if (msx_can_boot(&msx))
        notify_post("Firmware running - F9 opens options");
    else
        notify_post(
            "Enable Tinker, then define firmware in F9 > Advanced model editor");
    if (gamepad_input_connected(&gamepad))
        notify_post("Gamepad connected: %s",
                    gamepad_input_name(&gamepad));

    memset(&capture, 0, sizeof(capture));
    if (cli.gif_out &&
        !gif_capture_start(&capture, cli.gif_out, &config))
        fprintf(stderr, "gif-out: failed to open %s\n", cli.gif_out);

    startup_info(config.notifications,
                 "1983 - MSX / MSX2 emulator (git %s)\n",
                 PROG_GIT_COMMIT);
    startup_info(config.notifications, "Machine catalogue: %s\n",
                 models.path[0] ? models.path : "built-in defaults");
    startup_info(config.notifications,
                 "%s, %s, %d KB RAM, %d KB VRAM, %s, %s PSG\n",
                 definition ? definition->name : msx.profile->name,
                 msx_region_name(msx.region),
                 msx.ram_kb, msx.profile->vram_kb, msx_vdp_name(&msx),
                 msx.profile->psg_variant == PSG_VARIANT_YM2149
                 ? "YM2149" : "AY-3-8910");
    startup_info(config.notifications,
                 "F4 screenshot, F5 reset, F6 GIF record, F9 options, "
                 "F11 fullscreen, F12 quit\n");
    startup_info(config.notifications,
                 "Shift+F1..F5 = MSX F1..F5, Shift+F7 = SELECT, "
                 "Shift+F8 = STOP\n");
    startup_info(config.notifications,
                 "Ctrl+V = paste host clipboard text\n");
    startup_info(config.notifications, "Gamepad: %s\n",
                 gamepad_input_name(&gamepad));
    if (msx_can_boot(&msx))
        startup_info(config.notifications,
                     "BIOS loaded%s%s%s%s%s\n",
                     msx.logo_loaded ? ", logo ROM loaded" : "",
                     msx.subrom_loaded ? ", Sub-ROM loaded" : "",
                     msx.disk_rom_loaded ? ", disk ROM loaded" : "",
                     msx_get_cartridge(&msx, 0)->loaded
                     ? ", cartridge 1 loaded" : "",
                     msx_get_cartridge(&msx, 1)->loaded
                     ? ", cartridge 2 loaded" : "");
    else
        startup_info(config.notifications,
                     "No BIOS loaded; use --bios PATH "
                     "(and --logo PATH for C-BIOS)\n");
    if (msx_sunrise_connected(&msx))
        startup_info(config.notifications,
                     "Sunrise IDE loaded in cartridge slot %d%s\n",
                     msx_sunrise_slot(&msx) + 1,
                     msx_sunrise_disk_mounted(&msx)
                     ? (msx_sunrise_disk_writable(&msx)
                        ? ", raw disk mounted read/write"
                        : ", raw disk mounted read-only")
                     : ", no disk mounted");
    if (msx_sd_mapper_connected(&msx)) {
        startup_info(config.notifications,
                     "SD Mapper V2 loaded in cartridge slot %d "
                     "(%s512 KB mapper)\n",
                     msx_sd_mapper_slot(&msx) + 1,
                     config.sd_mapper_ram ? "" : "no ");
        for (unsigned card = 0;
             card < MSX_SD_MAPPER_CARDS; ++card) {
            startup_info(config.notifications,
                         "  SD %c: %s%s\n", 'A' + (int)card,
                         msx_sd_card_mounted(&msx, card)
                         ? config.sd_card_path[card] : "empty",
                         msx_sd_card_mounted(&msx, card)
                         ? (msx_sd_card_writable(&msx, card)
                            ? " (read/write)" : " (read-only)")
                         : "");
        }
    }
    if (msx_megaflash_connected(&msx)) {
        startup_info(config.notifications,
                     "MegaFlashROM SCC+ SD loaded in cartridge slot %d "
                     "(8 MiB flash, SCC-I, PSG, 512 KB mapper)\n",
                     msx_megaflash_slot(&msx) + 1);
        for (unsigned card = 0;
             card < MSX_MEGAFLASH_CARDS; ++card) {
            startup_info(config.notifications,
                         "  MegaFlash SD %c: %s%s\n",
                         'A' + (int)card,
                         msx_megaflash_card_mounted(&msx, card)
                         ? config.megaflash_card_path[card] : "empty",
                         msx_megaflash_card_mounted(&msx, card)
                         ? (msx_megaflash_card_writable(&msx, card)
                            ? " (read/write)" : " (read-only)")
                         : "");
        }
    }
    if (msx_drive_a_mounted(&msx))
        startup_info(config.notifications,
                     "Floppy A: %s "
                     "(%s, %u tracks, %u sides, %u sectors)\n",
                     config.drive_a_path,
                     msx_drive_a_writable(&msx)
                     ? "read/write" : "read-only",
                     msx.fdc.drive_a.tracks,
                     msx.fdc.drive_a.sides,
                     msx.fdc.drive_a.sectors_per_track);
    if (msx_drive_b_mounted(&msx))
        startup_info(config.notifications,
                     "Floppy B: %s "
                     "(%s, %u tracks, %u sides, %u sectors)\n",
                     config.drive_b_path,
                     msx_drive_b_writable(&msx)
                     ? "read/write" : "read-only",
                     msx.fdc.drive_b.tracks,
                     msx.fdc.drive_b.sides,
                     msx.fdc.drive_b.sectors_per_track);
    if (msx_rtc_persistence_active(&msx))
        startup_info(config.notifications, "RTC CMOS: %s%s\n",
                     msx_rtc_persistence_path(&msx),
                     msx_rtc_persistence_has_error(&msx)
                     ? " (load warning; will recover atomically)" : "");
    if (msx_cassette_mounted(&msx)) {
        CassetteFileType type = msx_cassette_file_type(&msx);

        startup_info(config.notifications,
                     "Cassette inserted: %s (%s; %s)\n",
                     config.cassette_path,
                     cassette_file_type_name(type),
                     cassette_load_command(type));
    }
    if (unapinet_enabled(unapinet))
        startup_info(config.notifications,
                     "MSX TCP/IP UNAPI bridge enabled on ports 28h/29h; "
                     "run UNAPINET.COM in Nextor\n");

    next_frame_ns = SDL_GetTicksNS();
    paced_frame_hz = msx.frame_hz;
    while (running) {
        SDL_Event event;

        sync_mouse_ports(&msx, &config);
        if (display.mouse_captured && !mouse_input_enabled(&config))
            set_mouse_capture(&display, &msx, false);
        while (SDL_PollEvent(&event)) {
            bool overlay_was_visible = overlay.visible;

            if (event.type == SDL_EVENT_GAMEPAD_ADDED ||
                event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                bool was_connected =
                    gamepad_input_connected(&gamepad);

                gamepad_input_handle_device_event(&gamepad, &event);
                if (!was_connected &&
                    gamepad_input_connected(&gamepad))
                    notify_post("Gamepad connected: %s",
                                gamepad_input_name(&gamepad));
                else if (was_connected &&
                         !gamepad_input_connected(&gamepad))
                    notify_post("Gamepad disconnected");
                continue;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
                event.window.windowID == window_id) {
                kbd_release_all(&keyboard, &msx);
                paste_cancel(&paste, &msx);
                set_mouse_capture(&display, &msx, false);
            }
            if (display.mouse_captured &&
                event.type == SDL_EVENT_KEY_DOWN &&
                (event.key.mod & SDL_KMOD_CTRL) &&
                (event.key.key == SDLK_RETURN ||
                 event.key.key == SDLK_KP_ENTER)) {
                kbd_release_all(&keyboard, &msx);
                set_mouse_capture(&display, &msx, false);
                continue;
            }
            if (overlay_handle_event(&overlay, &event)) {
                sync_mouse_ports(&msx, &config);
                if (display.mouse_captured &&
                    (overlay.visible ||
                     !mouse_input_enabled(&config)))
                    set_mouse_capture(&display, &msx, false);
                if (!overlay_was_visible && overlay.visible) {
                    kbd_release_all(&keyboard, &msx);
                    paste_cancel(&paste, &msx);
                }
                if (overlay.visible)
                    leds_set_mouse_position(0.0f, 0.0f, false);
                continue;
            }
            track_led_mouse(&display, window_id, &event);
            if (event.type == SDL_EVENT_MOUSE_MOTION &&
                display.mouse_captured &&
                event.motion.windowID == window_id) {
                msx_mouse_add_host_motion(
                    &msx, main_input_port(&config),
                    (int)event.motion.xrel,
                    (int)event.motion.yrel);
                continue;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.windowID == window_id &&
                mouse_input_enabled(&config)) {
                unsigned button;

                if (!display.mouse_captured) {
                    set_mouse_capture(&display, &msx, true);
                    if (!display.mouse_captured)
                        continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT)
                    button = 0;
                else if (event.button.button == SDL_BUTTON_RIGHT)
                    button = 1;
                else
                    continue;
                msx_mouse_set_button(
                    &msx, main_input_port(&config), button, true);
                continue;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                display.mouse_captured &&
                event.button.windowID == window_id) {
                unsigned button;

                if (event.button.button == SDL_BUTTON_LEFT)
                    button = 0;
                else if (event.button.button == SDL_BUTTON_RIGHT)
                    button = 1;
                else
                    continue;
                msx_mouse_set_button(
                    &msx, main_input_port(&config), button, false);
                continue;
            }
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

            /*
             * Ignore chord auto-repeat: holding Ctrl+V must not restart
             * the queue and duplicate the text already typed.
             */
            if (event.key.scancode == SDL_SCANCODE_V &&
                !event.key.repeat &&
                (event.key.mod & SDL_KMOD_CTRL)) {
                char *text;

                /*
                 * Drop the physical Ctrl+V chord before the synthetic
                 * matrix input starts. Its later key-up events are harmless
                 * because KbdHost no longer considers the keys held.
                 */
                kbd_release_all(&keyboard, &msx);
                text = SDL_GetClipboardText();
                if (!text) {
                    notify_post("Cannot read host clipboard: %s",
                                SDL_GetError());
                } else if (!text[0]) {
                    notify_post("Host clipboard is empty");
                } else if (!paste_start(&paste, &msx, text)) {
                    notify_post("Cannot paste clipboard: out of memory");
                } else {
                    notify_post("Pasting host clipboard");
                }
                SDL_free(text);
                continue;
            }

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
                    if (sfx_stream && sfx_buf) {
                        SDL_ClearAudioStream(sfx_stream);
                        SDL_PutAudioStreamData(
                            sfx_stream, sfx_buf, (int)sfx_buf_len);
                    }
                    break;
                }
                case SDLK_F5:
                    reset_machine(&msx, &config, &paste, &keyboard,
                                  &display, &audio);
                    notify_post("Machine reset");
                    break;
                case SDLK_F6:
                    gif_capture_toggle(&capture, &config);
                    if (capture.encoder)
                        notify_post("GIF recording started");
                    else
                        notify_post("GIF recording stopped");
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
        if (overlay_take_machine_reset_request(&overlay)) {
            reset_machine(&msx, &config, &paste, &keyboard,
                          &display, &audio);
            notify_post("Hardware changed; machine reset");
        }
        for (unsigned port = 0; port < MSX_JOYSTICK_PORTS; ++port)
            msx_joystick_set_pressed(&msx, port, 0);
        {
            unsigned port = main_input_port(&config);

            if (config.joy_port_device[port] == JOY_PORT_JOYSTICK)
                msx_joystick_set_pressed(
                    &msx, port, gamepad_input_poll(&gamepad));
        }
        /*
         * Scripted paste for headless smoke tests. --paste-repeat models an
         * OS auto-repeat storm re-invoking the paste trigger: every requeue
         * restarts the queue exactly like a repeated Ctrl+V key-down does.
         */
        if (cli.paste_text && host_frame >= cli.paste_at) {
            bool due = host_frame == cli.paste_at ||
                       (cli.paste_repeat > 0 &&
                        (host_frame - cli.paste_at) % cli.paste_repeat == 0);

            if (due && !paste_start(&paste, &msx, cli.paste_text))
                fprintf(stderr, "--paste-text: out of memory\n");
        }
        if (!msx.paused && !overlay.visible)
            paste_tick(&paste, &msx);
        unapinet_poll(unapinet);
        msx_run_frame(&msx);
        audio_output_submit(&audio, msx.audio_samples,
                            msx.audio_sample_count);
        leds_set_state(LED_CAPS, msx.caps_led);
        leds_set_state(LED_KANA, msx.kana_led);
        leds_set_state(LED_TAPE, msx_cassette_rolling(&msx));
        if (msx_sunrise_take_activity(&msx))
            leds_ping(LED_IDE);
        if (msx_sd_card_take_activity(&msx, 0))
            leds_ping(LED_SD_A);
        if (msx_sd_card_take_activity(&msx, 1))
            leds_ping(LED_SD_B);
        if (msx_megaflash_take_activity(&msx, 0))
            leds_ping(LED_SD_A);
        if (msx_megaflash_take_activity(&msx, 1))
            leds_ping(LED_SD_B);
        if (msx_drive_a_take_activity(&msx))
            leds_ping(LED_FDC_A);
        if (msx_drive_b_take_activity(&msx))
            leds_ping(LED_FDC_B);
        if (unapinet_take_activity(unapinet))
            leds_ping(LED_NETWORK);
        if (rs232dev_take_rx_activity(rs232dev))
            leds_ping_half(LED_RS232, true);   /* green RX half */
        if (rs232dev_take_tx_activity(rs232dev))
            leds_ping_half(LED_RS232, false);  /* red TX half */
        notify_tick(1000 / msx.frame_hz);
        display_draw(&display, &msx);
        gif_capture_frame(&capture, display.pixels,
                          1000000000ULL / (uint64_t)msx.frame_hz);
        draw_debug(&config, &msx, &display);
        draw_paused(&msx, &display);
        overlay_render_cassette_scope(&overlay);
        notify_render(display.renderer, DISPLAY_SCREEN_H);
        display_present_begin(&display);
        overlay_render(&overlay);
        display_present_end(&display, &msx);

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

    if (cli.screenshot_path &&
        display_save_ppm(&display, cli.screenshot_path) != 0)
        fprintf(stderr, "cannot save final screenshot: %s\n",
                cli.screenshot_path);
    gif_capture_stop(&capture);
    config.fullscreen = display.fullscreen;
    config.scale = display.scale;
    config_save(&config);
    if (cli.dump_ram_spec) {
        char *end = NULL;
        long start = strtol(cli.dump_ram_spec, &end, 0);
        long length = end && *end == ':' ? strtol(end + 1, &end, 0) : -1;

        if (!end || *end || start < 0 || length <= 0 ||
            start + length > 0x10000) {
            fprintf(stderr, "--dump-ram: expected ADDR:LEN, e.g. 0x8000:64\n");
        } else {
            for (long i = 0; i < length; ++i) {
                if (i % 16 == 0)
                    printf("%04lX:", (unsigned long)start + i);
                printf(" %02X",
                       msx_memory_read(&msx, (u16)(start + i)));
                if (i % 16 == 15 || i + 1 == length)
                    putchar('\n');
            }
        }
    }
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
    int shutdown_status = 0;
    if (msx_flush_rtc_persistence(
            &msx, rtc_host_seconds()) != 0) {
        fprintf(stderr, "cannot flush RTC CMOS at shutdown: %s\n",
                msx_rtc_persistence_error(&msx));
        shutdown_status = 1;
    }
    if (msx_sunrise_disk_mounted(&msx) &&
        msx_flush_sunrise_disk(&msx) != 0) {
        fprintf(stderr, "cannot flush IDE image at shutdown: %s\n",
                msx_sunrise_disk_error(&msx));
        shutdown_status = 1;
    }
    if (msx_megaflash_connected(&msx) &&
        msx_flush_megaflash(&msx) != 0) {
        fprintf(stderr,
                "cannot flush MegaFlashROM flash state at shutdown: %s\n",
                msx_megaflash_flash_error(&msx));
        shutdown_status = 1;
    }
    for (unsigned card = 0;
         card < MSX_MEGAFLASH_CARDS; ++card) {
        if (!msx_megaflash_card_mounted(&msx, card) ||
            msx_flush_megaflash_card(&msx, card) == 0)
            continue;
        fprintf(stderr,
                "cannot flush MegaFlash SD %c at shutdown: %s\n",
                'A' + (int)card,
                msx_megaflash_card_error(&msx, card));
        shutdown_status = 1;
    }
    for (unsigned card = 0;
         card < MSX_SD_MAPPER_CARDS; ++card) {
        if (!msx_sd_card_mounted(&msx, card) ||
            msx_flush_sd_card(&msx, card) == 0)
            continue;
        fprintf(stderr,
                "cannot flush SD Mapper card %c at shutdown: %s\n",
                'A' + (int)card,
                msx_sd_card_error(&msx, card));
        shutdown_status = 1;
    }
    if (msx_drive_a_mounted(&msx) &&
        msx_flush_drive_a(&msx) != 0) {
        fprintf(stderr, "cannot flush Floppy A at shutdown: %s\n",
                msx_drive_a_error(&msx));
        shutdown_status = 1;
    }
    if (msx_drive_b_mounted(&msx) &&
        msx_flush_drive_b(&msx) != 0) {
        fprintf(stderr, "cannot flush Floppy B at shutdown: %s\n",
                msx_drive_b_error(&msx));
        shutdown_status = 1;
    }
    if (sfx_stream)
        SDL_DestroyAudioStream(sfx_stream);
    if (sfx_buf)
        SDL_free(sfx_buf);
    /* audio_output_quit() tears down SDL_INIT_AUDIO. Destroy the separate
     * shutter stream first; SDL no longer owns a valid audio device after
     * the subsystem has stopped. */
    audio_output_quit(&audio);
    gamepad_input_destroy(&gamepad);
    paste_cancel(&paste, &msx);
    set_mouse_capture(&display, &msx, false);
    msx_set_io_extension(&msx, NULL, NULL, NULL, NULL);
    msx_set_io_extension_advance(&msx, NULL, NULL);
    rs232dev_destroy(rs232dev);
    unapinet_destroy(unapinet);
    display_quit(&display);
    msx_destroy(&msx);
    return shutdown_status;
}
