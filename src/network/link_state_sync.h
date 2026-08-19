#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "protocol.pb.h"

typedef enum {
    LINK_STATE_SYNC_NONE,
    LINK_STATE_SYNC_SEND,
    LINK_STATE_SYNC_RETRY
} LinkStateSyncAction;

void link_state_sync_init(void);
void link_state_sync_reset(uint32_t local_port);
void link_state_sync_request_scan(uint32_t local_port);

/* Processes an ACK only when it identifies an exact current database version. */
bool link_state_sync_process_ack(uint32_t local_port, const Ack *ack);

/*
 * Advance one port's stop-and-wait synchronization state. When a new packet
 * or retry is ready, copy it to packet and return the corresponding action.
 * LINK_STATE_SYNC_NONE means packet was left unchanged.
 */
LinkStateSyncAction link_state_sync_prepare_packet_for_port_if_needed(
    uint32_t local_port,
    bool neighbor_observed,
    uint64_t current_time_us,
    NetworkPacket *packet
);
