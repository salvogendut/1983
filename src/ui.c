#include "ui.h"

void ui_fill_rect(SDL_Renderer *renderer, float x, float y, float w, float h,
                  Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha) {
    SDL_FRect rect = { x, y, w, h };
    SDL_SetRenderDrawBlendMode(renderer,
        alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderFillRect(renderer, &rect);
}

void ui_draw_rect(SDL_Renderer *renderer, float x, float y, float w, float h,
                  Uint8 red, Uint8 green, Uint8 blue) {
    SDL_FRect rect = { x, y, w, h };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderRect(renderer, &rect);
}

void ui_draw_text(SDL_Renderer *renderer, float x, float y, const char *text,
                  Uint8 red, Uint8 green, Uint8 blue) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderDebugText(renderer, x, y, text);
}
