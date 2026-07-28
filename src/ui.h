#pragma once

#include <SDL3/SDL.h>

void ui_fill_rect(SDL_Renderer *renderer, float x, float y, float w, float h,
                  Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);
void ui_draw_rect(SDL_Renderer *renderer, float x, float y, float w, float h,
                  Uint8 red, Uint8 green, Uint8 blue);
void ui_draw_text(SDL_Renderer *renderer, float x, float y, const char *text,
                  Uint8 red, Uint8 green, Uint8 blue);
