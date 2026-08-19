#include "link_state_sync.h"
#include "link_state_database.h"
#include "pico/assert.h"
#include <string.h>

#define LINK_STATE_SYNC_PORT_COUNT 4
#define LINK_STATE_RETRY_INTERVAL_US 250000

/* ==========================================================================
   Synchronization state
   ========================================================================== */

typedef struct {
    uint64_t next_retry_time_us;
    uint32_t pending_boot_id;
    uint32_t pending_sequence;
    uint8_t  pending_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    bool     waiting_for_ack;
    bool     scan_requested;
} LinkStateSyncState;

static LinkStateSyncState port_states[LINK_STATE_SYNC_PORT_COUNT] = {0};

static LinkStateSyncState *get_port_state(uint32_t local_port) {
    hard_assert(local_port < LINK_STATE_SYNC_PORT_COUNT);
    return &port_states[local_port];
}

void link_state_sync_init(void) {
    memset(port_states, 0, sizeof(port_states));
}

void link_state_sync_reset(uint32_t local_port) {
    *get_port_state(local_port) = (LinkStateSyncState){0};
}

void link_state_sync_request_scan(uint32_t local_port) {
    get_port_state(local_port)->scan_requested = true;
}

/* ==========================================================================
   ACK processing
   ========================================================================== */

bool link_state_sync_process_ack(uint32_t local_port, const Ack *ack) {
    if (ack == NULL) { return false; }

    size_t entry_index;
    NetworkPacket acknowledged_packet;
    bool entry_exists =
        link_state_database_find_index(ack->acknowledged_node_id, &entry_index) &&
        link_state_database_get_packet(entry_index, &acknowledged_packet);
    bool version_matches =
        entry_exists &&
        acknowledged_packet.boot_id == ack->acknowledged_boot_id &&
        acknowledged_packet.sequence == ack->acknowledged_sequence;

    if (!version_matches) { return false; }

    link_state_database_mark_known_by_port(entry_index, local_port);

    LinkStateSyncState *port_state = get_port_state(local_port);
    bool pending_version_matches =
        port_state->waiting_for_ack &&
        memcmp(ack->acknowledged_node_id, port_state->pending_node_id, sizeof(port_state->pending_node_id)) == 0 &&
        ack->acknowledged_boot_id == port_state->pending_boot_id &&
        ack->acknowledged_sequence == port_state->pending_sequence;

    if (pending_version_matches) { port_state->waiting_for_ack = false; }

    port_state->scan_requested = true;
    return true;
}

/* ==========================================================================
   Transmission scheduling
   ========================================================================== */

LinkStateSyncAction link_state_sync_prepare_packet_for_port_if_needed(
    uint32_t local_port,
    bool neighbor_observed,
    uint64_t current_time_us,
    NetworkPacket *packet_to_send
) {
    if (packet_to_send == NULL) { return LINK_STATE_SYNC_NONE; }

    LinkStateSyncState *port_state = get_port_state(local_port);

    if (!neighbor_observed) {
        port_state->scan_requested = false;
        return LINK_STATE_SYNC_NONE;
    }

    if (port_state->waiting_for_ack) {
        size_t entry_index;
        NetworkPacket pending_packet;
        bool entry_exists =
            link_state_database_find_index(port_state->pending_node_id, &entry_index) &&
            link_state_database_get_packet(entry_index, &pending_packet);
        bool pending_version_is_current =
            entry_exists &&
            pending_packet.boot_id == port_state->pending_boot_id &&
            pending_packet.sequence == port_state->pending_sequence;
        bool pending_version_is_known =
            entry_exists &&
            link_state_database_is_known_by_port(entry_index, local_port);

        if (!pending_version_is_current || pending_version_is_known) {
            port_state->waiting_for_ack = false;
            port_state->scan_requested = true;
        } else {
            if (current_time_us < port_state->next_retry_time_us) { return LINK_STATE_SYNC_NONE; }

            *packet_to_send = pending_packet;
            port_state->next_retry_time_us = current_time_us + LINK_STATE_RETRY_INTERVAL_US;
            return LINK_STATE_SYNC_RETRY;
        }
    }

    if (!port_state->scan_requested) { return LINK_STATE_SYNC_NONE; }

    port_state->scan_requested = false;

    for (size_t entry_index = 0; entry_index < NETWORK_LINK_STATE_DATABASE_CAPACITY; entry_index++) {
        NetworkPacket database_packet;

        if (!link_state_database_get_packet(entry_index, &database_packet)) { continue; }
        if (link_state_database_is_known_by_port(entry_index, local_port)) { continue; }

        port_state->waiting_for_ack = true;
        memcpy(port_state->pending_node_id, database_packet.source_node_id, sizeof(port_state->pending_node_id));
        port_state->pending_boot_id    = database_packet.boot_id;
        port_state->pending_sequence   = database_packet.sequence;
        port_state->next_retry_time_us = current_time_us + LINK_STATE_RETRY_INTERVAL_US;
        *packet_to_send = database_packet;
        return LINK_STATE_SYNC_SEND;
    }

    return LINK_STATE_SYNC_NONE;
}
