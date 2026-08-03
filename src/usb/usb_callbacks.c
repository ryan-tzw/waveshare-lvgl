#include "init_widgets.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "web_usb.h"

#include <string.h>


// #define URL "localhost:8080"
// const tusb_desc_webusb_url_t desc_url = {
//     .bLength         = 3 + sizeof(URL) - 1,
//     .bDescriptorType = 3, // WEBUSB URL type (https://wicg.github.io/webusb/#webusb-descriptors)
//     .bScheme         = 1, // 0: http, 1: https
//     .url             = URL
// };

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    if (stage != CONTROL_STAGE_SETUP) { return true; }

    switch (request->bmRequestType_bit.type) {
        case TUSB_REQ_TYPE_VENDOR: {
            switch (request->bRequest) {
                case VENDOR_REQUEST_WEBUSB: {
                    // match vendor request in BOS descriptor
                    // Get landing page url
                    // return tud_control_xfer(rhport, request, (void*)(uintptr_t)&desc_url, desc_url.bLength);
                    break;
                }
                case VENDOR_REQUEST_MICROSOFT: {
                    if (request->wIndex == 7) {
                        // Get Microsoft OS 2.0 compatible descriptor
                        uint16_t total_len;
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);
                        return tud_control_xfer(rhport, request, (void*)(uintptr_t)desc_ms_os_20, total_len);
                    } else {
                        return false;
                    }
                }
                default: break;
            }
        } break;

        case TUSB_REQ_TYPE_CLASS: {
            if (request->bRequest == 0x22) {
                // Webserial simulate the CDC_REQUEST_SET_CONTROL_LINE_STATE (0x22) to connect and disconnect.
                web_usb_set_connected(request->wValue != 0);
                // response with status OK
                return tud_control_status(rhport, request);
            }
        } break;

        default: break;
    }

    // stall unknown request
    return false;
}

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    while (tud_vendor_available()) {
        char buf[64];
        const uint32_t count = tud_vendor_read(buf, sizeof(buf));
        write_to_label(buf, count);
    }
}

// Invoked when cdc line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    // if connected, print initial message
    if (dtr && rts) {
        tud_cdc_write_str("\r\nConnected via serial\r\n");
    }
}
