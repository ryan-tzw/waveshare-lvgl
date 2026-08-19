#include "neighbor_table.h"
#include "pico/assert.h"
#include <string.h>

#define NEIGHBOR_TABLE_PORT_COUNT 4

static Neighbor neighbors[NEIGHBOR_TABLE_PORT_COUNT] = {0};

static Neighbor *get_neighbor(uint32_t local_port) {
    hard_assert(local_port < NEIGHBOR_TABLE_PORT_COUNT);
    return &neighbors[local_port];
}
void neighbor_table_init(void) {
    memset(neighbors, 0, sizeof(neighbors));
}
const Neighbor *neighbor_table_get(uint32_t local_port) {
    return get_neighbor(local_port);
}

NeighborChanges neighbor_table_process_hello(
    uint32_t local_port,
    const uint8_t node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    uint32_t boot_id,
    uint32_t remote_port,
    uint64_t received_time_us
) {
    hard_assert(node_id != NULL);

    Neighbor *neighbor       = get_neighbor(local_port);
    bool node_id_changed     = memcmp(neighbor->node_id, node_id, sizeof(neighbor->node_id)) != 0;
    bool boot_id_changed     = !node_id_changed && neighbor->boot_id != boot_id;
    bool remote_port_changed = neighbor->remote_port != remote_port;
    NeighborChanges neighbor_changes = NEIGHBOR_CHANGE_NONE;

    if (!neighbor->observed) { neighbor_changes |= NEIGHBOR_CHANGE_CONNECTED; }
    else {
        if      (node_id_changed)     { neighbor_changes |= NEIGHBOR_CHANGE_NODE; }
        else if (boot_id_changed)     { neighbor_changes |= NEIGHBOR_CHANGE_BOOT; }
        if      (remote_port_changed) { neighbor_changes |= NEIGHBOR_CHANGE_REMOTE_PORT; }
    }

    memcpy(neighbor->node_id, node_id, sizeof(neighbor->node_id));
    neighbor->boot_id            = boot_id;
    neighbor->remote_port        = remote_port;
    neighbor->last_hello_time_us = received_time_us;
    neighbor->observed           = true;

    return neighbor_changes;
}

NeighborChanges neighbor_table_check_timeout(
    uint32_t local_port,
    uint64_t current_time_us,
    uint64_t timeout_us
) {
    Neighbor *neighbor = get_neighbor(local_port);

    if (!neighbor->observed) { return NEIGHBOR_CHANGE_NONE; }

    uint64_t elapsed_time_us = current_time_us - neighbor->last_hello_time_us;

    if (elapsed_time_us < timeout_us) { return NEIGHBOR_CHANGE_NONE; }

    neighbor->observed = false;
    return NEIGHBOR_CHANGE_DISCONNECTED;
}
