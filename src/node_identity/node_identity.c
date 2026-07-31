#include "node_identity.h"

#include "pico/assert.h"
#include "pico/rand.h"

bool node_identity_init(NodeIdentity *identity) {
    if (identity == NULL || identity->initialized) {
        return false;
    }

    pico_get_unique_board_id(&identity->node_id);
    identity->boot_id = get_rand_32();
    identity->next_sequence = 0;
    identity->initialized = true;

    return true;
}

uint32_t node_identity_next_sequence(NodeIdentity *identity) {
    hard_assert(identity != NULL);
    hard_assert(identity->initialized);

    uint32_t sequence = identity->next_sequence;
    identity->next_sequence++;

    return sequence;
}
