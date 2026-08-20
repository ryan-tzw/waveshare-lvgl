#include "network_diagnostics.h"
#include "gateway_routes.h"
#include "link_state_database.h"
#include "pico/assert.h"
#include "pico/unique_id.h"
#include <stdio.h>


/* ==========================================================================
   Diagnostic output
   ========================================================================== */

static void print_node_id(const uint8_t node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES]) {
    for (size_t i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        printf("%02x", (unsigned int)node_id[i]);
    }
}

void network_diagnostics_print_neighbor(
    const char     *event,
    const Neighbor *neighbor,
    uint32_t        local_port
) {
    printf("%s on port %lu: ", event, (unsigned long)local_port);
    print_node_id(neighbor->node_id);

    printf(
        " (boot %08lx, remote port %lu)\n\n", // format long to 8-digit zero-padded hex
        (unsigned long)neighbor->boot_id,
        (unsigned long)neighbor->remote_port
    );
}

void network_diagnostics_print_link_state(const NetworkPacket *packet) {
    const LinkState *link_state = &packet->payload.link_state;

    print_node_id(packet->source_node_id);

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

        print_node_id(neighbor->node_id);

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

        print_node_id(route.node_id);

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
    print_node_id(packet->source_node_id);

    printf(
        " (boot %08lx)\n",
        (unsigned long)packet->boot_id
    );

    // ACK payload identifies the LINK_STATE packet it acknowledges
    printf("Acknowledged: ");
    print_node_id(ack->acknowledged_node_id);

    printf(
        " (boot %08lx, sequence %lu)\n\n",
        (unsigned long)ack->acknowledged_boot_id,
        (unsigned long)ack->acknowledged_sequence
    );
}

void network_diagnostics_print_routed_device_state(
    const NetworkPacket *packet,
    uint32_t             local_port
) {
    const RoutedMessage *routed_message = &packet->payload.routed_message;

    printf("Routed DEVICE_STATE received on port %lu\n", (unsigned long)local_port);

    printf("Source: ");
    print_node_id(packet->source_node_id);
    printf(
        " (boot %08lx, sequence %lu)\n",
        (unsigned long)packet->boot_id,
        (unsigned long)packet->sequence
    );

    printf("Destination gateway: ");
    print_node_id(routed_message->destination_gateway_node_id);

    printf(
        "\nRemaining hops: %lu\n",
        (unsigned long)routed_message->remaining_hops
    );

    const char *device_type;
    if (!routed_message->has_device_state) { device_type = "missing"; }
    else {
        switch (routed_message->device_state.which_state) {
            case 0:                        { device_type = "unselected";  } break;
            case DeviceState_bulb_tag:     { device_type = "bulb";        } break;
            case DeviceState_battery_tag:  { device_type = "battery";     } break;
            case DeviceState_switch_tag:   { device_type = "switch";      } break;
            default:                       { device_type = "unsupported"; } break;
        }
    }

    printf("Device type: %s\n", device_type);
}
