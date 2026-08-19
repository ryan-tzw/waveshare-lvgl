#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/unique_id.h"
#include "protocol.pb.h"

#define NETWORK_LINK_STATE_DATABASE_CAPACITY 64
#define LINK_STATE_DATABASE_LOCAL_INDEX 0

typedef enum {
    LINK_STATE_STORE_NEW,
    LINK_STATE_STORE_UPDATED,
    LINK_STATE_STORE_DUPLICATE,
    LINK_STATE_STORE_STALE,
    LINK_STATE_STORE_LOCAL_DUPLICATE,
    LINK_STATE_STORE_LOCAL_CONFLICT,
    LINK_STATE_STORE_FULL
} LinkStateStoreResult;

void link_state_database_init(void);

/* Stores a new local version in the reserved local entry. */
void link_state_database_store_local(const NetworkPacket *packet);

/* Stores or classifies a received packet and updates ingress knowledge. */
LinkStateStoreResult link_state_database_store_received(
    const NetworkPacket *packet,
    const uint8_t local_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    uint32_t ingress_port
);

/* Finds an occupied entry by the source node ID. */
bool link_state_database_find_index(
    const uint8_t source_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    size_t *entry_index
);

/* Copies an occupied packet; invalid or empty indices return false. */
bool link_state_database_get_packet(
    size_t entry_index,
    NetworkPacket *packet
);

bool link_state_database_is_known_by_port(
    size_t entry_index,
    uint32_t local_port
);

void link_state_database_mark_known_by_port(
    size_t entry_index,
    uint32_t local_port
);

void link_state_database_clear_port_knowledge(uint32_t local_port);

/* Returns and clears the first pending entry index, if one exists. */
bool link_state_database_take_update(size_t *entry_index);

/* Clears pending updates without changing stored packets or UART knowledge. */
void link_state_database_clear_updates(void);
