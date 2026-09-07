#include "simulator_engine_heap.h"

#include <pico/platform/panic.h>
#include <pico/types.h>

#define MAX_SIMULATOR_ENGINES 64
static bool simulator_engine_heap_malloced[MAX_SIMULATOR_ENGINES] = {false};
static SIMULATOR_ENGINE simulator_engine_heap[MAX_SIMULATOR_ENGINES];
static size_t simulator_engine_next = 0;

SIMULATOR_ENGINE *malloc_simulator_engine() {
  for (size_t i = 0; i < MAX_SIMULATOR_ENGINES; i++) {
    // Find free slot
    if (simulator_engine_heap_malloced[simulator_engine_next]) {
      simulator_engine_next =
          (simulator_engine_next + 1) % MAX_SIMULATOR_ENGINES;
      continue;
    }

    // Mark as malloced
    simulator_engine_heap_malloced[simulator_engine_next] = true;

    return &simulator_engine_heap[simulator_engine_next];
  }
  panic("malloc_simulator_engine: Ran out of space");
}

void free_simulator_engine(SIMULATOR_ENGINE *ptr) {
  size_t index = ptr - simulator_engine_heap;
  if (index < 0 || index >= MAX_SIMULATOR_ENGINES)
    panic("free_simulator_engine: Rcvd unknown ptr");
  simulator_engine_heap_malloced[index] = false;
}