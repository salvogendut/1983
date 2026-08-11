/* Stubs for host-only modules excluded from the WASM POC build.
 *
 * The POC only boots an MSX to firmware with video/keyboard/audio, so host
 * device code (SDL window/renderer, notifications, the RS-232C/PTY backend,
 * gamepad input) is replaced by no-op implementations that satisfy the linker.
 * This mirrors 1984's JS1984 POC stubs.c.
 */
#include "types.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "SDL3/SDL.h"

/* ---- SDL runtime functions the core header declares but the POC never
 * calls (the browser supplies video/audio directly). Declared here so the
 * linker is satisfied if any of the compiled core sources reference them. */
const char *SDL_GetError(void) { return ""; }
const char *SDL_GetBasePath(void) { return ""; }
void SDL_DestroyAudioStream(SDL_AudioStream *stream) { (void)stream; }