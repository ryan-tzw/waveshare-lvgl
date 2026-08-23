#include "web_usb.h"

#include "pico/time.h"
#include "tusb.h"

#include <string.h>


#define WEB_USB_LENGTH_SIZE 2
#define WEB_USB_MAX_FRAME_SIZE (WEB_USB_LENGTH_SIZE + WEB_USB_MAX_PAYLOAD_SIZE)
#define WEB_USB_HEARTBEAT_TIMEOUT_MS 2500

static bool web_usb_connected = false;
static bool connection_change_pending = false;
static absolute_time_t heartbeat_timeout;
static uint8_t tx_buffer[WEB_USB_MAX_FRAME_SIZE] = {0};
static size_t tx_length = 0;
static size_t tx_offset = 0;

void web_usb_set_connected(bool connected) {
    if (connected) {
        heartbeat_timeout = make_timeout_time_ms(WEB_USB_HEARTBEAT_TIMEOUT_MS);
    }

    if (web_usb_connected == connected) {
        return;
    }

    web_usb_connected = connected;
    connection_change_pending = true;
    tx_length = 0;
    tx_offset = 0;
}

bool web_usb_take_connection_change(bool *connected) {
    if (connected == NULL || !connection_change_pending) {
        return false;
    }

    *connected = web_usb_connected;
    connection_change_pending = false;
    return true;
}

bool web_usb_can_send(void) {
    return web_usb_connected && tx_length == 0;
}

bool web_usb_send(const uint8_t *payload, size_t length) {
    if (!web_usb_can_send()) { return false; }
    if (length > WEB_USB_MAX_PAYLOAD_SIZE) { return false; }
    if (length > 0 && payload == NULL) { return false; }

    uint16_t payload_length = (uint16_t)length;
    uint8_t low_byte = (uint8_t)payload_length;
    uint8_t high_byte = (uint8_t)(payload_length >> 8);

    tx_buffer[0] = low_byte;
    tx_buffer[1] = high_byte;

    if (length > 0) {
        memcpy(&tx_buffer[WEB_USB_LENGTH_SIZE], payload, length);
    }

    tx_length = WEB_USB_LENGTH_SIZE + length;
    tx_offset = 0;
    return true;
}

void web_usb_update(void) {
    if (!web_usb_connected) { return; }

    if (time_reached(heartbeat_timeout)) {
        web_usb_set_connected(false);
        return;
    }

    if (tx_length == 0) { return; }

    uint32_t available_space = tud_vendor_write_available();
    if (available_space == 0) { return; }

    size_t remaining_length = tx_length - tx_offset;
    size_t write_length = remaining_length;

    if (write_length > available_space) {
        write_length = available_space;
    }

    uint32_t bytes_written = tud_vendor_write(&tx_buffer[tx_offset], write_length);
    tx_offset += bytes_written;

    if (bytes_written > 0) {
        tud_vendor_write_flush();
    }

    if (tx_offset == tx_length) {
        tx_length = 0;
        tx_offset = 0;
    }
}
