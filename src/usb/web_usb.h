#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_USB_MAX_PAYLOAD_SIZE 240

void web_usb_set_connected(bool connected);
/*
 * Returns true if a connection change was available
 * When true is returned, `connected` receives the new connection state.
 */
bool web_usb_take_connection_change(bool *connected);
bool web_usb_can_send(void);
bool web_usb_send(const uint8_t *payload, size_t length);
void web_usb_update(void);
