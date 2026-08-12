#pragma once

#include "node_identity.h"

/*
 * Call repeatedly after tud_task() and before web_usb_update().
 * The initialized NodeIdentity must remain valid while the gateway is running.
 */
void gateway_update(const NodeIdentity *identity);
