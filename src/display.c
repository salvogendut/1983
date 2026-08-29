#include "display.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "leds.h"
#include "ui.h"

#define VDP_PRESENTATION_LINES 240
#define VDP_DEFAULT_CROP_X 40
#define VDP_DEFAULT_CROP_W 560
#define VDP_ACTIVE_HIGH_RES_W 512
#define VDP_ACTIVE_LEFT 64

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

void display_calculate_layout(int output_w, int output_h,
                              DisplayLayout *layout) {
    int content_h;

    if (!layout)
        return;
    if (output_w < 1)
        output_w = 1;
    if (output_h < DISPLAY_FOOTER_H + 1)
        output_h = DISPLAY_FOOTER_H + 1;

    layout->footer_y = output_h - DISPLAY_FOOTER_H;
    content_h = layout->footer_y;
    layout->screen_w = output_w;
    layout->screen_h = content_h;
    if ((int64_t)layout->screen_w * DISPLAY_LOGICAL_H >
        (int64_t)layout->screen_h * DISPLAY_LOGICAL_W) {
        layout->screen_w =
            layout->screen_h * DISPLAY_LOGICAL_W / DISPLAY_LOGICAL_H;
    } else {
        layout->screen_h =
            layout->screen_w * DISPLAY_LOGICAL_H / DISPLAY_LOGICAL_W;
    }
    if (layout->screen_w < 1)
        layout->screen_w = 1;
    if (layout->screen_h < 1)
        layout->screen_h = 1;
    layout->screen_x = (output_w - layout->screen_w) / 2;
    layout->screen_y = (content_h - layout->screen_h) / 2;
}

void display_set_title(Display *display, const MsxMachine *msx,
                       const char *model_name) {
    char title[96];
    if (model_name && model_name[0] && model_name != display->model_name)
        snprintf(display->model_name, sizeof(display->model_name),
                 "%s", model_name);
    snprintf(title, sizeof(title), "1983 - %s (%s)",
             display->model_name[0]
             ? display->model_name : msx->profile->name,
             msx_video_output_name(msx));
    SDL_SetWindowTitle(display->window, title);
    display->powergraph_output =
        msx_video_output_is_powergraph(msx);
}

int display_init(Display *display, const Config *config,
                 const MsxMachine *msx, const char *model_name) {
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
        DISPLAY_LOGICAL_H * display->scale + DISPLAY_FOOTER_H,
        flags);
    if (!display->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowMinimumSize(display->window,
                             DISPLAY_LOGICAL_W / 2,
                             DISPLAY_LOGICAL_H / 2 + DISPLAY_FOOTER_H);
    display->renderer = SDL_CreateRenderer(display->window, NULL);
    if (!display->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderVSync(display->renderer, 0);
    display->texture = SDL_CreateTexture(display->renderer,
                                         SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         DISPLAY_FB_W,
                                         DISPLAY_FB_H);
    if (!display->texture) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }
    display->canvas = SDL_CreateTexture(display->renderer,
                                        SDL_PIXELFORMAT_XRGB8888,
                                        SDL_TEXTUREACCESS_TARGET,
                                        DISPLAY_LOGICAL_W,
                                        DISPLAY_LOGICAL_H);
    if (!display->canvas) {
        fprintf(stderr, "SDL_CreateTexture canvas: %s\n", SDL_GetError());
        return -1;
    }
    display_set_smoothing(display, display->smoothing);
    display_set_title(display, msx, model_name);
    display_prepare_scaffold(display, msx);
    return 0;
}

void display_quit(Display *display) {
    if (display->window && display->mouse_captured)
        SDL_SetWindowRelativeMouseMode(display->window, false);
    if (display->renderer)
        SDL_SetRenderTarget(display->renderer, NULL);
    if (display->canvas)
        SDL_DestroyTexture(display->canvas);
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
        msx_model_is_msx2(msx->profile->model)
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
             msx_has_memory_mapper(msx) ? " with mapper" : "");
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

static void draw_footer(Display *display, const MsxMachine *msx,
                        int output_w, int output_h) {
    const char *full_keys =
        "  F3=ROM bank  F4=screenshot  F5=reset  F6=capture  F8=monitor  "
        "F9=options  F11=fullscreen  F12=quit";
    const char *compact_keys =
        "  F3=ROM F4=shot F5=rst F6=rec F8=mon F9=opts F11=full F12=quit";
    const char *captured_keys =
        "  Mouse captured  Ctrl+Enter=release";
    const char *model = msx->profile->name;
    const char *keys =
        display->mouse_captured ? captured_keys : full_keys;
    float total_w = (float)(strlen(model) + strlen(keys)) * 8.0f;
    float strip_y = (float)(output_h - DISPLAY_FOOTER_H);
    float model_x;
    float keys_x;

    if (!display->mouse_captured && total_w > (float)output_w) {
        keys = compact_keys;
        total_w = (float)(strlen(model) + strlen(keys)) * 8.0f;
    }
    model_x = ((float)output_w - total_w) * 0.5f;
    if (model_x < 0.0f)
        model_x = 0.0f;
    keys_x = model_x + (float)strlen(model) * 8.0f;
    ui_fill_rect(display->renderer, 0.0f, strip_y,
                 (float)output_w, (float)DISPLAY_STRIP_H,
                 16, 16, 20, 255);
    ui_draw_text(display->renderer, model_x, strip_y + 4.0f,
                 model, 255, 64, 64);
    ui_draw_text(display->renderer, model_x + 1.0f, strip_y + 4.0f,
                 model, 255, 64, 64);
    ui_draw_text(display->renderer, keys_x,
                 strip_y + 4.0f,
                 keys, 224, 224, 224);
    leds_render(display->renderer, 0,
                output_h - DISPLAY_LED_H,
                output_w, DISPLAY_LED_H);
}

static int presentation_raw_x(int destination_x) {
    /*
     * openMSX's default horizontal_stretch=280 presents the middle 280
     * low-resolution dots (560 high-resolution dots) of its 320/640-dot
     * raw line. Sampling at destination-pixel centres keeps both borders
     * symmetrical.
     */
    return VDP_DEFAULT_CROP_X +
        (VDP_DEFAULT_CROP_W * (destination_x * 2 + 1)) /
        (DISPLAY_FB_W * 2);
}

static int vdp_active_left(const MsxVdp *vdp) {
    int left = VDP_ACTIVE_LEFT;

    if (vdp_type_is_v99x8(vdp->type)) {
        int horizontal_adjust =
            (vdp->registers[18] & 0x0f) ^ 7;
        left += (horizontal_adjust - 7) * 2;
    }
    if (vdp->registers[1] & 0x10) {
        int text_adjust =
            vdp_type_is_v99x8(vdp->type) ? 9 : 6;
        left += (text_adjust - 8) * 2;
    }
    return left;
}

static int vdp_active_top(const MsxVdp *vdp) {
    int top =
        (VDP_PRESENTATION_LINES - (int)vdp->render_height) / 2;

    if (vdp_type_is_v99x8(vdp->type)) {
        int vertical_adjust =
            (vdp->registers[18] >> 4) ^ 7;
        top += vertical_adjust - 7;
    }
    return top;
}

void display_compose_vdp(u32 *destination, const MsxVdp *vdp) {
    int source_x[DISPLAY_FB_W];
    unsigned border_phase[DISPLAY_FB_W];
    u32 border[2];
    int active_left;
    int active_top;

    if (!destination || !vdp)
        return;
    active_left = vdp_active_left(vdp);
    active_top = vdp_active_top(vdp);
    border[0] = vdp_border_colour(vdp, 0);
    border[1] = vdp_border_colour(vdp, 1);
    for (int x = 0; x < DISPLAY_FB_W; ++x) {
        int raw_x = presentation_raw_x(x);
        int active_x = raw_x - active_left;

        border_phase[x] = (unsigned)raw_x & 1;
        source_x[x] =
            (unsigned)active_x < VDP_ACTIVE_HIGH_RES_W &&
            (vdp->render_width == MSX1_VIDEO_W ||
             vdp->render_width == MSX2_VIDEO_W)
            ? active_x * (int)vdp->render_width /
              VDP_ACTIVE_HIGH_RES_W
            : -1;
    }
    for (int y = 0; y < DISPLAY_FB_H; ++y) {
        int raw_y = y * VDP_PRESENTATION_LINES / DISPLAY_FB_H;
        int source_y = raw_y - active_top;
        bool active_line =
            (unsigned)source_y < vdp->render_height;

        for (int x = 0; x < DISPLAY_FB_W; ++x) {
            destination[y * DISPLAY_FB_W + x] =
                active_line && source_x[x] >= 0
                ? vdp->pixels[
                    (unsigned)source_y * vdp->render_width +
                    (unsigned)source_x[x]]
                : border[border_phase[x]];
        }
    }
}

static void display_compose_v9990(u32 *destination,
                                  const MsxV9990 *v9990) {
    unsigned width;
    unsigned height;

    if (!destination || !v9990 || !v9990->pixels)
        return;
    width = v9990->render_width;
    height = v9990->render_height;
    if (!width || !height)
        return;
    for (int y = 0; y < DISPLAY_FB_H; ++y) {
        unsigned source_y = (unsigned)y * height / DISPLAY_FB_H;

        for (int x = 0; x < DISPLAY_FB_W; ++x) {
            unsigned source_x = (unsigned)x * width / DISPLAY_FB_W;

            destination[y * DISPLAY_FB_W + x] =
                v9990->pixels[source_y * width + source_x];
        }
    }
}

void display_draw(Display *display, const MsxMachine *msx) {
    SDL_FRect destination = {
        0.0f, 0.0f, (float)DISPLAY_FB_W, (float)DISPLAY_FB_H
    };

    if (display->powergraph_output !=
        msx_video_output_is_powergraph(msx))
        display_set_title(display, msx, NULL);
    if (msx_can_boot(msx)) {
        if (msx_video_output_is_powergraph(msx))
            display_compose_v9990(display->pixels, &msx->v9990);
        else
            display_compose_vdp(display->pixels, &msx->vdp);
    }

    SDL_UpdateTexture(display->texture, NULL, display->pixels,
                      DISPLAY_FB_W * (int)sizeof(*display->pixels));
    SDL_SetRenderTarget(display->renderer, display->canvas);
    SDL_SetRenderViewport(display->renderer, NULL);
    SDL_SetRenderScale(display->renderer, 1.0f, 1.0f);
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
}

void display_present_begin(Display *display) {
    DisplayLayout layout;
    SDL_FRect destination;
    int output_w;
    int output_h;

    SDL_SetRenderTarget(display->renderer, NULL);
    SDL_SetRenderViewport(display->renderer, NULL);
    SDL_SetRenderScale(display->renderer, 1.0f, 1.0f);
    if (!SDL_GetRenderOutputSize(display->renderer, &output_w, &output_h)) {
        SDL_GetWindowSize(display->window, &output_w, &output_h);
    }
    display_calculate_layout(output_w, output_h, &layout);
    destination = (SDL_FRect) {
        (float)layout.screen_x,
        (float)layout.screen_y,
        (float)layout.screen_w,
        (float)layout.screen_h
    };
    SDL_SetRenderDrawColor(display->renderer, 0, 0, 0, 255);
    SDL_RenderClear(display->renderer);
    SDL_RenderTexture(display->renderer, display->canvas, NULL,
                      &destination);
}

void display_present_end(Display *display, const MsxMachine *msx) {
    int output_w;
    int output_h;

    SDL_SetRenderViewport(display->renderer, NULL);
    SDL_SetRenderScale(display->renderer, 1.0f, 1.0f);
    if (!SDL_GetRenderOutputSize(display->renderer, &output_w, &output_h)) {
        SDL_GetWindowSize(display->window, &output_w, &output_h);
    }
    draw_footer(display, msx, output_w, output_h);
    leds_render_hover(display->renderer);
    SDL_RenderPresent(display->renderer);
}

void display_present(Display *display, const MsxMachine *msx) {
    display_present_begin(display);
    display_present_end(display, msx);
}

void display_set_scale(Display *display, int scale) {
    scale = clamp_int(scale, 1, 4);
    display->scale = scale;
    if (!display->fullscreen)
        SDL_SetWindowSize(display->window,
                          DISPLAY_LOGICAL_W * scale,
                          DISPLAY_LOGICAL_H * scale + DISPLAY_FOOTER_H);
}

void display_set_smoothing(Display *display, bool smoothing) {
    display->smoothing = smoothing;
    if (display->canvas)
        SDL_SetTextureScaleMode(display->canvas,
            smoothing ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void display_set_crt(Display *display, bool enabled, int scanlines) {
    display->real_crt = enabled;
    display->crt_scanlines = clamp_int(scanlines, 0, 95);
}

bool display_set_mouse_capture(Display *display, bool captured) {
    if (!display || !display->window)
        return false;
    if (display->mouse_captured == captured)
        return true;
    if (!SDL_SetWindowRelativeMouseMode(display->window, captured)) {
        fprintf(stderr, "mouse capture: %s\n", SDL_GetError());
        return false;
    }
    display->mouse_captured = captured;
    if (captured)
        leds_set_mouse_position(0.0f, 0.0f, false);
    return true;
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
