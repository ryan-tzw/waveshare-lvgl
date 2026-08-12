#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_USB_MAX_PAYLOAD_SIZE 240

void web_usb_set_connected(bool connected);
/*
 * Return value: whether a connection change was available.
 * Output parameter: the new connection state when the return value is true.
 */
bool web_usb_take_connection_change(bool *connected);

/* Returns whether WebUSB is connected and no previous message is pending. */
bool web_usb_can_send(void);

/*
 * Copies one message into the internal transmit buffer. Returns false when
 * disconnected, busy, oversized, or given invalid arguments.
 */
bool web_usb_send(const uint8_t *payload, size_t length);

/* Call repeatedly from the main loop to transmit a queued message. */
void web_usb_update(void);
