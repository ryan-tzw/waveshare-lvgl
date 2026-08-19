#pragma once

typedef enum {
    DEVICE_TYPE_NONE,
    DEVICE_TYPE_BULB,
    DEVICE_TYPE_BATTERY,
    DEVICE_TYPE_SWITCH
} DeviceType;

void       device_state_set_type(DeviceType device_type);
DeviceType device_state_get_type(void);
