#include "notify.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

#define NOTIFY_MAX 5
#define NOTIFY_TEXT_MAX 96
#define NOTIFY_TTL_MS 3500
#define NOTIFY_FADE_MS 600

typedef struct {
    char text[NOTIFY_TEXT_MAX];
    int age_ms;
} NotifyEntry;

static NotifyEntry entries[NOTIFY_MAX];
static NotifyMode current_mode = NOTIFY_MODE_SCREEN;

void notify_init(void) {
    for (int i = 0; i < NOTIFY_MAX; ++i)
        entries[i].age_ms = -1;
    current_mode = NOTIFY_MODE_SCREEN;
}

void notify_set_mode(NotifyMode mode) {
    current_mode = mode;
}

static int oldest_entry(void) {
    int oldest = 0;
    for (int i = 1; i < NOTIFY_MAX; ++i)
        if (entries[i].age_ms > entries[oldest].age_ms)
            oldest = i;
    return oldest;
}

void notify_post(const char *fmt, ...) {
    char text[NOTIFY_TEXT_MAX];
    va_list args;
    int slot = -1;

    if (current_mode == NOTIFY_MODE_OFF)
        return;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    if (current_mode == NOTIFY_MODE_CONSOLE) {
        fprintf(stderr, "%s\n", text);
        return;
    }
    for (int i = 0; i < NOTIFY_MAX; ++i) {
        if (entries[i].age_ms < 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        slot = oldest_entry();
    snprintf(entries[slot].text, sizeof(entries[slot].text), "%s", text);
    entries[slot].age_ms = 0;
}

void notify_tick(int elapsed_ms) {
    for (int i = 0; i < NOTIFY_MAX; ++i) {
        if (entries[i].age_ms < 0)
            continue;
        entries[i].age_ms += elapsed_ms;
        if (entries[i].age_ms >= NOTIFY_TTL_MS)
            entries[i].age_ms = -1;
    }
}

void notify_render(SDL_Renderer *renderer, int screen_h) {
    int order[NOTIFY_MAX];
    int count = 0;
    int y;

    if (current_mode != NOTIFY_MODE_SCREEN)
        return;
    for (int i = 0; i < NOTIFY_MAX; ++i)
        if (entries[i].age_ms >= 0)
            order[count++] = i;
    for (int i = 0; i + 1 < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (entries[order[i]].age_ms < entries[order[j]].age_ms) {
                int swap = order[i];
                order[i] = order[j];
                order[j] = swap;
            }
        }
    }

    y = screen_h - 28;
    for (int i = count - 1; i >= 0; --i) {
        NotifyEntry *entry = &entries[order[i]];
        Uint8 alpha = 255;
        int fade_start = NOTIFY_TTL_MS - NOTIFY_FADE_MS;
        size_t text_len = strlen(entry->text);

        if (entry->age_ms > fade_start) {
            int remaining = NOTIFY_TTL_MS - entry->age_ms;
            alpha = (Uint8)((remaining * 255) / NOTIFY_FADE_MS);
        }
        ui_fill_rect(renderer, 10.0f, (float)y,
                     (float)text_len * 8.0f + 16.0f, 22.0f,
                     0, 0, 0, (Uint8)((alpha * 180) / 255));
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 220, 120, alpha);
        SDL_RenderDebugText(renderer, 18.0f, (float)y + 7.0f, entry->text);
        y -= 26;
        if (y < 0)
            break;
    }
}
