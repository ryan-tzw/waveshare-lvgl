#include "device_state.h"

static DeviceType selected_device_type = DEVICE_TYPE_NONE;

void device_state_set_type(DeviceType device_type) {
    selected_device_type = device_type;
}

DeviceType device_state_get_type(void) {
    return selected_device_type;
}
