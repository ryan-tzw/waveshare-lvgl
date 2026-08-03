#include "framed_uart.h"
#include "network.h"
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

#define PIO_UART_BAUD 115200
#define PIO_UART_PORT_COUNT 4
#define HELLO_INTERVAL_MS 500
#define NEIGHBOR_TIMEOUT_US 1500000 // 1500 ms
#define LINK_STATE_DATABASE_CAPACITY 64
#define LOCAL_LINK_STATE_INDEX 0
#define LINK_STATE_RETRY_INTERVAL_US 250000

typedef struct {
    uint32_t tx_pin;
    uint32_t rx_pin;
} PioUartPinPair;

typedef struct {
    bool observed;
    uint8_t node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    uint32_t boot_id;
    uint32_t remote_port;
    uint64_t last_hello_time_us;
} Neighbor;

typedef struct {
    bool occupied;
    NetworkPacket packet;
    uint8_t known_by_ports;
} LinkStateDatabaseEntry;

typedef struct {
    bool waiting_for_ack;
    bool scan_requested;
    uint8_t pending_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    uint32_t pending_boot_id;
    uint32_t pending_sequence;
    uint64_t next_retry_time_us;
} LinkStateTransmissionState;

static const PioUartPinPair pio_uart_pin_pairs[PIO_UART_PORT_COUNT] = {
    {.tx_pin = 3, .rx_pin = 0},
    {.tx_pin = 4, .rx_pin = 10},
    {.tx_pin = 6, .rx_pin = 5},
    {.tx_pin = 23, .rx_pin = 11}
};

static PioUart pio_uarts[PIO_UART_PORT_COUNT] = {0};
static FramedUart framed_uarts[PIO_UART_PORT_COUNT] = {0};
static NodeIdentity *node_identity = NULL;
static Neighbor neighbors[PIO_UART_PORT_COUNT] = {0};
static LinkStateDatabaseEntry
    link_state_database[LINK_STATE_DATABASE_CAPACITY] = {0};
static LinkStateTransmissionState
    link_state_transmissions[PIO_UART_PORT_COUNT] = {0};
static bool timing_output_enabled = false;
static bool hello_schedule_started = false;
static absolute_time_t next_hello_time;
static uint8_t received_payload[FRAMED_UART_MAX_PAYLOAD_SIZE];

/* ==========================================================================
   Basic packet transmission
   ========================================================================== */

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

static void send_ack(
    FramedUart *framed_uart,
    const NodeIdentity *identity,
    const NetworkPacket *acknowledged_packet
) {
    hard_assert(
        acknowledged_packet->which_payload == NetworkPacket_link_state_tag
    );

    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(
        packet.source_node_id,
        identity->node_id.id,
        sizeof(packet.source_node_id)
    );
    packet.boot_id = identity->boot_id;
    packet.which_payload = NetworkPacket_ack_tag;

    Ack *ack = &packet.payload.ack;
    memcpy(
        ack->acknowledged_node_id,
        acknowledged_packet->source_node_id,
        sizeof(ack->acknowledged_node_id)
    );
    ack->acknowledged_boot_id = acknowledged_packet->boot_id;
    ack->acknowledged_sequence = acknowledged_packet->sequence;

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
}

/* ==========================================================================
   Diagnostic output
   ========================================================================== */

static void print_neighbor(
    const char *event,
    const Neighbor *neighbor,
    uint32_t local_port
) {
    printf("%s on port %lu: ", event, (unsigned long)local_port);
    for (size_t i = 0; i < sizeof(neighbor->node_id); i++) {
        unsigned int node_id_byte = neighbor->node_id[i];
        printf("%02x", node_id_byte);
    }

    printf(
        " (boot %08lx, remote port %lu)\n\n",
        (unsigned long)neighbor->boot_id,
        (unsigned long)neighbor->remote_port
    );
}

static void print_link_state(const NetworkPacket *packet) {
    const LinkState *link_state = &packet->payload.link_state;

    for (size_t i = 0; i < sizeof(packet->source_node_id); i++) {
        unsigned int node_id_byte = packet->source_node_id[i];
        printf("%02x", node_id_byte);
    }

    printf(
        " (boot %08lx, sequence %lu) -> [",
        (unsigned long)packet->boot_id,
        (unsigned long)packet->sequence
    );

    for (size_t i = 0; i < link_state->neighbors_count; i++) {
        const LinkStateNeighbor *neighbor = &link_state->neighbors[i];

        if (i > 0) {
            printf(", ");
        }

        for (size_t j = 0; j < sizeof(neighbor->node_id); j++) {
            unsigned int node_id_byte = neighbor->node_id[j];
            printf("%02x", node_id_byte);
        }

        printf(
            " (%lu->%lu)",
            (unsigned long)neighbor->local_port,
            (unsigned long)neighbor->remote_port
        );
    }

    printf("]\n\n");
}

void network_print_link_state_database(void) {
    size_t entry_count = 0;

    for (
        size_t entry_index = 0;
        entry_index < LINK_STATE_DATABASE_CAPACITY;
        entry_index++
    ) {
        if (link_state_database[entry_index].occupied) {
            entry_count++;
        }
    }

    printf(
        "LINK_STATE database (%lu entries)\n",
        (unsigned long)entry_count
    );

    if (entry_count == 0) {
        printf("(empty)\n\n");
        return;
    }

    for (
        size_t entry_index = 0;
        entry_index < LINK_STATE_DATABASE_CAPACITY;
        entry_index++
    ) {
        const LinkStateDatabaseEntry *entry =
            &link_state_database[entry_index];

        if (entry->occupied) {
            print_link_state(&entry->packet);
        }
    }
}

static void print_ack(
    const char *event,
    const NetworkPacket *packet,
    uint32_t local_port
) {
    const Ack *ack = &packet->payload.ack;

    printf(
        "%s on port %lu from ",
        event,
        (unsigned long)local_port
    );
    for (size_t i = 0; i < sizeof(packet->source_node_id); i++) {
        unsigned int node_id_byte = packet->source_node_id[i];
        printf("%02x", node_id_byte);
    }

    printf(
        " (boot %08lx)\n",
        (unsigned long)packet->boot_id
    );

    printf("Acknowledged: ");
    for (size_t i = 0; i < sizeof(ack->acknowledged_node_id); i++) {
        unsigned int node_id_byte = ack->acknowledged_node_id[i];
        printf("%02x", node_id_byte);
    }

    printf(
        " (boot %08lx, sequence %lu)\n\n",
        (unsigned long)ack->acknowledged_boot_id,
        (unsigned long)ack->acknowledged_sequence
    );
}

/* ==========================================================================
   LINK_STATE database lookup and knowledge
   ========================================================================== */

static LinkStateDatabaseEntry *find_link_state_entry(
    const uint8_t *source_node_id
) {
    for (
        size_t entry_index = 0;
        entry_index < LINK_STATE_DATABASE_CAPACITY;
        entry_index++
    ) {
        LinkStateDatabaseEntry *entry = &link_state_database[entry_index];

        if (!entry->occupied) {
            continue;
        }

        if (memcmp(
            entry->packet.source_node_id,
            source_node_id,
            sizeof(entry->packet.source_node_id)
        ) == 0) {
            return entry;
        }
    }

    return NULL;
}

static LinkStateDatabaseEntry *find_empty_link_state_entry(void) {
    for (
        size_t entry_index = LOCAL_LINK_STATE_INDEX + 1;
        entry_index < LINK_STATE_DATABASE_CAPACITY;
        entry_index++
    ) {
        LinkStateDatabaseEntry *entry = &link_state_database[entry_index];

        if (!entry->occupied) {
            return entry;
        }
    }

    return NULL;
}

static void clear_link_state_knowledge(uint32_t local_port) {
    uint8_t port_mask = (uint8_t)(1u << local_port);
    uint8_t other_ports_mask = (uint8_t)~port_mask;

    for (
        size_t entry_index = 0;
        entry_index < LINK_STATE_DATABASE_CAPACITY;
        entry_index++
    ) {
        LinkStateDatabaseEntry *entry = &link_state_database[entry_index];

        if (entry->occupied) {
            entry->known_by_ports &= other_ports_mask;
        }
    }

    printf(
        "LINK_STATE knowledge cleared for port %lu\n\n",
        (unsigned long)local_port
    );
}

/* ==========================================================================
   LINK_STATE synchronization
   ========================================================================== */

static void reset_link_state_transmission(uint32_t local_port) {
    link_state_transmissions[local_port] = (LinkStateTransmissionState){0};
}

static void request_link_state_scan(uint32_t local_port) {
    link_state_transmissions[local_port].scan_requested = true;
}

static void request_link_state_scans_for_observed_neighbors(void) {
    for (uint32_t local_port = 0; local_port < PIO_UART_PORT_COUNT; local_port++) {
        if (neighbors[local_port].observed) {
            request_link_state_scan(local_port);
        }
    }
}

static bool link_state_versions_match(
    const NetworkPacket *first,
    const NetworkPacket *second
) {
    return first->boot_id == second->boot_id &&
           first->sequence == second->sequence;
}

static bool link_state_version_is_newer(
    const NetworkPacket *received,
    const NetworkPacket *stored
) {
    if (received->boot_id != stored->boot_id) {
        return received->boot_id > stored->boot_id;
    }

    return received->sequence > stored->sequence;
}

static void handle_received_ack(
    const NetworkPacket *packet,
    uint32_t local_port,
    const Neighbor *neighbor
) {
    const Ack *ack = &packet->payload.ack;
    bool sender_node_id_matches = memcmp(
        packet->source_node_id,
        neighbor->node_id,
        sizeof(neighbor->node_id)
    ) == 0;
    bool sender_matches_neighbor =
        neighbor->observed &&
        sender_node_id_matches &&
        packet->boot_id == neighbor->boot_id;

    LinkStateDatabaseEntry *entry = find_link_state_entry(
        ack->acknowledged_node_id
    );
    bool acknowledged_version_matches =
        entry != NULL &&
        entry->packet.boot_id == ack->acknowledged_boot_id &&
        entry->packet.sequence == ack->acknowledged_sequence;

    if (!sender_matches_neighbor || !acknowledged_version_matches) {
        print_ack("ACK ignored", packet, local_port);
        return;
    }

    uint8_t ingress_port_mask = (uint8_t)(1u << local_port);
    entry->known_by_ports |= ingress_port_mask;

    LinkStateTransmissionState *transmission =
        &link_state_transmissions[local_port];
    bool ack_matches_pending_transmission =
        transmission->waiting_for_ack &&
        memcmp(
            ack->acknowledged_node_id,
            transmission->pending_node_id,
            sizeof(transmission->pending_node_id)
        ) == 0 &&
        ack->acknowledged_boot_id == transmission->pending_boot_id &&
        ack->acknowledged_sequence == transmission->pending_sequence;

    if (ack_matches_pending_transmission) {
        transmission->waiting_for_ack = false;
    }

    request_link_state_scan(local_port);
    print_ack("ACK accepted", packet, local_port);
}

static bool store_received_link_state(
    const NetworkPacket *packet,
    uint32_t local_port
) {
    uint8_t ingress_port_mask = (uint8_t)(1u << local_port);
    LinkStateDatabaseEntry *entry = find_link_state_entry(
        packet->source_node_id
    );

    bool source_is_local = memcmp(
        packet->source_node_id,
        node_identity->node_id.id,
        sizeof(packet->source_node_id)
    ) == 0;

    if (source_is_local) {
        if (
            entry != NULL &&
            link_state_versions_match(packet, &entry->packet)
        ) {
            entry->known_by_ports |= ingress_port_mask;
            printf(
                "Duplicate local LINK_STATE received on port %lu\n\n",
                (unsigned long)local_port
            );
        } else {
            printf(
                "Conflicting local LINK_STATE ignored on port %lu\n\n",
                (unsigned long)local_port
            );
        }

        return true;
    }

    if (entry == NULL) {
        entry = find_empty_link_state_entry();

        if (entry == NULL) {
            printf(
                "LINK_STATE database full; packet ignored on port %lu\n\n",
                (unsigned long)local_port
            );
            return false;
        }

        entry->packet = *packet;
        entry->occupied = true;
        entry->known_by_ports = ingress_port_mask;

        printf(
            "Remote LINK_STATE stored on port %lu\n",
            (unsigned long)local_port
        );
        print_link_state(&entry->packet);
        return true;
    }

    if (link_state_versions_match(packet, &entry->packet)) {
        entry->known_by_ports |= ingress_port_mask;
        printf(
            "Duplicate LINK_STATE received on port %lu\n\n",
            (unsigned long)local_port
        );
        return true;
    }

    if (!link_state_version_is_newer(packet, &entry->packet)) {
        printf(
            "Stale LINK_STATE ignored on port %lu\n\n",
            (unsigned long)local_port
        );
        return true;
    }

    entry->packet = *packet;
    entry->known_by_ports = ingress_port_mask;

    printf(
        "Remote LINK_STATE updated on port %lu\n",
        (unsigned long)local_port
    );
    print_link_state(&entry->packet);

    return true;
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

    printf("Local LINK_STATE updated\n");
    print_link_state(&entry->packet);
}

static void send_link_state(
    const char *event,
    const LinkStateDatabaseEntry *entry,
    FramedUart *framed_uart,
    uint32_t local_port
) {
    hard_assert(entry->occupied);
    hard_assert(entry->packet.which_payload == NetworkPacket_link_state_tag);

    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(
        encoded_packet,
        sizeof(encoded_packet)
    );

    bool encoded = pb_encode(
        &stream,
        &NetworkPacket_msg,
        &entry->packet
    );
    hard_assert(encoded);
    hard_assert(framed_uart_send(
        framed_uart,
        encoded_packet,
        stream.bytes_written
    ));

    printf(
        "%s LINK_STATE on port %lu\n",
        event,
        (unsigned long)local_port
    );
    print_link_state(&entry->packet);
}

static void service_link_state_transmission(uint32_t local_port) {
    LinkStateTransmissionState *transmission =
        &link_state_transmissions[local_port];

    if (!neighbors[local_port].observed) {
        transmission->scan_requested = false;
        return;
    }

    uint8_t port_mask = (uint8_t)(1u << local_port);

    if (transmission->waiting_for_ack) {
        LinkStateDatabaseEntry *entry = find_link_state_entry(
            transmission->pending_node_id
        );
        bool pending_version_is_current =
            entry != NULL &&
            entry->packet.boot_id == transmission->pending_boot_id &&
            entry->packet.sequence == transmission->pending_sequence;
        bool pending_version_is_known =
            entry != NULL &&
            (entry->known_by_ports & port_mask) != 0;

        if (!pending_version_is_current || pending_version_is_known) {
            transmission->waiting_for_ack = false;
            transmission->scan_requested = true;
        } else {
            uint64_t current_time_us = time_us_64();

            if (current_time_us >= transmission->next_retry_time_us) {
                send_link_state(
                    "Retrying",
                    entry,
                    &framed_uarts[local_port],
                    local_port
                );
                transmission->next_retry_time_us =
                    time_us_64() + LINK_STATE_RETRY_INTERVAL_US;
            }

            return;
        }
    }

    if (!transmission->scan_requested) {
        return;
    }

    transmission->scan_requested = false;

    for (
        size_t entry_index = 0;
        entry_index < LINK_STATE_DATABASE_CAPACITY;
        entry_index++
    ) {
        LinkStateDatabaseEntry *entry = &link_state_database[entry_index];

        if (!entry->occupied) {
            continue;
        }

        bool entry_is_known = (entry->known_by_ports & port_mask) != 0;
        if (entry_is_known) {
            continue;
        }

        send_link_state(
            "Sending",
            entry,
            &framed_uarts[local_port],
            local_port
        );

        transmission->waiting_for_ack = true;
        memcpy(
            transmission->pending_node_id,
            entry->packet.source_node_id,
            sizeof(transmission->pending_node_id)
        );
        transmission->pending_boot_id = entry->packet.boot_id;
        transmission->pending_sequence = entry->packet.sequence;
        transmission->next_retry_time_us =
            time_us_64() + LINK_STATE_RETRY_INTERVAL_US;
        return;
    }
}

/* ==========================================================================
   Packet reception and neighbour lifecycle
   ========================================================================== */

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
        printf("Failed to decode network packet\n\n");
        return false;
    }

    if (packet.which_payload == NetworkPacket_link_state_tag) {
        bool should_acknowledge = store_received_link_state(
            &packet,
            local_port
        );

        if (should_acknowledge) {
            send_ack(
                &framed_uarts[local_port],
                node_identity,
                &packet
            );
            request_link_state_scans_for_observed_neighbors();
        }

        return false;
    }

    if (packet.which_payload == NetworkPacket_ack_tag) {
        handle_received_ack(&packet, local_port, neighbor);
        return false;
    }

    if (packet.which_payload != NetworkPacket_hello_tag) {
        printf("Unsupported network packet\n\n");
        return false;
    }

    bool node_id_changed = memcmp(
        neighbor->node_id,
        packet.source_node_id,
        sizeof(neighbor->node_id)
    ) != 0;
    bool remote_port_changed =
        neighbor->remote_port != packet.payload.hello.sender_port;
    bool neighbor_discovered = !neighbor->observed;
    bool neighbor_replaced = neighbor->observed && node_id_changed;
    bool neighbor_restarted =
        neighbor->observed &&
        !node_id_changed &&
        neighbor->boot_id != packet.boot_id;
    bool adjacency_changed =
        neighbor_discovered || node_id_changed || remote_port_changed;

    const char *neighbor_event = NULL;
    if (neighbor_discovered) {
        neighbor_event = "Neighbor discovered";
    } else if (neighbor_replaced) {
        neighbor_event = "Neighbor replaced";
    } else if (neighbor_restarted) {
        neighbor_event = "Neighbor restarted";
    } else if (remote_port_changed) {
        neighbor_event = "Neighbor port changed";
    }

    if (neighbor_replaced || neighbor_restarted) {
        clear_link_state_knowledge(local_port);
        reset_link_state_transmission(local_port);
    }

    memcpy(
        neighbor->node_id,
        packet.source_node_id,
        sizeof(neighbor->node_id)
    );
    neighbor->boot_id = packet.boot_id;
    neighbor->remote_port = packet.payload.hello.sender_port;
    neighbor->last_hello_time_us = time_us_64();
    neighbor->observed = true;

    if (neighbor_event != NULL) {
        print_neighbor(neighbor_event, neighbor, local_port);
    }

    if (neighbor_discovered || neighbor_replaced || neighbor_restarted) {
        request_link_state_scan(local_port);
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
        clear_link_state_knowledge(local_port);
        reset_link_state_transmission(local_port);
        neighbor->observed = false;
        adjacency_changed = true;
    }

    return adjacency_changed;
}

/* ==========================================================================
   Public network interface
   ========================================================================== */

void network_init(
    NodeIdentity *identity,
    bool enable_timing_output
) {
    hard_assert(identity != NULL);
    node_identity = identity;
    timing_output_enabled = enable_timing_output;

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

    update_local_link_state(
        &link_state_database[LOCAL_LINK_STATE_INDEX],
        node_identity,
        neighbors
    );
}

void network_update(void) {
    if (!hello_schedule_started) {
        next_hello_time = make_timeout_time_ms(HELLO_INTERVAL_MS);
        hello_schedule_started = true;
    }

    size_t payload_length;
    bool adjacency_changed = false;

    if (time_reached(next_hello_time)) {
        for (
            uint32_t local_port = 0;
            local_port < PIO_UART_PORT_COUNT;
            local_port++
        ) {
            send_hello(
                &framed_uarts[local_port],
                node_identity,
                local_port
            );
        }
        next_hello_time = make_timeout_time_ms(HELLO_INTERVAL_MS);
    }

    for (
        uint32_t local_port = 0;
        local_port < PIO_UART_PORT_COUNT;
        local_port++
    ) {
        while (framed_uart_try_receive(
            &framed_uarts[local_port],
            received_payload,
            sizeof(received_payload),
            &payload_length
        )) {
            uint64_t packet_start_time_us = time_us_64();
            bool port_adjacency_changed = handle_received_packet(
                received_payload,
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
            &link_state_database[LOCAL_LINK_STATE_INDEX],
            node_identity,
            neighbors
        );
        request_link_state_scans_for_observed_neighbors();
    }

    for (
        uint32_t local_port = 0;
        local_port < PIO_UART_PORT_COUNT;
        local_port++
    ) {
        service_link_state_transmission(local_port);
    }
}
