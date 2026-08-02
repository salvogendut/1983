#include "gamepad.h"

#include <assert.h>
#include <string.h>

static GamepadSnapshot blank_snapshot(void) {
    GamepadSnapshot snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    return snapshot;
}

static void test_dpad_and_triggers(void) {
    GamepadSnapshot snapshot = blank_snapshot();

    assert(gamepad_snapshot_to_msx(NULL) == 0);
    assert(gamepad_snapshot_to_msx(&snapshot) == 0);

    snapshot.dpad_up = true;
    snapshot.dpad_left = true;
    snapshot.trigger_a = true;
    assert(gamepad_snapshot_to_msx(&snapshot) ==
           (MSX_JOY_UP | MSX_JOY_LEFT | MSX_JOY_TRIGGER_A));

    snapshot = blank_snapshot();
    snapshot.dpad_down = true;
    snapshot.dpad_right = true;
    snapshot.trigger_b = true;
    assert(gamepad_snapshot_to_msx(&snapshot) ==
           (MSX_JOY_DOWN | MSX_JOY_RIGHT | MSX_JOY_TRIGGER_B));

    snapshot.trigger_a = true;
    assert(gamepad_snapshot_to_msx(&snapshot) ==
           (MSX_JOY_DOWN | MSX_JOY_RIGHT |
            MSX_JOY_TRIGGER_A | MSX_JOY_TRIGGER_B));
}

static void test_axes_and_dead_zone(void) {
    GamepadSnapshot snapshot = blank_snapshot();

    snapshot.left_x = GAMEPAD_AXIS_DEAD_ZONE;
    snapshot.left_y = -GAMEPAD_AXIS_DEAD_ZONE;
    assert(gamepad_snapshot_to_msx(&snapshot) == 0);

    snapshot.left_x = GAMEPAD_AXIS_DEAD_ZONE + 1;
    snapshot.left_y = -GAMEPAD_AXIS_DEAD_ZONE - 1;
    assert(gamepad_snapshot_to_msx(&snapshot) ==
           (MSX_JOY_UP | MSX_JOY_RIGHT));

    snapshot.left_x = -GAMEPAD_AXIS_DEAD_ZONE - 1;
    snapshot.left_y = GAMEPAD_AXIS_DEAD_ZONE + 1;
    assert(gamepad_snapshot_to_msx(&snapshot) ==
           (MSX_JOY_DOWN | MSX_JOY_LEFT));
}

static void test_opposing_directions_are_neutral(void) {
    GamepadSnapshot snapshot = blank_snapshot();

    snapshot.dpad_up = true;
    snapshot.dpad_down = true;
    snapshot.dpad_left = true;
    snapshot.dpad_right = true;
    assert(gamepad_snapshot_to_msx(&snapshot) == 0);

    snapshot = blank_snapshot();
    snapshot.dpad_up = true;
    snapshot.left_y = GAMEPAD_AXIS_DEAD_ZONE + 1;
    snapshot.dpad_right = true;
    snapshot.left_x = -GAMEPAD_AXIS_DEAD_ZONE - 1;
    snapshot.trigger_a = true;
    assert(gamepad_snapshot_to_msx(&snapshot) == MSX_JOY_TRIGGER_A);
}

int main(void) {
    test_dpad_and_triggers();
    test_axes_and_dead_zone();
    test_opposing_directions_are_neutral();
    return 0;
}
