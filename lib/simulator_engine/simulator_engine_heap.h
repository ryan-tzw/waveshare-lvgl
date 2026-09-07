#ifndef SIMULATOR_ENGINE_HEAP_H
#define SIMULATOR_ENGINE_HEAP_H

#include "simulator_engine_t.h"

SIMULATOR_ENGINE *malloc_simulator_engine();

void free_simulator_engine(SIMULATOR_ENGINE *ptr);

#endif