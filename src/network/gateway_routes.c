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
static bool visited[NETWORK_LINK_STATE_DATABASE_CAPACITY];
static GatewayRouteTraversal queue[NETWORK_LINK_STATE_DATABASE_CAPACITY];

/* ==========================================================================
   Reciprocal link validation
   ========================================================================== */

static bool link_has_reciprocal_observation(
    const NetworkPacket     *source_packet,
    const LinkStateNeighbor *source_neighbor,
    const NetworkPacket     *neighbor_packet,
    uint32_t port_count
) {
    if (
        source_neighbor->local_port >= port_count ||
        source_neighbor->remote_port >= port_count
    ) { return false; }

    const LinkState *neighbor_link_state = &neighbor_packet->payload.link_state;

    for (size_t neighbor_index = 0; neighbor_index < neighbor_link_state->neighbors_count; neighbor_index++) {
        const LinkStateNeighbor *reciprocal_neighbor = &neighbor_link_state->neighbors[neighbor_index];

        bool node_id_matches = memcmp(
            reciprocal_neighbor->node_id,
            source_packet->source_node_id,
            sizeof(reciprocal_neighbor->node_id)
        ) == 0;
        bool ports_match =
            reciprocal_neighbor->local_port == source_neighbor->remote_port &&
            reciprocal_neighbor->remote_port == source_neighbor->local_port;

        if (node_id_matches && ports_match) { return true; }
    }

    return false;
}

/* ==========================================================================
   Route calculation
   ========================================================================== */

void gateway_routes_recalculate(uint32_t port_count) {
    memset(visited, 0, sizeof(visited));
    memset(gateway_routes, 0, sizeof(gateway_routes));
    gateway_route_count = 0;

    NetworkPacket local_packet;
    if (!link_state_database_get_packet(LINK_STATE_DATABASE_LOCAL_INDEX, &local_packet)) { return; }

    size_t next_queue_index = 0;
    size_t queue_length     = 1;
    queue[0] = (GatewayRouteTraversal){
        .database_index = LINK_STATE_DATABASE_LOCAL_INDEX,
        .first_local_port = 0,
        .hop_count = 0
    };
    visited[LINK_STATE_DATABASE_LOCAL_INDEX] = true;

    while (next_queue_index < queue_length) {
        GatewayRouteTraversal current = queue[next_queue_index];
        next_queue_index++;

        NetworkPacket current_packet;
        bool current_entry_exists = link_state_database_get_packet(current.database_index, &current_packet);
        hard_assert(current_entry_exists);

        const LinkState *current_link_state = &current_packet.payload.link_state;

        if (current_link_state->gateway_connected) {
            GatewayRoute *route = &gateway_routes[gateway_route_count];
            memcpy(route->node_id, current_packet.source_node_id, sizeof(route->node_id));
            route->local_port = current.first_local_port;
            route->hop_count  = current.hop_count;
            route->is_local   = current.database_index == LINK_STATE_DATABASE_LOCAL_INDEX;
            gateway_route_count++;
        }

        for (size_t neighbor_index = 0; neighbor_index < current_link_state->neighbors_count; neighbor_index++) {
            const LinkStateNeighbor *neighbor = &current_link_state->neighbors[neighbor_index];
            size_t neighbor_database_index;

            if (!link_state_database_find_index(neighbor->node_id, &neighbor_database_index)) { continue; }
            if (visited[neighbor_database_index]) { continue; }

            NetworkPacket neighbor_packet;
            bool neighbor_entry_exists = link_state_database_get_packet(neighbor_database_index, &neighbor_packet);
            hard_assert(neighbor_entry_exists);

            if (!link_has_reciprocal_observation(&current_packet, neighbor, &neighbor_packet, port_count)) { continue; }

            uint32_t first_local_port = current.first_local_port;
            if (current.database_index == LINK_STATE_DATABASE_LOCAL_INDEX) {
                first_local_port = neighbor->local_port;
            }

            visited[neighbor_database_index] = true;
            queue[queue_length] = (GatewayRouteTraversal){
                .database_index = neighbor_database_index,
                .first_local_port = first_local_port,
                .hop_count = current.hop_count + 1
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
