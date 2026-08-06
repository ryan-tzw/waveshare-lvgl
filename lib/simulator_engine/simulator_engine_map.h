#ifndef SIMULATOR_ENGINE_MAP_H
#define SIMULATOR_ENGINE_MAP_H

#include "simulator_engine_t.h"

typedef uint64_t key_t;

void init_simulator_engine_map();

/**
 * Inserts a pointer into the map.
 *
 * Ownership:
 * - The map stores only the pointer value.
 * - The map does NOT allocate, copy, or free the SIMULATOR_ENGINE object.
 * - The caller retains ownership and is responsible for ensuring the object
 *   remains valid for as long as it may be accessed through the map.
 *
 * If an entry already exists for @p key, its stored pointer is replaced.
 */
void put_into_simulator_engine_map(key_t key,
                                   SIMULATOR_ENGINE *simulator_engine);

/**
 * Returns the pointer associated with @p key, or NULL if the key is not
 * present.
 *
 * Ownership of the returned object is not transferred. The returned pointer
 * must not be freed by the caller unless the caller already owns it.
 */
SIMULATOR_ENGINE *get_from_simulator_engine_map(key_t key);

#endif