#pragma once

#include <SDL3/SDL.h>

typedef enum {
    NOTIFY_MODE_OFF = 0,
    NOTIFY_MODE_SCREEN,
    NOTIFY_MODE_CONSOLE
} NotifyMode;

void notify_init(void);
void notify_set_mode(NotifyMode mode);
void notify_post(const char *fmt, ...);
void notify_tick(int elapsed_ms);
void notify_render(SDL_Renderer *renderer, int screen_h);
