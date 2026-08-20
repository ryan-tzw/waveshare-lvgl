#include "device_state.h"
#include "framed_uart.h"
#include "gateway_routes.h"
#include "link_state_database.h"
#include "link_state_sync.h"
#include "neighbor_table.h"
#include "network.h"
#include "network_diagnostics.h"
#include "node_identity.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pico/time.h"
#include "pio_uart.h"
#include "protocol.pb.h"

#include <stdio.h>
#include <string.h>


/* ==========================================================================
   Network configuration and state
   ========================================================================== */

#define PIO_UART_BAUD                   115200
#define PIO_UART_PORT_COUNT             4
#define HELLO_INTERVAL_MS               500
#define DEVICE_STATE_SEND_INTERVAL_MS   1000
#define NEIGHBOR_TIMEOUT_US             1500000 // 1500 ms

enum {
    NEIGHBOR_ADJACENCY_CHANGE_MASK =
        NEIGHBOR_CHANGE_CONNECTED |
        NEIGHBOR_CHANGE_DISCONNECTED |
        NEIGHBOR_CHANGE_NODE |
        NEIGHBOR_CHANGE_REMOTE_PORT,
    NEIGHBOR_SYNCHRONIZATION_RESET_MASK =
        NEIGHBOR_CHANGE_DISCONNECTED |
        NEIGHBOR_CHANGE_NODE |
        NEIGHBOR_CHANGE_BOOT,
    NEIGHBOR_SYNCHRONIZATION_SCAN_MASK =
        NEIGHBOR_CHANGE_CONNECTED |
        NEIGHBOR_CHANGE_NODE |
        NEIGHBOR_CHANGE_BOOT
};

typedef struct {
    uint32_t tx_pin;
    uint32_t rx_pin;
} PioUartPinPair;

static const PioUartPinPair pio_uart_pin_pairs[PIO_UART_PORT_COUNT] = {
    { .tx_pin = 3,  .rx_pin = 0  },
    { .tx_pin = 4,  .rx_pin = 10 },
    { .tx_pin = 6,  .rx_pin = 5  },
    { .tx_pin = 23, .rx_pin = 11 }
};

static PioUart          pio_uarts[PIO_UART_PORT_COUNT]    = {0};
static FramedUart       framed_uarts[PIO_UART_PORT_COUNT] = {0};
static NodeIdentity     *node_identity                    = NULL;
static bool             local_gateway_connected           = false;
static absolute_time_t  next_hello_time;
static absolute_time_t  next_device_state_send_time;
static uint8_t          received_packet_bytes[FRAMED_UART_MAX_PAYLOAD_SIZE]; // reused while FramedUart ports are sequentially drained

// manually toggle for timing diagnostic output
static bool timing_output_enabled = false;

/* ==========================================================================
   Basic packet transmission
   ========================================================================== */

static void send_hello(FramedUart *framed_uart, NodeIdentity *identity, uint32_t sender_port) {
    uint64_t start_time_us = time_us_64();

    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(packet.source_node_id, identity->node_id.id, sizeof(packet.source_node_id));
    packet.boot_id                   = identity->boot_id;
    packet.which_payload             = NetworkPacket_hello_tag;
    packet.payload.hello.sender_port = sender_port;

    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(encoded_packet, sizeof(encoded_packet));

    hard_assert( pb_encode(&stream, &NetworkPacket_msg, &packet) );
    hard_assert( framed_uart_send(framed_uart, encoded_packet, stream.bytes_written) );

    uint64_t elapsed_time_us = time_us_64() - start_time_us;
    if (timing_output_enabled) {
        printf(
            "HELLO send on port %lu: %llu us\n",
            (unsigned long)sender_port,
            (unsigned long long)elapsed_time_us
        );
    }
}

static void send_ack(FramedUart *framed_uart, const NodeIdentity *identity, const NetworkPacket *acknowledged_packet) {
    hard_assert( acknowledged_packet->which_payload == NetworkPacket_link_state_tag );

    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(packet.source_node_id, identity->node_id.id, sizeof(packet.source_node_id));
    packet.boot_id       = identity->boot_id;
    packet.which_payload = NetworkPacket_ack_tag;

    Ack *ack = &packet.payload.ack;
    memcpy(ack->acknowledged_node_id, acknowledged_packet->source_node_id, sizeof(ack->acknowledged_node_id));
    ack->acknowledged_boot_id  = acknowledged_packet->boot_id;
    ack->acknowledged_sequence = acknowledged_packet->sequence;

    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(encoded_packet, sizeof(encoded_packet));

    hard_assert( pb_encode(&stream, &NetworkPacket_msg, &packet) );
    hard_assert( framed_uart_send(framed_uart, encoded_packet, stream.bytes_written) );
}

static void send_device_state_to_gateway(const GatewayRoute *route, DeviceType device_type) {
    hard_assert(route != NULL);
    hard_assert(!route->is_local);
    hard_assert(route->local_port < PIO_UART_PORT_COUNT);

    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(packet.source_node_id, node_identity->node_id.id, sizeof(packet.source_node_id));
    packet.boot_id       = node_identity->boot_id;
    packet.sequence      = node_identity_next_sequence(node_identity);
    packet.which_payload = NetworkPacket_routed_message_tag;
    
    RoutedMessage *routed_message = &packet.payload.routed_message;
    memcpy(routed_message->destination_gateway_node_id, route->node_id, sizeof(routed_message->destination_gateway_node_id));
    routed_message->remaining_hops   = route->hop_count;
    routed_message->has_device_state = true;

    DeviceState *device_state = &routed_message->device_state;
    switch (device_type) {
        case DEVICE_TYPE_NONE:    { device_state->which_state = 0;                       } break;
        case DEVICE_TYPE_BULB:    { device_state->which_state = DeviceState_bulb_tag;    } break;
        case DEVICE_TYPE_BATTERY: { device_state->which_state = DeviceState_battery_tag; } break;
        case DEVICE_TYPE_SWITCH:  { device_state->which_state = DeviceState_switch_tag;  } break;
    }

    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(encoded_packet, sizeof(encoded_packet));

    hard_assert( pb_encode(&stream, &NetworkPacket_msg, &packet) );
    hard_assert( framed_uart_send(&framed_uarts[route->local_port], encoded_packet, stream.bytes_written) );
}

static void send_device_state_to_remote_gateways(void) {
    DeviceType device_type = device_state_get_type();
    size_t route_count     = gateway_routes_get_count();

    for (size_t route_index = 0; route_index < route_count; route_index++) {
        GatewayRoute route;
        hard_assert( gateway_routes_get(route_index, &route) );

        if (route.is_local) { continue; }
        send_device_state_to_gateway(&route, device_type);
    }
}

static void clear_link_state_knowledge_for_port(uint32_t local_port) {
    link_state_database_clear_port_knowledge(local_port);
    printf(
        "LINK_STATE knowledge cleared for port %lu\n\n",
        (unsigned long)local_port
    );
}

/* ==========================================================================
   LINK_STATE synchronization
   ========================================================================== */

static void request_link_state_scans_for_observed_neighbors(void) {
    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        const Neighbor *neighbor = neighbor_table_get(local_port);
        if (neighbor->observed) {
            link_state_sync_request_scan(local_port);
        }
    }
}

static void handle_received_ack(const NetworkPacket *packet, uint32_t local_port, const Neighbor *neighbor) {
    const Ack *ack = &packet->payload.ack;
    bool sender_node_id_matches = memcmp(packet->source_node_id, neighbor->node_id, sizeof(neighbor->node_id)) == 0;
    bool sender_matches_neighbor =
        neighbor->observed &&
        sender_node_id_matches &&
        packet->boot_id == neighbor->boot_id;

    if (!sender_matches_neighbor || !link_state_sync_process_ack(local_port, ack)) {
        network_diagnostics_print_ack("ACK ignored", packet, local_port);
        return;
    }

    network_diagnostics_print_ack("ACK accepted", packet, local_port);
}

static bool process_received_link_state(const NetworkPacket *packet, uint32_t local_port) {
    LinkStateStoreResult store_result = link_state_database_store_received(packet, node_identity->node_id.id, local_port);

    switch (store_result) {
        case LINK_STATE_STORE_NEW: {
            gateway_routes_recalculate(PIO_UART_PORT_COUNT);
            printf(
                "Remote LINK_STATE stored on port %lu\n",
                (unsigned long)local_port
            );
            network_diagnostics_print_link_state(packet);
        } break;

        case LINK_STATE_STORE_UPDATED: {
            gateway_routes_recalculate(PIO_UART_PORT_COUNT);
            printf(
                "Remote LINK_STATE updated on port %lu\n",
                (unsigned long)local_port
            );
            network_diagnostics_print_link_state(packet);
        } break;

        case LINK_STATE_STORE_DUPLICATE: {
            printf(
                "Duplicate LINK_STATE received on port %lu\n\n",
                (unsigned long)local_port
            );
        } break;

        case LINK_STATE_STORE_STALE: {
            printf(
                "Stale LINK_STATE ignored on port %lu\n\n",
                (unsigned long)local_port
            );
        } break;

        case LINK_STATE_STORE_LOCAL_DUPLICATE: {
            printf(
                "Duplicate local LINK_STATE received on port %lu\n\n",
                (unsigned long)local_port
            );
        } break;

        case LINK_STATE_STORE_LOCAL_CONFLICT: {
            printf(
                "Conflicting local LINK_STATE ignored on port %lu\n\n",
                (unsigned long)local_port
            );
        } break;

        case LINK_STATE_STORE_FULL: {
            printf(
                "LINK_STATE database full; packet ignored on port %lu\n\n",
                (unsigned long)local_port
            );
            return false;
        }
    }

    return true;
}

static void originate_local_link_state(NodeIdentity *identity) {
    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(packet.source_node_id, identity->node_id.id, sizeof(packet.source_node_id));
    packet.boot_id       = identity->boot_id;
    packet.sequence      = node_identity_next_sequence(identity);
    packet.which_payload = NetworkPacket_link_state_tag;

    LinkState *link_state = &packet.payload.link_state;
    link_state->gateway_connected = local_gateway_connected;

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        const Neighbor *neighbor = neighbor_table_get(local_port);

        if (!neighbor->observed) { continue; }

        pb_size_t entry_index = link_state->neighbors_count;
        LinkStateNeighbor *link_state_neighbor = &link_state->neighbors[entry_index];

        memcpy(link_state_neighbor->node_id, neighbor->node_id, sizeof(link_state_neighbor->node_id));
        link_state_neighbor->local_port  = local_port;
        link_state_neighbor->remote_port = neighbor->remote_port;
        link_state->neighbors_count++;
    }

    link_state_database_store_local(&packet);
    gateway_routes_recalculate(PIO_UART_PORT_COUNT);

    printf("Local LINK_STATE updated\n");
    network_diagnostics_print_link_state(&packet);
}

static void send_link_state_for_port_if_needed(uint32_t local_port) {
    NetworkPacket packet;
    const Neighbor *neighbor        = neighbor_table_get(local_port);
    LinkStateSyncAction sync_action = link_state_sync_prepare_packet_for_port_if_needed(local_port, neighbor->observed, time_us_64(), &packet);

    if (sync_action == LINK_STATE_SYNC_NONE) { return; }

    hard_assert( packet.which_payload == NetworkPacket_link_state_tag );

    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(encoded_packet, sizeof(encoded_packet));

    hard_assert( pb_encode(&stream, &NetworkPacket_msg, &packet) );
    hard_assert( framed_uart_send(&framed_uarts[local_port], encoded_packet, stream.bytes_written) );

    printf(
        "%s LINK_STATE on port %lu\n",
        sync_action == LINK_STATE_SYNC_RETRY ? "Retrying" : "Sending",
        (unsigned long)local_port
    );
    network_diagnostics_print_link_state(&packet);
}

/* ==========================================================================
   Packet reception and neighbour event handling
   ========================================================================== */

static const char *neighbor_change_name(NeighborChanges neighbor_changes) {
    if      (neighbor_changes & NEIGHBOR_CHANGE_CONNECTED)    { return "Neighbor discovered"; }
    else if (neighbor_changes & NEIGHBOR_CHANGE_NODE)         { return "Neighbor replaced"; }
    else if (neighbor_changes & NEIGHBOR_CHANGE_BOOT)         { return "Neighbor restarted"; }
    else if (neighbor_changes & NEIGHBOR_CHANGE_REMOTE_PORT)  { return "Neighbor port changed"; }
    else if (neighbor_changes & NEIGHBOR_CHANGE_DISCONNECTED) { return "Neighbor disconnected"; }
    return NULL;
}

static void handle_received_routed_message(const NetworkPacket *packet, uint32_t local_port) {
    const RoutedMessage *routed_message = &packet->payload.routed_message;
    bool destination_is_local = memcmp(
        routed_message->destination_gateway_node_id,
        node_identity->node_id.id,
        sizeof(routed_message->destination_gateway_node_id)
    ) == 0;

    const char *delivery_result;
    if (!destination_is_local) {
        delivery_result = "addressed to another gateway; forwarding is not implemented yet";
    } else if (!local_gateway_connected) {
        delivery_result = "local WebUSB gateway is no longer connected";
    } else {
        delivery_result = "reached the intended local gateway";
    }

    network_diagnostics_print_routed_device_state(packet, local_port, delivery_result);
}

static bool handle_received_packet(const uint8_t *data, size_t length, uint32_t local_port) {
    NetworkPacket packet = NetworkPacket_init_zero;
    pb_istream_t stream  = pb_istream_from_buffer(data, length);

    if (!pb_decode(&stream, &NetworkPacket_msg, &packet)) {
        printf("Failed to decode network packet\n\n");
        return false;
    }

    if (packet.which_payload == NetworkPacket_link_state_tag) {
        bool should_acknowledge = process_received_link_state(&packet, local_port);

        if (should_acknowledge) {
            send_ack(&framed_uarts[local_port], node_identity, &packet);
            request_link_state_scans_for_observed_neighbors();
        }

        return false;
    }

    if (packet.which_payload == NetworkPacket_ack_tag) {
        const Neighbor *neighbor = neighbor_table_get(local_port);
        handle_received_ack(&packet, local_port, neighbor);
        return false;
    }

    if (packet.which_payload == NetworkPacket_routed_message_tag) {
        handle_received_routed_message(&packet, local_port);
        return false;
    }

    if (packet.which_payload != NetworkPacket_hello_tag) {
        printf("Unsupported network packet\n\n");
        return false;
    }

    NeighborChanges neighbor_changes = neighbor_table_process_hello(
        local_port,
        packet.source_node_id,
        packet.boot_id,
        packet.payload.hello.sender_port,
        time_us_64()
    );

    if (neighbor_changes & NEIGHBOR_SYNCHRONIZATION_RESET_MASK) {
        clear_link_state_knowledge_for_port(local_port);
        link_state_sync_reset(local_port);
    }

    const char *neighbor_event_name = neighbor_change_name(neighbor_changes);
    if (neighbor_event_name != NULL) {
        const Neighbor *neighbor = neighbor_table_get(local_port);
        network_diagnostics_print_neighbor(neighbor_event_name, neighbor, local_port);
    }

    if (neighbor_changes & NEIGHBOR_SYNCHRONIZATION_SCAN_MASK) {
        link_state_sync_request_scan(local_port);
    }

    return (neighbor_changes & NEIGHBOR_ADJACENCY_CHANGE_MASK) != 0;
}

static bool disconnect_timed_out_neighbors(void) {
    uint64_t current_time_us = time_us_64();
    bool neighbor_disconnected = false;

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        NeighborChanges neighbor_changes = neighbor_table_check_timeout(local_port, current_time_us, NEIGHBOR_TIMEOUT_US);

        if (!(neighbor_changes & NEIGHBOR_CHANGE_DISCONNECTED)) { continue; }

        const Neighbor *neighbor = neighbor_table_get(local_port);
        network_diagnostics_print_neighbor(neighbor_change_name(neighbor_changes), neighbor, local_port);

        if (neighbor_changes & NEIGHBOR_SYNCHRONIZATION_RESET_MASK) {
            clear_link_state_knowledge_for_port(local_port);
            link_state_sync_reset(local_port);
        }

        if (neighbor_changes & NEIGHBOR_ADJACENCY_CHANGE_MASK) {
            neighbor_disconnected = true;
        }
    }

    return neighbor_disconnected;
}

/* ==========================================================================
   Public network interface
   ========================================================================== */

void network_print_link_state_database(void)         { network_diagnostics_print_link_state_database(); }
void network_print_gateway_routes(void)              { network_diagnostics_print_gateway_routes();      }
void network_clear_link_state_database_updates(void) { link_state_database_clear_updates();             }

bool network_get_link_state_database_packet(size_t entry_index, NetworkPacket *packet) {
    return link_state_database_get_packet(entry_index, packet); }
bool network_take_link_state_database_update(size_t *entry_index) {
    return link_state_database_take_update(entry_index); }

void network_set_gateway_connected(bool connected) {
    hard_assert(node_identity != NULL);
    if (local_gateway_connected == connected) { return; }

    local_gateway_connected = connected;
    originate_local_link_state(node_identity);
    request_link_state_scans_for_observed_neighbors();
}

void network_init(NodeIdentity *identity, bool enable_timing_output) {
    hard_assert(identity != NULL);

    node_identity         = identity;
    timing_output_enabled = enable_timing_output;

    neighbor_table_init();
    link_state_database_init();
    link_state_sync_init();

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        uint32_t tx_pin = pio_uart_pin_pairs[local_port].tx_pin;
        uint32_t rx_pin = pio_uart_pin_pairs[local_port].rx_pin;

        hard_assert(pio_uart_init(&pio_uarts[local_port], tx_pin, rx_pin, PIO_UART_BAUD));
        hard_assert(framed_uart_init(&framed_uarts[local_port], &pio_uarts[local_port]));
    }

    originate_local_link_state(node_identity);
    next_hello_time             = make_timeout_time_ms(HELLO_INTERVAL_MS);
    next_device_state_send_time = make_timeout_time_ms(DEVICE_STATE_SEND_INTERVAL_MS);
}

void network_update(void) {
    size_t payload_length;
    bool adjacency_changed = false;

    if (time_reached(next_hello_time)) {
        for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
            send_hello(&framed_uarts[local_port], node_identity, local_port);
        }
        next_hello_time = make_timeout_time_ms(HELLO_INTERVAL_MS);
    }

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        while (framed_uart_try_receive(
            &framed_uarts[local_port],
            received_packet_bytes,
            sizeof(received_packet_bytes),
            &payload_length
        )) {
            uint64_t packet_start_time_us = time_us_64();

            bool port_adjacency_changed = handle_received_packet(received_packet_bytes, payload_length, local_port);
            if (port_adjacency_changed) { adjacency_changed = true; }

            uint64_t packet_elapsed_time_us = time_us_64() - packet_start_time_us;
            if (timing_output_enabled) {
                printf(
                    "Packet handling on port %lu: %llu us\n",
                    (unsigned long)local_port,
                    (unsigned long long)packet_elapsed_time_us
                );
            }
        }
    }

    if (disconnect_timed_out_neighbors()) { adjacency_changed = true; }

    if (adjacency_changed) {
        originate_local_link_state(node_identity);
        request_link_state_scans_for_observed_neighbors();
    }

    if (time_reached(next_device_state_send_time)) {
        send_device_state_to_remote_gateways();
        next_device_state_send_time = make_timeout_time_ms(DEVICE_STATE_SEND_INTERVAL_MS);
    }

    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        send_link_state_for_port_if_needed(local_port);
    }
}
