#include "link_state_database.h"
#include "pico/assert.h"
#include <string.h>

/* ==========================================================================
   Database state and internal helpers
   ========================================================================== */

typedef struct {
    NetworkPacket packet;
    uint8_t known_by_ports;
    bool occupied;
    bool update_pending;
} LinkStateDatabaseEntry;

static LinkStateDatabaseEntry entries[NETWORK_LINK_STATE_DATABASE_CAPACITY] = {0};

static uint8_t get_port_mask(uint32_t local_port) {
    hard_assert(local_port < 8);
    return (uint8_t)(1u << local_port);
}

static bool versions_match(const NetworkPacket *first, const NetworkPacket *second) {
    return first->boot_id == second->boot_id &&
           first->sequence == second->sequence;
}

static bool received_version_is_newer(const NetworkPacket *received, const NetworkPacket *stored) {
    if (received->boot_id != stored->boot_id) {
        return received->boot_id > stored->boot_id;
    }

    return received->sequence > stored->sequence;
}

static LinkStateDatabaseEntry *find_empty_entry(void) {
    for (size_t i = LINK_STATE_DATABASE_LOCAL_INDEX + 1; i < NETWORK_LINK_STATE_DATABASE_CAPACITY; i++) {
        LinkStateDatabaseEntry *entry = &entries[i];
        if (!entry->occupied) { return entry; }
    }

    return NULL;
}

/* ==========================================================================
   Packet storage and classification
   ========================================================================== */

void link_state_database_init(void) {
    memset(entries, 0, sizeof(entries));
}

void link_state_database_store_local(const NetworkPacket *packet) {
    hard_assert(packet != NULL);
    hard_assert(packet->which_payload == NetworkPacket_link_state_tag);

    LinkStateDatabaseEntry *entry = &entries[LINK_STATE_DATABASE_LOCAL_INDEX];

    entry->packet         = *packet;
    entry->known_by_ports = 0;
    entry->occupied       = true;
    entry->update_pending = true;
}

LinkStateStoreResult link_state_database_store_received(
    const NetworkPacket *packet,
    const uint8_t local_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    uint32_t ingress_port
) {
    hard_assert(packet != NULL);
    hard_assert(local_node_id != NULL);
    hard_assert(packet->which_payload == NetworkPacket_link_state_tag);

    size_t entry_index;
    uint8_t ingress_port_mask = get_port_mask(ingress_port);
    
    bool entry_exists    = link_state_database_find_index(packet->source_node_id, &entry_index);
    bool source_is_local = memcmp(packet->source_node_id, local_node_id, sizeof(packet->source_node_id)) == 0;

    if (source_is_local) {
        if (entry_exists && versions_match(packet, &entries[entry_index].packet)) {
            entries[entry_index].known_by_ports |= ingress_port_mask;
            return LINK_STATE_STORE_LOCAL_DUPLICATE;
        }

        return LINK_STATE_STORE_LOCAL_CONFLICT;
    }

    if (!entry_exists) {
        LinkStateDatabaseEntry *entry = find_empty_entry();

        if (entry == NULL) { return LINK_STATE_STORE_FULL; }

        entry->packet         = *packet;
        entry->known_by_ports = ingress_port_mask;
        entry->occupied       = true;
        entry->update_pending = true;
        return LINK_STATE_STORE_NEW;
    }

    LinkStateDatabaseEntry *entry = &entries[entry_index];

    if (versions_match(packet, &entry->packet)) {
        entry->known_by_ports |= ingress_port_mask;
        return LINK_STATE_STORE_DUPLICATE;
    }

    if (!received_version_is_newer(packet, &entry->packet)) {
        return LINK_STATE_STORE_STALE;
    }

    entry->packet         = *packet;
    entry->known_by_ports = ingress_port_mask;
    entry->update_pending = true;
    return LINK_STATE_STORE_UPDATED;
}

/* ==========================================================================
   Packet lookup
   ========================================================================== */

bool link_state_database_find_index(
    const uint8_t source_node_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES],
    size_t *entry_index
) {
    if (source_node_id == NULL || entry_index == NULL) { return false; }

    for (size_t i = 0; i < NETWORK_LINK_STATE_DATABASE_CAPACITY; i++) {
        const LinkStateDatabaseEntry *entry = &entries[i];

        if (!entry->occupied) { continue; }

        if (memcmp(entry->packet.source_node_id, source_node_id, sizeof(entry->packet.source_node_id)) == 0) {
            *entry_index = i;
            return true;
        }
    }

    return false;
}

bool link_state_database_get_packet(size_t entry_index, NetworkPacket *packet) {
    if (
        packet == NULL ||
        entry_index >= NETWORK_LINK_STATE_DATABASE_CAPACITY ||
        !entries[entry_index].occupied
    ) { return false; }

    *packet = entries[entry_index].packet;
    return true;
}

/* ==========================================================================
   Per-port knowledge
   ========================================================================== */

bool link_state_database_is_known_by_port(size_t entry_index, uint32_t local_port) {
    if (
        entry_index >= NETWORK_LINK_STATE_DATABASE_CAPACITY ||
        !entries[entry_index].occupied
    ) { return false; }

    uint8_t port_mask = get_port_mask(local_port);
    return (entries[entry_index].known_by_ports & port_mask) != 0;
}

void link_state_database_mark_known_by_port(size_t entry_index, uint32_t local_port) {
    hard_assert(entry_index < NETWORK_LINK_STATE_DATABASE_CAPACITY);
    hard_assert(entries[entry_index].occupied);

    entries[entry_index].known_by_ports |= get_port_mask(local_port);
}

void link_state_database_clear_port_knowledge(uint32_t local_port) {
    uint8_t port_mask        = get_port_mask(local_port);
    uint8_t other_ports_mask = (uint8_t)~port_mask;

    for (size_t i = 0; i < NETWORK_LINK_STATE_DATABASE_CAPACITY; i++) {
        LinkStateDatabaseEntry *entry = &entries[i];

        if (entry->occupied) {
            entry->known_by_ports &= other_ports_mask;
        }
    }
}

/* ==========================================================================
   Update notifications
   ========================================================================== */

bool link_state_database_take_update(size_t *entry_index) {
    if (entry_index == NULL) { return false; }

    for (size_t i = 0; i < NETWORK_LINK_STATE_DATABASE_CAPACITY; i++) {
        LinkStateDatabaseEntry *entry = &entries[i];

        if (!entry->update_pending) { continue; }

        entry->update_pending = false;
        *entry_index = i;
        return true;
    }

    return false;
}

void link_state_database_clear_updates(void) {
    for (size_t i = 0; i < NETWORK_LINK_STATE_DATABASE_CAPACITY; i++) {
        entries[i].update_pending = false;
    }
}
