#pragma once

#include "neighbor_table.h"
#include "protocol.pb.h"
#include <stdint.h>

void network_diagnostics_print_neighbor            (const char *event, const Neighbor *neighbor, uint32_t local_port);
void network_diagnostics_print_link_state          (const NetworkPacket *packet);
void network_diagnostics_print_link_state_database (void);
void network_diagnostics_print_gateway_routes      (void);
void network_diagnostics_print_ack                 (const char *event, const NetworkPacket *packet, uint32_t local_port);
