#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "config.h"
#include "msx.h"
#include "types.h"

#define DISPLAY_FB_W 640
#define DISPLAY_FB_H 480
#define DISPLAY_LOGICAL_W 640
#define DISPLAY_SCREEN_H 480
#define DISPLAY_STRIP_H 18
#define DISPLAY_LED_H 22
#define DISPLAY_LOGICAL_H \
    (DISPLAY_SCREEN_H + DISPLAY_STRIP_H + DISPLAY_LED_H)

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    u32          *pixels;

    int  scale;
    bool fullscreen;
    bool smoothing;
    bool real_crt;
    int  crt_scanlines;
} Display;

int  display_init(Display *display, const Config *config,
                  const MsxMachine *msx);
void display_quit(Display *display);
void display_prepare_scaffold(Display *display, const MsxMachine *msx);
void display_draw(Display *display, const MsxMachine *msx);
void display_present(Display *display);
void display_set_title(Display *display, const MsxMachine *msx);
void display_set_scale(Display *display, int scale);
void display_set_smoothing(Display *display, bool smoothing);
void display_set_crt(Display *display, bool enabled, int scanlines);
void display_toggle_fullscreen(Display *display);
int  display_save_ppm(const Display *display, const char *path);
