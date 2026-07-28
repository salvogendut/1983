#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#include "types.h"

typedef struct {
    SDL_AudioStream *stream;
    bool subsystem;
} AudioOutput;

void audio_output_init(AudioOutput *audio, bool enabled);
void audio_output_clear(AudioOutput *audio);
void audio_output_submit(AudioOutput *audio,
                         const s16 *samples, size_t count);
void audio_output_quit(AudioOutput *audio);
