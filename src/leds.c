#include "leds.h"

#include <stdio.h>
#include <string.h>

#include "ui.h"

typedef struct {
    Uint8 idle_red;
    Uint8 idle_green;
    Uint8 idle_blue;
    Uint8 active_red;
    Uint8 active_green;
    Uint8 active_blue;
} LedPalette;

static const LedPalette palette[LED_COUNT] = {
    [LED_POWER] = { 18, 60, 18, 80, 255, 80 },
    [LED_CAPS]  = { 60, 30,  0, 255, 160, 30 },
    [LED_KANA]  = { 60, 20, 60, 255, 90, 255 },
    [LED_FDC_A] = { 70, 18, 18, 255, 70, 70 },
    [LED_FDC_B] = { 70, 18, 18, 255, 70, 70 },
    [LED_TAPE]  = { 18, 35, 65, 70, 150, 255 },
    [LED_IDE]   = { 55, 45,  8, 255, 220, 40 },
};

static bool enabled[LED_COUNT];
static bool state[LED_COUNT];
static Uint64 last_ping[LED_COUNT];
static bool mouse_inside;
static float mouse_x;
static float mouse_y;
static bool hover_active;
static char hover_label[32];
static float hover_x;
static float hover_y;

static const char *label_for(LedId id) {
    switch (id) {
        case LED_POWER: return "Power";
        case LED_CAPS:  return "Caps Lock";
        case LED_KANA:  return "Kana";
        case LED_FDC_A: return "Drive A";
        case LED_FDC_B: return "Drive B";
        case LED_TAPE:  return "Cassette";
        case LED_IDE:   return "Sunrise IDE";
        case LED_COUNT: break;
    }
    return "";
}

void leds_init(void) {
    memset(enabled, 0, sizeof(enabled));
    memset(state, 0, sizeof(state));
    memset(last_ping, 0, sizeof(last_ping));
    mouse_inside = false;
    hover_active = false;
    hover_label[0] = '\0';
}

void leds_set_enabled(LedId id, bool value) {
    if ((unsigned)id < LED_COUNT)
        enabled[id] = value;
}

void leds_set_state(LedId id, bool active) {
    if ((unsigned)id < LED_COUNT)
        state[id] = active;
}

void leds_ping(LedId id) {
    if ((unsigned)id < LED_COUNT)
        last_ping[id] = SDL_GetTicks();
}

void leds_set_mouse_position(float x, float y, bool inside) {
    mouse_x = x;
    mouse_y = y;
    mouse_inside = inside;
    if (!inside)
        hover_active = false;
}

void leds_render(SDL_Renderer *renderer, int x, int y, int w, int h) {
    const float led_w = 28.0f;
    const float led_h = 10.0f;
    const float padding = 8.0f;
    int count = 0;
    Uint64 now = SDL_GetTicks();

    ui_fill_rect(renderer, (float)x, (float)y, (float)w, (float)h,
                 18, 18, 18, 255);
    ui_fill_rect(renderer, (float)x, (float)y, (float)w, 1.0f,
                 52, 52, 52, 255);

    for (int i = 0; i < LED_COUNT; ++i)
        if (enabled[i])
            ++count;
    if (!count) {
        hover_active = false;
        return;
    }

    float total_w = (float)count * led_w + (float)(count - 1) * padding;
    float cursor_x = (float)x + ((float)w - total_w) * 0.5f;
    float led_y = (float)y + ((float)h - led_h) * 0.5f;
    hover_active = false;

    for (int i = 0; i < LED_COUNT; ++i) {
        const LedPalette *colors;
        bool active;

        if (!enabled[i])
            continue;
        colors = &palette[i];
        active = state[i] ||
                 (last_ping[i] && now - last_ping[i] < LED_GLOW_MS);
        ui_fill_rect(renderer, cursor_x, led_y, led_w, led_h,
                     active ? colors->active_red : colors->idle_red,
                     active ? colors->active_green : colors->idle_green,
                     active ? colors->active_blue : colors->idle_blue,
                     255);
        ui_draw_rect(renderer, cursor_x, led_y, led_w, led_h, 0, 0, 0);

        if (mouse_inside &&
            mouse_x >= cursor_x && mouse_x < cursor_x + led_w &&
            mouse_y >= led_y && mouse_y < led_y + led_h) {
            snprintf(hover_label, sizeof(hover_label), "%s",
                     label_for((LedId)i));
            hover_x = cursor_x + led_w * 0.5f;
            hover_y = (float)y;
            hover_active = true;
        }
        cursor_x += led_w + padding;
    }
}

void leds_render_hover(SDL_Renderer *renderer) {
    const float font_w = 8.0f;
    const float font_h = 8.0f;
    float box_w;
    float box_x;
    float box_y;

    if (!hover_active || !hover_label[0])
        return;
    box_w = (float)strlen(hover_label) * font_w + 12.0f;
    box_x = hover_x - box_w * 0.5f;
    box_y = hover_y - font_h - 10.0f;
    if (box_x < 2.0f)
        box_x = 2.0f;
    if (box_y < 2.0f)
        box_y = 2.0f;

    ui_fill_rect(renderer, box_x, box_y, box_w, font_h + 8.0f,
                 0, 0, 0, 220);
    ui_draw_rect(renderer, box_x, box_y, box_w, font_h + 8.0f,
                 220, 220, 220);
    ui_draw_text(renderer, box_x + 6.0f, box_y + 4.0f, hover_label,
                 230, 230, 230);
}
