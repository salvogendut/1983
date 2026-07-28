#include "audio.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "msx.h"

void audio_output_init(AudioOutput *audio, bool enabled) {
    SDL_AudioSpec spec = {
        SDL_AUDIO_S16,
        1,
        MSX_AUDIO_SAMPLE_RATE,
    };

    if (!audio)
        return;
    memset(audio, 0, sizeof(*audio));
    if (!enabled)
        return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "audio: SDL_InitSubSystem failed: %s\n",
                SDL_GetError());
        return;
    }
    audio->subsystem = true;
    audio->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!audio->stream) {
        fprintf(stderr, "audio: SDL_OpenAudioDeviceStream failed: %s\n",
                SDL_GetError());
        return;
    }
    if (!SDL_ResumeAudioStreamDevice(audio->stream)) {
        fprintf(stderr, "audio: SDL_ResumeAudioStreamDevice failed: %s\n",
                SDL_GetError());
        SDL_DestroyAudioStream(audio->stream);
        audio->stream = NULL;
    }
}

void audio_output_clear(AudioOutput *audio) {
    if (audio && audio->stream)
        SDL_ClearAudioStream(audio->stream);
}

void audio_output_submit(AudioOutput *audio,
                         const s16 *samples, size_t count) {
    size_t bytes;

    if (!audio || !audio->stream || !samples || !count)
        return;
    bytes = count * sizeof(*samples);
    if (bytes > (size_t)INT_MAX)
        return;
    if (!SDL_PutAudioStreamData(audio->stream, samples, (int)bytes))
        fprintf(stderr, "audio: SDL_PutAudioStreamData failed: %s\n",
                SDL_GetError());
}

void audio_output_quit(AudioOutput *audio) {
    if (!audio)
        return;
    if (audio->stream)
        SDL_DestroyAudioStream(audio->stream);
    if (audio->subsystem)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    memset(audio, 0, sizeof(*audio));
}
