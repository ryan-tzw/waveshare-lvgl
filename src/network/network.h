#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "link_state_database.h"
#include "node_identity.h"
#include "protocol.pb.h"

/* The NodeIdentity must remain valid while the network is running. */
void network_init(NodeIdentity *node_identity);
void network_update(void);

/* A changed value originates and synchronizes a new local LINK_STATE. */
void network_set_gateway_connected(bool connected);
void network_print_link_state_database(void);
void network_print_gateway_routes(void);

/* Copies an occupied database packet; invalid or empty indices return false. */
bool network_get_link_state_database_packet(
    size_t entry_index,
    NetworkPacket *packet
);

/*
 * Return value: whether a pending database update was available.
 * Output parameter: the updated entry index when the return value is true.
 * Repeated changes to one entry are coalesced until its update is consumed.
 */
bool network_take_link_state_database_update(size_t *entry_index);

/* Clears update notifications without changing packets or UART knowledge. */
void network_clear_link_state_database_updates(void);

/*  Retrieves a packet from the local gateway queue, if one exists */
bool network_take_local_gateway_packet(NetworkPacket *packet);
