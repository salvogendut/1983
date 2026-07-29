#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#define LED_GLOW_MS 120

typedef enum {
    LED_POWER = 0,
    LED_CARTRIDGE_I,
    LED_CARTRIDGE_II,
    LED_CAPS,
    LED_KANA,
    LED_FDC_A,
    LED_FDC_B,
    LED_TAPE,
    LED_IDE,
    LED_SD_A,
    LED_SD_B,
    LED_COUNT
} LedId;

typedef enum {
    LED_CARTRIDGE_STANDARD = 0,
    LED_CARTRIDGE_NETWORK
} LedCartridgeType;

typedef struct {
    LedCartridgeType type;
    bool present;
    bool activity;
} LedCartridgeState;

void leds_init(void);
void leds_set_enabled(LedId id, bool enabled);
void leds_set_state(LedId id, bool active);
void leds_ping(LedId id);
void leds_set_cartridge(unsigned slot, LedCartridgeType type, bool present);
void leds_set_cartridge_activity(unsigned slot, bool active);
void leds_ping_cartridge_activity(unsigned slot);
LedCartridgeState leds_get_cartridge_state(unsigned slot);
void leds_set_mouse_position(float x, float y, bool inside);
void leds_render(SDL_Renderer *renderer, int x, int y, int w, int h);
void leds_render_hover(SDL_Renderer *renderer);
