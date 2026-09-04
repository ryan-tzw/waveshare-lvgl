#include "device_state.h"

#include "pico/assert.h"

static DeviceType selected_device_type = DEVICE_TYPE_NONE;
static bool switch_on = false;

void device_state_set_type(DeviceType device_type) {
    selected_device_type = device_type;
}

DeviceType device_state_get_type(void) {
    return selected_device_type;
}

void device_state_set_switch_on(bool on) {
    switch_on = on;
}

bool device_state_get_switch_on(void) {
    return switch_on;
}

void device_state_build_message(DeviceState *message) {
    hard_assert(message != NULL);

    DeviceState current_state = DeviceState_init_zero;

    switch (selected_device_type) {
        case DEVICE_TYPE_NONE:    { current_state.which_state = 0;                       } break;
        case DEVICE_TYPE_BULB:    { current_state.which_state = DeviceState_bulb_tag;    } break;
        case DEVICE_TYPE_BATTERY: { current_state.which_state = DeviceState_battery_tag; } break;
        case DEVICE_TYPE_SWITCH: {
            current_state.which_state      = DeviceState_switch_tag;
            current_state.state.switch_.on = switch_on;
        } break;
    }

    *message = current_state;
}
