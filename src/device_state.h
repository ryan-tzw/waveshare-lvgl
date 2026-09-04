#pragma once

#include <stdbool.h>

#include "protocol.pb.h"

typedef enum {
    DEVICE_TYPE_NONE,
    DEVICE_TYPE_BULB,
    DEVICE_TYPE_BATTERY,
    DEVICE_TYPE_SWITCH
} DeviceType;

void       device_state_set_type(DeviceType device_type);
DeviceType device_state_get_type(void);

void device_state_set_switch_on(bool on);
bool device_state_get_switch_on(void);

/* Replaces the output with the currently selected protocol device state. */
void device_state_build_message(DeviceState *message);
