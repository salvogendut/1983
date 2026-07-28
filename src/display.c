#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "leds.h"
#include "ui.h"

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

void display_set_title(Display *display, const MsxMachine *msx) {
    char title[96];
    snprintf(title, sizeof(title), "1983 - %s (%s)",
             msx->profile->name, msx_vdp_name(msx));
    SDL_SetWindowTitle(display->window, title);
}

int display_init(Display *display, const Config *config,
                 const MsxMachine *msx) {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;

    memset(display, 0, sizeof(*display));
    display->pixels = calloc((size_t)DISPLAY_FB_W * DISPLAY_FB_H,
                             sizeof(*display->pixels));
    if (!display->pixels) {
        fprintf(stderr, "display: framebuffer allocation failed\n");
        return -1;
    }
    display->scale = clamp_int(config->scale, 1, 4);
    display->fullscreen = config->fullscreen;
    display->smoothing = config->smoothing;
    display->real_crt = config->real_crt;
    display->crt_scanlines =
        clamp_int(config->crt_scanlines, 0, 95);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    if (display->fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    display->window = SDL_CreateWindow(
        "1983 - MSX / MSX2 emulator",
        DISPLAY_LOGICAL_W * display->scale,
        DISPLAY_LOGICAL_H * display->scale,
        flags);
    if (!display->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    display->renderer = SDL_CreateRenderer(display->window, NULL);
    if (!display->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderVSync(display->renderer, 0);
    SDL_SetRenderLogicalPresentation(display->renderer,
                                     DISPLAY_LOGICAL_W,
                                     DISPLAY_LOGICAL_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    display->texture = SDL_CreateTexture(display->renderer,
                                         SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         DISPLAY_FB_W,
                                         DISPLAY_FB_H);
    if (!display->texture) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }
    display_set_smoothing(display, display->smoothing);
    display_set_title(display, msx);
    display_prepare_scaffold(display, msx);
    return 0;
}

void display_quit(Display *display) {
    if (display->texture)
        SDL_DestroyTexture(display->texture);
    if (display->renderer)
        SDL_DestroyRenderer(display->renderer);
    if (display->window)
        SDL_DestroyWindow(display->window);
    free(display->pixels);
    memset(display, 0, sizeof(*display));
    SDL_Quit();
}

void display_prepare_scaffold(Display *display, const MsxMachine *msx) {
    static const u32 msx_colors[] = {
        0x000000, 0x3EB849, 0x74D07D, 0x5955E0,
        0x8076F1, 0xB95E51, 0x65DBEF, 0xDB6559,
        0xFF897D, 0xCCC35E, 0xDED087, 0x3AA241,
        0xB766B5, 0xCCCCCC, 0xFFFFFF,
    };
    u32 background =
        msx->profile->model == MSX_MODEL_GENERIC_MSX2
        ? 0x111A45 : 0x16245C;

    for (int y = 0; y < DISPLAY_FB_H; ++y) {
        for (int x = 0; x < DISPLAY_FB_W; ++x) {
            u32 color = background;
            if (x < 12 || x >= DISPLAY_FB_W - 12 ||
                y < 12 || y >= DISPLAY_FB_H - 12)
                color = 0x080B18;
            display->pixels[y * DISPLAY_FB_W + x] = color;
        }
    }
    for (int i = 0; i < 15; ++i) {
        int x0 = 48 + i * 36;
        for (int y = 388; y < 420; ++y)
            for (int x = x0; x < x0 + 28; ++x)
                display->pixels[y * DISPLAY_FB_W + x] = msx_colors[i];
    }
}

static void draw_scaffold_text(Display *display, const MsxMachine *msx) {
    char line[128];
    const char *status =
        "Machine scaffold ready - load firmware with --bios PATH";

    ui_draw_text(display->renderer, 40.0f, 44.0f,
                 "1983 - MSX / MSX2 emulator", 255, 255, 120);
    ui_draw_text(display->renderer, 40.0f, 66.0f,
                 status, 210, 220, 255);
    snprintf(line, sizeof(line), "Machine: %s", msx->profile->name);
    ui_draw_text(display->renderer, 40.0f, 112.0f, line, 235, 235, 235);
    snprintf(line, sizeof(line), "Video:   %s, %d KB VRAM, %s",
             msx_vdp_name(msx), msx->profile->vram_kb,
             msx_region_name(msx->region));
    ui_draw_text(display->renderer, 40.0f, 130.0f, line, 235, 235, 235);
    snprintf(line, sizeof(line), "Memory:  %d KB RAM%s",
             msx->ram_kb,
             msx->profile->memory_mapper ? " with mapper" : "");
    ui_draw_text(display->renderer, 40.0f, 148.0f, line, 235, 235, 235);
    snprintf(line, sizeof(line), "CPU:     Z80 at %.6f MHz",
             (double)MSX_CPU_HZ / 1000000.0);
    ui_draw_text(display->renderer, 40.0f, 166.0f, line, 235, 235, 235);
    snprintf(line, sizeof(line), "Slots:   four primary%s",
             msx->profile->expanded_slots
             ? ", secondary expansion enabled" : "");
    ui_draw_text(display->renderer, 40.0f, 184.0f, line, 235, 235, 235);
    ui_draw_text(display->renderer, 40.0f, 230.0f,
                 "Press F9 to explore the shared options overlay.",
                 160, 225, 190);
}

static void draw_footer(Display *display, const MsxMachine *msx) {
    const char *keys =
        " F4 shot F5 reset F6 rec F8 mon F9 options F11 full F12 quit";
    float total_w =
        (float)(strlen(msx->profile->name) + strlen(keys)) * 8.0f;
    float model_x = ((float)DISPLAY_LOGICAL_W - total_w) * 0.5f;
    float keys_x;

    if (model_x < 2.0f)
        model_x = 2.0f;
    keys_x = model_x + (float)strlen(msx->profile->name) * 8.0f;
    ui_fill_rect(display->renderer, 0.0f, (float)DISPLAY_SCREEN_H,
                 (float)DISPLAY_LOGICAL_W, (float)DISPLAY_STRIP_H,
                 16, 16, 20, 255);
    ui_draw_text(display->renderer, model_x,
                 (float)DISPLAY_SCREEN_H + 5.0f,
                 msx->profile->name, 255, 64, 64);
    ui_draw_text(display->renderer, keys_x,
                 (float)DISPLAY_SCREEN_H + 5.0f,
                 keys, 224, 224, 224);
}

void display_draw(Display *display, const MsxMachine *msx) {
    SDL_FRect destination = {
        0.0f, 0.0f, (float)DISPLAY_FB_W, (float)DISPLAY_FB_H
    };

    if (msx_can_boot(msx)) {
        for (int y = 0; y < DISPLAY_FB_H; ++y) {
            int source_y = y * MSX1_VIDEO_H / DISPLAY_FB_H;
            for (int x = 0; x < DISPLAY_FB_W; ++x) {
                int source_x = x * MSX1_VIDEO_W / DISPLAY_FB_W;
                display->pixels[y * DISPLAY_FB_W + x] =
                    msx->vdp.pixels[
                        source_y * MSX1_VIDEO_W + source_x];
            }
        }
    }

    SDL_UpdateTexture(display->texture, NULL, display->pixels,
                      DISPLAY_FB_W * (int)sizeof(*display->pixels));
    SDL_SetRenderDrawColor(display->renderer, 0, 0, 0, 255);
    SDL_RenderClear(display->renderer);
    SDL_RenderTexture(display->renderer, display->texture, NULL, &destination);

    if (display->real_crt && display->crt_scanlines > 0) {
        Uint8 alpha =
            (Uint8)((display->crt_scanlines * 255 + 50) / 100);
        SDL_SetRenderDrawBlendMode(display->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(display->renderer, 0, 0, 0, alpha);
        for (int y = 1; y < DISPLAY_SCREEN_H; y += 2) {
            SDL_FRect line = {
                0.0f, (float)y, (float)DISPLAY_LOGICAL_W, 1.0f
            };
            SDL_RenderFillRect(display->renderer, &line);
        }
    }
    if (!msx_can_boot(msx))
        draw_scaffold_text(display, msx);
    draw_footer(display, msx);
    leds_render(display->renderer, 0,
                DISPLAY_SCREEN_H + DISPLAY_STRIP_H,
                DISPLAY_LOGICAL_W, DISPLAY_LED_H);
}

void display_present(Display *display) {
    SDL_RenderPresent(display->renderer);
}

void display_set_scale(Display *display, int scale) {
    scale = clamp_int(scale, 1, 4);
    display->scale = scale;
    if (!display->fullscreen)
        SDL_SetWindowSize(display->window,
                          DISPLAY_LOGICAL_W * scale,
                          DISPLAY_LOGICAL_H * scale);
}

void display_set_smoothing(Display *display, bool smoothing) {
    display->smoothing = smoothing;
    if (display->texture)
        SDL_SetTextureScaleMode(display->texture,
            smoothing ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void display_set_crt(Display *display, bool enabled, int scanlines) {
    display->real_crt = enabled;
    display->crt_scanlines = clamp_int(scanlines, 0, 95);
}

void display_toggle_fullscreen(Display *display) {
    display->fullscreen = !display->fullscreen;
    SDL_SetWindowFullscreen(display->window, display->fullscreen);
}

int display_save_ppm(const Display *display, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror("display_save_ppm");
        return -1;
    }
    fprintf(file, "P6\n%d %d\n255\n", DISPLAY_FB_W, DISPLAY_FB_H);
    for (int y = 0; y < DISPLAY_FB_H; ++y) {
        for (int x = 0; x < DISPLAY_FB_W; ++x) {
            u32 pixel = display->pixels[y * DISPLAY_FB_W + x];
            Uint8 rgb[3] = {
                (Uint8)(pixel >> 16),
                (Uint8)(pixel >> 8),
                (Uint8)pixel,
            };
            fwrite(rgb, 1, sizeof(rgb), file);
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}
