#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pico/unique_id.h"

typedef struct {
    pico_unique_board_id_t node_id;
    uint32_t boot_id;
    uint32_t next_sequence;
    bool initialized;
} NodeIdentity;

/* A NodeIdentity must be zero-initialized before first use. */
bool node_identity_init(NodeIdentity *identity);
uint32_t node_identity_next_sequence(NodeIdentity *identity);
