#include "network_diagnostics.h"
#include "gateway_routes.h"
#include "link_state_database.h"
#include "pico/assert.h"
#include <stdio.h>


/* ==========================================================================
   Diagnostic output
   ========================================================================== */

void network_diagnostics_print_neighbor(
    const char     *event,
    const Neighbor *neighbor,
    uint32_t        local_port
) {
    printf("%s on port %lu: ", event, (unsigned long)local_port);
    for (size_t i = 0; i < sizeof(neighbor->node_id); i++) {
        unsigned int node_id_byte = neighbor->node_id[i];
        printf("%02x", node_id_byte); // format int to 2-digit zero-padded hex
    }

    printf(
        " (boot %08lx, remote port %lu)\n\n", // format long to 8-digit zero-padded hex
        (unsigned long)neighbor->boot_id,
        (unsigned long)neighbor->remote_port
    );
}

void network_diagnostics_print_link_state(const NetworkPacket *packet) {
    const LinkState *link_state = &packet->payload.link_state;

    for (size_t i = 0; i < sizeof(packet->source_node_id); i++) {
        unsigned int node_id_byte = packet->source_node_id[i];
        printf("%02x", node_id_byte);
    }

    printf(
        " (boot %08lx, sequence %lu, gateway %s) -> [",
        (unsigned long)packet->boot_id,
        (unsigned long)packet->sequence,
        link_state->gateway_connected ? "connected" : "disconnected"
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

void network_diagnostics_print_link_state_database(void) {
    size_t entry_count = 0;

    for (size_t entry_index = 0; entry_index < NETWORK_LINK_STATE_DATABASE_CAPACITY; entry_index++) {
        NetworkPacket packet;

        if (link_state_database_get_packet(entry_index, &packet)) {
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

    for (size_t entry_index = 0; entry_index < NETWORK_LINK_STATE_DATABASE_CAPACITY; entry_index++) {
        NetworkPacket packet;

        if (link_state_database_get_packet(entry_index, &packet)) {
            network_diagnostics_print_link_state(&packet);
        }
    }
}

void network_diagnostics_print_gateway_routes(void) {
    size_t route_count = gateway_routes_get_count();

    printf(
        "Gateway routes (%lu %s)\n",
        (unsigned long)route_count,
        route_count == 1 ? "route" : "routes"
    );

    if (route_count == 0) {
        printf("(none)\n\n");
        return;
    }

    for (size_t route_index = 0; route_index < route_count; route_index++) {
        GatewayRoute route;
        hard_assert( gateway_routes_get(route_index, &route) );

        for (size_t i = 0; i < sizeof(route.node_id); i++) {
            unsigned int node_id_byte = route.node_id[i];
            printf("%02x", node_id_byte);
        }

        if (route.is_local) {
            printf(" -> local, 0 hops\n");
        } else {
            printf(
                " -> port %lu, %lu %s\n",
                (unsigned long)route.local_port,
                (unsigned long)route.hop_count,
                route.hop_count == 1 ? "hop" : "hops"
            );
        }
    }

    printf("\n");
}

void network_diagnostics_print_ack(
    const char          *event,
    const NetworkPacket *packet,
    uint32_t             local_port
) {
    const Ack *ack = &packet->payload.ack;

    // outer packet identifies the neighbor that sent the ACK
    printf("%s on port %lu from ", event, (unsigned long)local_port);
    for (size_t i = 0; i < sizeof(packet->source_node_id); i++) {
        unsigned int node_id_byte = packet->source_node_id[i];
        printf("%02x", node_id_byte);
    }

    printf(
        " (boot %08lx)\n",
        (unsigned long)packet->boot_id
    );

    // ACK payload identifies the LINK_STATE packet it acknowledges
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

void network_diagnostics_print_knowledge_cleared(uint32_t local_port) {
    printf(
        "LINK_STATE knowledge cleared for port %lu\n\n",
        (unsigned long)local_port
    );
}
