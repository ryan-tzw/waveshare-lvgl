#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/unique_id.h"

typedef struct {
    uint64_t last_hello_time_us;
    uint32_t boot_id;
    uint32_t remote_port;
    uint8_t  node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    bool     observed;
} Neighbor;

typedef uint8_t NeighborChanges;
enum {
    NEIGHBOR_CHANGE_NONE         = 0,
    NEIGHBOR_CHANGE_CONNECTED    = 1u << 0,
    NEIGHBOR_CHANGE_DISCONNECTED = 1u << 1,
    NEIGHBOR_CHANGE_NODE         = 1u << 2,
    NEIGHBOR_CHANGE_BOOT         = 1u << 3,
    NEIGHBOR_CHANGE_REMOTE_PORT  = 1u << 4
};

void neighbor_table_init(void);

/* The pointer remains valid; its contents may change when the port is updated. */
const Neighbor *neighbor_table_get(uint32_t local_port);

/* Stores the HELLO and returns a combination of NEIGHBOR_CHANGE_* flags. */
NeighborChanges neighbor_table_process_hello(
    uint32_t local_port,
    const uint8_t node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    uint32_t boot_id,
    uint32_t remote_port,
    uint64_t received_time_us
);

/* Marks an observed neighbor disconnected once the timeout has elapsed. */
NeighborChanges neighbor_table_check_timeout(
    uint32_t local_port,
    uint64_t current_time_us,
    uint64_t timeout_us
);
