#pragma once

#include <stdbool.h>

#include "node_identity.h"

/* The NodeIdentity must remain valid while the network is running. */
void network_init(
    NodeIdentity *node_identity,
    bool timing_output_enabled
);
void network_update(void);
void network_print_link_state_database(void);
