#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#define LED_GLOW_MS 120

typedef enum {
    LED_POWER = 0,
    LED_CAPS,
    LED_KANA,
    LED_FDC_A,
    LED_FDC_B,
    LED_TAPE,
    LED_IDE,
    LED_COUNT
} LedId;

void leds_init(void);
void leds_set_enabled(LedId id, bool enabled);
void leds_set_state(LedId id, bool active);
void leds_ping(LedId id);
void leds_set_mouse_position(float x, float y, bool inside);
void leds_render(SDL_Renderer *renderer, int x, int y, int w, int h);
void leds_render_hover(SDL_Renderer *renderer);
