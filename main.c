#include "init.h"
#include "init_widgets.h"
#include "framed_uart.h"
#include "node_identity.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pico/time.h"
#include "pio_uart.h"
#include "protocol.pb.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include <string.h>

#define PIO_UART_TX_PIN 4
#define PIO_UART_RX_PIN 5
#define PIO_UART_BAUD 115200
#define PIO_UART_PORT 0
#define HELLO_INTERVAL_MS 1000
#define TIMING_REPORT_INTERVAL_MS 1000

// #define URL "localhost:8080"
// const tusb_desc_webusb_url_t desc_url = {
//     .bLength         = 3 + sizeof(URL) - 1,
//     .bDescriptorType = 3, // WEBUSB URL type (https://wicg.github.io/webusb/#webusb-descriptors)
//     .bScheme         = 1, // 0: http, 1: https
//     .url             = URL
// };

static bool web_usb_connected = false;
static PioUart test_uart = {0};
static FramedUart test_framed_uart = {0};
static NodeIdentity node_identity = {0};
static const bool timing_output_enabled = false;

static void send_hello(
    FramedUart *framed_uart,
    NodeIdentity *identity,
    uint32_t sender_port
) {
    uint64_t start_time_us = time_us_64();

    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(
        packet.source_node_id,
        identity->node_id.id,
        sizeof(packet.source_node_id)
    );
    packet.boot_id = identity->boot_id;
    packet.sequence = node_identity_next_sequence(identity);
    packet.which_payload = NetworkPacket_hello_tag;
    packet.payload.hello.sender_port = sender_port;

    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(
        encoded_packet,
        sizeof(encoded_packet)
    );

    bool encoded = pb_encode(&stream, &NetworkPacket_msg, &packet);
    hard_assert(encoded);
    hard_assert(framed_uart_send(
        framed_uart,
        encoded_packet,
        stream.bytes_written
    ));

    uint64_t elapsed_time_us = time_us_64() - start_time_us;
    if (timing_output_enabled) {
        printf(
            "HELLO send: %llu us\n",
            (unsigned long long)elapsed_time_us
        );
    }
}

static void handle_received_packet(const uint8_t *data, size_t length) {
    NetworkPacket packet = NetworkPacket_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, length);

    bool decoded = pb_decode(&stream, &NetworkPacket_msg, &packet);
    if (!decoded) {
        printf("Failed to decode network packet\n");
        return;
    }

    if (packet.which_payload != NetworkPacket_hello_tag) {
        printf("Unsupported network packet\n");
        return;
    }

    printf("Received HELLO\n");
    printf("Node ID: ");
    for (size_t i = 0; i < sizeof(packet.source_node_id); i++) {
        unsigned int node_id_byte = packet.source_node_id[i];
        printf("%02x", node_id_byte);
    }

    printf("\nBoot ID: %08lx\n", (unsigned long)packet.boot_id);
    printf("Sequence: %lu\n", (unsigned long)packet.sequence);
    printf(
        "Sender port: %lu\n",
        (unsigned long)packet.payload.hello.sender_port
    );
}

int main (void) {
    if (DEV_Module_Init() != 0) { return -1; } 
    hard_assert(node_identity_init(&node_identity));
    hard_assert(
        pio_uart_init(
            &test_uart,
            PIO_UART_TX_PIN,
            PIO_UART_RX_PIN,
            PIO_UART_BAUD
        )
    );
    hard_assert(framed_uart_init(&test_framed_uart, &test_uart));

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

    absolute_time_t next_hello_time = make_timeout_time_ms(HELLO_INTERVAL_MS);
    uint8_t payload[FRAMED_UART_MAX_PAYLOAD_SIZE];
    size_t payload_length;

    absolute_time_t next_timing_report_time = make_timeout_time_ms(TIMING_REPORT_INTERVAL_MS);
    uint64_t loop_start_time_us = time_us_64();
    uint64_t total_work_time_us = 0;
    uint64_t maximum_work_time_us = 0;
    uint64_t total_loop_time_us = 0;
    uint64_t maximum_loop_time_us = 0;
    uint32_t loop_count = 0;

    while (1) {
        if (time_reached(next_hello_time)) {
            send_hello(
                &test_framed_uart,
                &node_identity,
                PIO_UART_PORT
            );
            next_hello_time = make_timeout_time_ms(HELLO_INTERVAL_MS);
        }

        if (take_hello_send_request()) {
            send_hello(
                &test_framed_uart,
                &node_identity,
                PIO_UART_PORT
            );
        }

        while (framed_uart_try_receive(
            &test_framed_uart,
            payload,
            sizeof(payload),
            &payload_length
        )) {
            uint64_t packet_start_time_us = time_us_64();
            handle_received_packet(payload, payload_length);
            uint64_t packet_elapsed_time_us =
                time_us_64() - packet_start_time_us;
            if (timing_output_enabled) {
                printf(
                    "Packet handling: %llu us\n",
                    (unsigned long long)packet_elapsed_time_us
                );
            }
        }

        tud_task(); // tinyusb device task
        tud_cdc_write_flush();
        lv_task_handler();

        uint64_t work_time_us = time_us_64() - loop_start_time_us;
        DEV_Delay_ms(5);
        uint64_t loop_time_us = time_us_64() - loop_start_time_us;

        total_work_time_us += work_time_us;
        total_loop_time_us += loop_time_us;
        loop_count++;

        if (work_time_us > maximum_work_time_us) {
            maximum_work_time_us = work_time_us;
        }

        if (loop_time_us > maximum_loop_time_us) {
            maximum_loop_time_us = loop_time_us;
        }

        if (time_reached(next_timing_report_time)) {
            uint64_t average_work_time_us = total_work_time_us / loop_count;
            uint64_t average_loop_time_us = total_loop_time_us / loop_count;

            if (timing_output_enabled) {
                printf(
                    "Loop timing: work avg %llu us, work max %llu us, "
                    "total avg %llu us, total max %llu us, loops %lu\n",
                    (unsigned long long)average_work_time_us,
                    (unsigned long long)maximum_work_time_us,
                    (unsigned long long)average_loop_time_us,
                    (unsigned long long)maximum_loop_time_us,
                    (unsigned long)loop_count
                );
            }

            total_work_time_us = 0;
            maximum_work_time_us = 0;
            total_loop_time_us = 0;
            maximum_loop_time_us = 0;
            loop_count = 0;
            next_timing_report_time = make_timeout_time_ms(
                TIMING_REPORT_INTERVAL_MS
            );
        }

        loop_start_time_us = time_us_64();
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
