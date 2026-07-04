#include "init.h"
#include "tusb.h"
#include "usb_descriptors.h"

// #define URL "localhost:8080"
// const tusb_desc_webusb_url_t desc_url = {
//     .bLength         = 3 + sizeof(URL) - 1,
//     .bDescriptorType = 3, // WEBUSB URL type (https://wicg.github.io/webusb/#webusb-descriptors)
//     .bScheme         = 1, // 0: http, 1: https
//     .url             = URL
// };

static bool web_usb_connected = false;

int main (void) {
    if (DEV_Module_Init() != 0) { return -1; } 

    /* Init LCD */
    Scan_dir = VERTICAL;
    LCD_2IN_Init(Scan_dir);
    LCD_2IN_Clear(WHITE);
    DEV_SET_PWM(60);

    /* Init touch screen */ 
    CST816D_init(CST816D_Point_Mode);
    
    /* Init IMU */
    // QMI8658_init();

    /* Init LVGL */
    init_lvgl();
    init_widgets();

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    while (1) {
        tud_task(); // tinyusb device task
        tud_cdc_write_flush();
        lv_task_handler();
        DEV_Delay_ms(5); 
    }

    DEV_Module_Exit();
    return 0;
}


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
                web_usb_connected = (request->wValue != 0);

                if (web_usb_connected) {
                    tud_vendor_write_str("\r\nWebUSB interface connected\r\n");
                    tud_vendor_write_flush();
                } 
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

void tud_cdc_rx_cb(uint8_t idx) {
    while (tud_cdc_available()) {
        char buf[64];
        const uint32_t count = tud_cdc_read(buf, sizeof(buf));
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