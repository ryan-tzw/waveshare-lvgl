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


#define PIO_UART_BAUD 115200
#define PIO_UART_PORT_COUNT 4
#define HELLO_INTERVAL_MS 500
#define NEIGHBOR_TIMEOUT_US 1500000 // 1500 ms
#define TIMING_REPORT_INTERVAL_MS 1000

// #define URL "localhost:8080"
// const tusb_desc_webusb_url_t desc_url = {
//     .bLength         = 3 + sizeof(URL) - 1,
//     .bDescriptorType = 3, // WEBUSB URL type (https://wicg.github.io/webusb/#webusb-descriptors)
//     .bScheme         = 1, // 0: http, 1: https
//     .url             = URL
// };

typedef struct {
    uint32_t tx_pin;
    uint32_t rx_pin;
} PioUartPinPair;

typedef struct {
    bool observed;
    uint8_t node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    uint32_t boot_id;
    uint32_t remote_port;
    uint32_t sequence;
    uint64_t last_hello_time_us;
} Neighbor;

typedef struct {
    bool occupied;
    NetworkPacket packet;
    uint8_t known_by_ports;
} LinkStateDatabaseEntry;

static const PioUartPinPair pio_uart_pin_pairs[PIO_UART_PORT_COUNT] = {
    {.tx_pin = 3, .rx_pin = 0},
    {.tx_pin = 4, .rx_pin = 10},
    {.tx_pin = 6, .rx_pin = 5},
    {.tx_pin = 23, .rx_pin = 11}
};

static bool web_usb_connected = false;
static PioUart pio_uarts[PIO_UART_PORT_COUNT] = {0};
static FramedUart framed_uarts[PIO_UART_PORT_COUNT] = {0};
static NodeIdentity node_identity = {0};
static Neighbor neighbors[PIO_UART_PORT_COUNT] = {0};
static LinkStateDatabaseEntry local_link_state_entry = {0};
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
            "HELLO send on port %lu: %llu us\n",
            (unsigned long)sender_port,
            (unsigned long long)elapsed_time_us
        );
    }
}

static void print_neighbor(
    const char *event,
    const Neighbor *neighbor,
    uint32_t local_port
) {
    printf("%s\n", event);
    printf("Local port: %lu\n", (unsigned long)local_port);

    printf("Node ID: ");
    for (size_t i = 0; i < sizeof(neighbor->node_id); i++) {
        unsigned int node_id_byte = neighbor->node_id[i];
        printf("%02x", node_id_byte);
    }

    printf("\nBoot ID: %08lx\n", (unsigned long)neighbor->boot_id);
    printf("Remote port: %lu\n", (unsigned long)neighbor->remote_port);
    printf("Sequence: %lu\n", (unsigned long)neighbor->sequence);
}

static void print_local_link_state(
    const LinkStateDatabaseEntry *entry
) {
    const NetworkPacket *packet = &entry->packet;
    const LinkState *link_state = &packet->payload.link_state;

    printf("Local LINK_STATE updated\n");
    printf("Origin node ID: ");
    for (size_t i = 0; i < sizeof(packet->source_node_id); i++) {
        unsigned int node_id_byte = packet->source_node_id[i];
        printf("%02x", node_id_byte);
    }

    printf("\nBoot ID: %08lx\n", (unsigned long)packet->boot_id);
    printf("Sequence: %lu\n", (unsigned long)packet->sequence);
    printf(
        "Neighbor count: %lu\n",
        (unsigned long)link_state->neighbors_count
    );

    for (size_t i = 0; i < link_state->neighbors_count; i++) {
        const LinkStateNeighbor *neighbor = &link_state->neighbors[i];

        printf("Neighbor %lu node ID: ", (unsigned long)i);
        for (size_t j = 0; j < sizeof(neighbor->node_id); j++) {
            unsigned int node_id_byte = neighbor->node_id[j];
            printf("%02x", node_id_byte);
        }

        printf(
            "\nNeighbor %lu local port: %lu\n",
            (unsigned long)i,
            (unsigned long)neighbor->local_port
        );
        printf(
            "Neighbor %lu remote port: %lu\n",
            (unsigned long)i,
            (unsigned long)neighbor->remote_port
        );
    }
}

static void update_local_link_state(
    LinkStateDatabaseEntry *entry,
    NodeIdentity *identity,
    const Neighbor *current_neighbors
) {
    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(
        packet.source_node_id,
        identity->node_id.id,
        sizeof(packet.source_node_id)
    );
    packet.boot_id = identity->boot_id;
    packet.sequence = node_identity_next_sequence(identity);
    packet.which_payload = NetworkPacket_link_state_tag;

    LinkState *link_state = &packet.payload.link_state;

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        const Neighbor *neighbor = &current_neighbors[local_port];

        if (!neighbor->observed) {
            continue;
        }

        pb_size_t entry_index = link_state->neighbors_count;
        LinkStateNeighbor *link_state_neighbor =
            &link_state->neighbors[entry_index];

        memcpy(
            link_state_neighbor->node_id,
            neighbor->node_id,
            sizeof(link_state_neighbor->node_id)
        );
        link_state_neighbor->local_port = local_port;
        link_state_neighbor->remote_port = neighbor->remote_port;
        link_state->neighbors_count++;
    }

    entry->packet = packet;
    entry->occupied = true;
    entry->known_by_ports = 0;

    print_local_link_state(entry);
}

static bool handle_received_packet(
    const uint8_t *data,
    size_t length,
    uint32_t local_port,
    Neighbor *neighbor
) {
    NetworkPacket packet = NetworkPacket_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, length);

    bool decoded = pb_decode(&stream, &NetworkPacket_msg, &packet);
    if (!decoded) {
        printf("Failed to decode network packet\n");
        return false;
    }

    if (packet.which_payload != NetworkPacket_hello_tag) {
        printf("Unsupported network packet\n");
        return false;
    }

    bool node_id_changed = memcmp(
        neighbor->node_id,
        packet.source_node_id,
        sizeof(neighbor->node_id)
    ) != 0;
    bool remote_port_changed =
        neighbor->remote_port != packet.payload.hello.sender_port;
    bool adjacency_changed =
        !neighbor->observed || node_id_changed || remote_port_changed;

    const char *neighbor_event = NULL;
    if (!neighbor->observed) {
        neighbor_event = "Neighbor discovered";
    } else if (node_id_changed) {
        neighbor_event = "Neighbor replaced";
    } else if (neighbor->boot_id != packet.boot_id) {
        neighbor_event = "Neighbor restarted";
    } else if (remote_port_changed) {
        neighbor_event = "Neighbor port changed";
    }

    memcpy(
        neighbor->node_id,
        packet.source_node_id,
        sizeof(neighbor->node_id)
    );
    neighbor->boot_id = packet.boot_id;
    neighbor->remote_port = packet.payload.hello.sender_port;
    neighbor->sequence = packet.sequence;
    neighbor->last_hello_time_us = time_us_64();
    neighbor->observed = true;

    if (neighbor_event != NULL) {
        print_neighbor(neighbor_event, neighbor, local_port);
    }

    return adjacency_changed;
}

static bool check_neighbor_timeouts(void) {
    uint64_t current_time_us = time_us_64();
    bool adjacency_changed = false;

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        Neighbor *neighbor = &neighbors[local_port];

        if (!neighbor->observed) {
            continue;
        }

        uint64_t elapsed_time_us =
            current_time_us - neighbor->last_hello_time_us;

        if (elapsed_time_us < NEIGHBOR_TIMEOUT_US) {
            continue;
        }

        print_neighbor("Neighbor disconnected", neighbor, local_port);
        neighbor->observed = false;
        adjacency_changed = true;
    }

    return adjacency_changed;
}

int main (void) {
    if (DEV_Module_Init() != 0) { return -1; } 
    hard_assert(node_identity_init(&node_identity));

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        uint32_t tx_pin = pio_uart_pin_pairs[local_port].tx_pin;
        uint32_t rx_pin = pio_uart_pin_pairs[local_port].rx_pin;

        hard_assert(
            pio_uart_init(
                &pio_uarts[local_port],
                tx_pin,
                rx_pin,
                PIO_UART_BAUD
            )
        );
        hard_assert(
            framed_uart_init(
                &framed_uarts[local_port],
                &pio_uarts[local_port]
            )
        );
    }

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
        bool adjacency_changed = false;

        if (time_reached(next_hello_time)) {
            for (
                uint32_t local_port = 0;
                local_port < PIO_UART_PORT_COUNT;
                local_port++
            ) {
                send_hello(
                    &framed_uarts[local_port],
                    &node_identity,
                    local_port
                );
            }
            next_hello_time = make_timeout_time_ms(HELLO_INTERVAL_MS);
        }

        if (take_hello_send_request()) {
            for (
                uint32_t local_port = 0;
                local_port < PIO_UART_PORT_COUNT;
                local_port++
            ) {
                send_hello(
                    &framed_uarts[local_port],
                    &node_identity,
                    local_port
                );
            }
        }

        for (
            uint32_t local_port = 0;
            local_port < PIO_UART_PORT_COUNT;
            local_port++
        ) {
            while (framed_uart_try_receive(
                &framed_uarts[local_port],
                payload,
                sizeof(payload),
                &payload_length
            )) {
                uint64_t packet_start_time_us = time_us_64();
                bool port_adjacency_changed = handle_received_packet(
                    payload,
                    payload_length,
                    local_port,
                    &neighbors[local_port]
                );
                if (port_adjacency_changed) {
                    adjacency_changed = true;
                }

                uint64_t packet_elapsed_time_us =
                    time_us_64() - packet_start_time_us;
                if (timing_output_enabled) {
                    printf(
                        "Packet handling on port %lu: %llu us\n",
                        (unsigned long)local_port,
                        (unsigned long long)packet_elapsed_time_us
                    );
                }
            }
        }

        if (check_neighbor_timeouts()) {
            adjacency_changed = true;
        }

        if (adjacency_changed) {
            update_local_link_state(
                &local_link_state_entry,
                &node_identity,
                neighbors
            );
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
