#pragma once

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "msx.h"

#define GAMEPAD_AXIS_DEAD_ZONE 16000

typedef struct {
    Sint16 left_x;
    Sint16 left_y;
    bool dpad_up;
    bool dpad_down;
    bool dpad_left;
    bool dpad_right;
    bool trigger_a;
    bool trigger_b;
} GamepadSnapshot;

typedef struct {
    SDL_Gamepad *device;
} GamepadInput;

u8 gamepad_snapshot_to_msx(const GamepadSnapshot *snapshot);

void gamepad_input_init(GamepadInput *input);
void gamepad_input_destroy(GamepadInput *input);
void gamepad_input_handle_device_event(GamepadInput *input,
                                       const SDL_Event *event);
bool gamepad_input_connected(const GamepadInput *input);
const char *gamepad_input_name(const GamepadInput *input);
u8 gamepad_input_poll(const GamepadInput *input);
