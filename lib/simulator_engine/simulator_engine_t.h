#ifndef SIMULATOR_ENGINE_T_H
#define SIMULATOR_ENGINE_T_H

#include <stdint.h>

#define DIRECTLY_CONNECTED_SIMULATOR_ENGINES_MAX 4
typedef struct SIMULATOR_ENGINE {
  uint64_t id;
  struct SIMULATOR_ENGINE
      *neighbour_simulator_engines[DIRECTLY_CONNECTED_SIMULATOR_ENGINES_MAX];
} SIMULATOR_ENGINE;

#endif