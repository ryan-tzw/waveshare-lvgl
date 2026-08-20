#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pico/unique_id.h"

typedef struct {
    uint8_t node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    uint32_t local_port;
    uint32_t hop_count;
    bool is_local;
} GatewayRoute;

void gateway_routes_recalculate(uint32_t port_count);
size_t gateway_routes_get_count(void);

/* Copies one calculated route; invalid indices and null outputs return false. */
bool gateway_routes_get(size_t route_index, GatewayRoute *route);

/* Finds the calculated route to one gateway; null arguments and missing routes return false. */
bool gateway_routes_find_by_node_id(
    const uint8_t gateway_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    GatewayRoute *route
);
