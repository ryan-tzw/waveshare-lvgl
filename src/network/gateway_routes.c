#include "gateway_routes.h"

#include "link_state_database.h"
#include "pico/assert.h"
#include "protocol.pb.h"
#include <string.h>


/* ==========================================================================
   Route table state
   ========================================================================== */

typedef struct {
    size_t database_index;
    uint32_t first_local_port;
    uint32_t hop_count;
} GatewayRouteTraversal;

static GatewayRoute gateway_routes[NETWORK_LINK_STATE_DATABASE_CAPACITY] = {0};
static size_t gateway_route_count = 0;
static bool visited_database_entries[NETWORK_LINK_STATE_DATABASE_CAPACITY];
static GatewayRouteTraversal traversal_queue[NETWORK_LINK_STATE_DATABASE_CAPACITY];

/* ==========================================================================
   Reciprocal link validation
   ========================================================================== */

static bool link_has_reciprocal_observation(
    const NetworkPacket     *source_packet,
    const LinkStateNeighbor *source_observation,
    const NetworkPacket     *neighbor_packet,
    uint32_t port_count
) {
    if (source_observation->local_port >= port_count ||
        source_observation->remote_port >= port_count
    ) { return false; }

    const LinkState *neighbor_link_state = &neighbor_packet->payload.link_state;

    for (size_t neighbor_index = 0; neighbor_index < neighbor_link_state->neighbors_count; neighbor_index++) {
        const LinkStateNeighbor *reciprocal_observation = &neighbor_link_state->neighbors[neighbor_index];

        bool node_id_matches = memcmp(
            reciprocal_observation->node_id,
            source_packet->source_node_id,
            sizeof(reciprocal_observation->node_id)
        ) == 0;
        bool ports_match =
            reciprocal_observation->local_port == source_observation->remote_port &&
            reciprocal_observation->remote_port == source_observation->local_port;

        if (node_id_matches && ports_match) { return true; }
    }

    return false;
}

/* ==========================================================================
   Route calculation
   ========================================================================== */

void gateway_routes_recalculate(uint32_t port_count) {
    memset(visited_database_entries, 0, sizeof(visited_database_entries));
    memset(gateway_routes, 0, sizeof(gateway_routes));
    gateway_route_count = 0;

    NetworkPacket local_packet;
    if (!link_state_database_get_packet(LINK_STATE_DATABASE_LOCAL_INDEX, &local_packet)) { return; }

    size_t queue_read_index = 0;
    size_t queue_length     = 1;
    traversal_queue[0] = (GatewayRouteTraversal){
        .database_index   = LINK_STATE_DATABASE_LOCAL_INDEX,
        .first_local_port = 0,
        .hop_count        = 0
    };
    visited_database_entries[LINK_STATE_DATABASE_LOCAL_INDEX] = true;

    while (queue_read_index < queue_length) {
        GatewayRouteTraversal current_traversal = traversal_queue[queue_read_index];
        queue_read_index++;

        NetworkPacket current_packet;
        bool current_entry_exists = link_state_database_get_packet(current_traversal.database_index, &current_packet);
        hard_assert(current_entry_exists);

        const LinkState *current_link_state = &current_packet.payload.link_state;

        if (current_link_state->gateway_connected) {
            GatewayRoute *route = &gateway_routes[gateway_route_count];
            memcpy(route->node_id, current_packet.source_node_id, sizeof(route->node_id));
            route->local_port = current_traversal.first_local_port;
            route->hop_count  = current_traversal.hop_count;
            route->is_local   = current_traversal.database_index == LINK_STATE_DATABASE_LOCAL_INDEX;
            gateway_route_count++;
        }

        for (size_t neighbor_index = 0; neighbor_index < current_link_state->neighbors_count; neighbor_index++) {
            const LinkStateNeighbor *neighbor_observation = &current_link_state->neighbors[neighbor_index];
            size_t neighbor_database_index;

            if (!link_state_database_find_index(neighbor_observation->node_id, &neighbor_database_index)) { continue; }
            if (visited_database_entries[neighbor_database_index]) { continue; }

            NetworkPacket neighbor_packet;
            bool neighbor_entry_exists = link_state_database_get_packet(neighbor_database_index, &neighbor_packet);
            hard_assert(neighbor_entry_exists);

            if (!link_has_reciprocal_observation(&current_packet, neighbor_observation, &neighbor_packet, port_count)) { continue; }

            uint32_t first_local_port = current_traversal.first_local_port;
            if (current_traversal.database_index == LINK_STATE_DATABASE_LOCAL_INDEX) {
                first_local_port = neighbor_observation->local_port;
            }

            visited_database_entries[neighbor_database_index] = true;
            traversal_queue[queue_length] = (GatewayRouteTraversal){
                .database_index   = neighbor_database_index,
                .first_local_port = first_local_port,
                .hop_count        = current_traversal.hop_count + 1
            };
            queue_length++;
        }
    }
}

/* ==========================================================================
   Route read access
   ========================================================================== */

size_t gateway_routes_get_count(void) {
    return gateway_route_count;
}

bool gateway_routes_get(size_t route_index, GatewayRoute *route) {
    if (route == NULL || route_index >= gateway_route_count) { return false; }

    *route = gateway_routes[route_index];
    return true;
}

bool gateway_routes_find_by_node_id(
    const uint8_t gateway_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    GatewayRoute *route
) {
    if (gateway_node_id == NULL || route == NULL) { return false; }

    for (size_t i = 0; i < gateway_route_count; i++) {
        if (memcmp(gateway_routes[i].node_id, gateway_node_id, sizeof(gateway_routes[i].node_id)) != 0) { continue; }

        *route = gateway_routes[i];
        return true;
    }

    return false;
}
