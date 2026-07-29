#include "gamepad.h"

#include <string.h>

static void gamepad_input_open_first(GamepadInput *input) {
    SDL_JoystickID *ids;
    int count = 0;

    if (!input || input->device)
        return;
    ids = SDL_GetGamepads(&count);
    if (ids && count > 0)
        input->device = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

u8 gamepad_snapshot_to_msx(const GamepadSnapshot *snapshot) {
    bool up;
    bool down;
    bool left;
    bool right;
    u8 pressed = 0;

    if (!snapshot)
        return 0;
    up = snapshot->dpad_up ||
         snapshot->left_y < -GAMEPAD_AXIS_DEAD_ZONE;
    down = snapshot->dpad_down ||
           snapshot->left_y > GAMEPAD_AXIS_DEAD_ZONE;
    left = snapshot->dpad_left ||
           snapshot->left_x < -GAMEPAD_AXIS_DEAD_ZONE;
    right = snapshot->dpad_right ||
            snapshot->left_x > GAMEPAD_AXIS_DEAD_ZONE;

    /* Opposing directions on one axis settle at the electrical neutral. */
    if (up != down)
        pressed |= up ? MSX_JOY_UP : MSX_JOY_DOWN;
    if (left != right)
        pressed |= left ? MSX_JOY_LEFT : MSX_JOY_RIGHT;
    if (snapshot->trigger_a)
        pressed |= MSX_JOY_TRIGGER_A;
    if (snapshot->trigger_b)
        pressed |= MSX_JOY_TRIGGER_B;
    return pressed;
}

void gamepad_input_init(GamepadInput *input) {
    if (!input)
        return;
    memset(input, 0, sizeof(*input));
    gamepad_input_open_first(input);
}

void gamepad_input_destroy(GamepadInput *input) {
    if (!input)
        return;
    if (input->device)
        SDL_CloseGamepad(input->device);
    input->device = NULL;
}

void gamepad_input_handle_device_event(GamepadInput *input,
                                       const SDL_Event *event) {
    if (!input || !event)
        return;
    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        if (!input->device)
            input->device = SDL_OpenGamepad(event->gdevice.which);
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED &&
               input->device &&
               SDL_GetGamepadID(input->device) == event->gdevice.which) {
        SDL_CloseGamepad(input->device);
        input->device = NULL;
        gamepad_input_open_first(input);
    }
}

bool gamepad_input_connected(const GamepadInput *input) {
    return input && input->device;
}

const char *gamepad_input_name(const GamepadInput *input) {
    const char *name;

    if (!gamepad_input_connected(input))
        return "None";
    name = SDL_GetGamepadName(input->device);
    return name && name[0] ? name : "SDL gamepad";
}

u8 gamepad_input_poll(const GamepadInput *input) {
    GamepadSnapshot snapshot;

    if (!gamepad_input_connected(input))
        return 0;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.left_x =
        SDL_GetGamepadAxis(input->device, SDL_GAMEPAD_AXIS_LEFTX);
    snapshot.left_y =
        SDL_GetGamepadAxis(input->device, SDL_GAMEPAD_AXIS_LEFTY);
    snapshot.dpad_up =
        SDL_GetGamepadButton(input->device, SDL_GAMEPAD_BUTTON_DPAD_UP);
    snapshot.dpad_down =
        SDL_GetGamepadButton(input->device, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    snapshot.dpad_left =
        SDL_GetGamepadButton(input->device, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    snapshot.dpad_right =
        SDL_GetGamepadButton(input->device, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    snapshot.trigger_a =
        SDL_GetGamepadButton(input->device, SDL_GAMEPAD_BUTTON_SOUTH);
    snapshot.trigger_b =
        SDL_GetGamepadButton(input->device, SDL_GAMEPAD_BUTTON_EAST);
    return gamepad_snapshot_to_msx(&snapshot);
}
