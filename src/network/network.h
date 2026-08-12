#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "node_identity.h"
#include "protocol.pb.h"

#define NETWORK_LINK_STATE_DATABASE_CAPACITY 64

/* The NodeIdentity must remain valid while the network is running. */
void network_init(
    NodeIdentity *node_identity,
    bool timing_output_enabled
);
void network_update(void);
void network_set_gateway_connected(bool connected);
void network_print_link_state_database(void);
bool network_get_link_state_database_packet(
    size_t entry_index,
    NetworkPacket *packet
);
bool network_take_link_state_database_update(size_t *entry_index);
void network_clear_link_state_database_updates(void);
