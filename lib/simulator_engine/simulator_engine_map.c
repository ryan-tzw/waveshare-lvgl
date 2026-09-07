#include "simulator_engine_map.h"

#include "khash.h"
#include "logging.h"
#include <pico/platform/panic.h>

KHASH_MAP_INIT_INT(simulator_engine_map, struct SIMULATOR_ENGINE *);
static khash_t(simulator_engine_map) * simulator_engines;

#define INITIAL_CAPACITY 64
#define MAX_LOAD_FACTOR 0.5

void init_simulator_engine_map() {
  simulator_engines = kh_init(simulator_engine_map);
  kh_resize(simulator_engine_map, simulator_engines,
            INITIAL_CAPACITY); // High enough prevents resizing
}

void put_into_simulator_engine_map(key_t key,
                                   SIMULATOR_ENGINE *simulator_engine) {
  int ret;
  khint_t k = kh_put(simulator_engine_map, simulator_engines, key, &ret);
  if (ret >= 0) {
    kh_value(simulator_engines, k) = simulator_engine;
  } else {
    panic("put_into_simulator_engine_map: Cannot insert into map");
  }

  size_t size = kh_size(simulator_engines);
  size_t buckets = kh_n_buckets(simulator_engines);
  double load_factor = (double)size / buckets;
  if (load_factor > MAX_LOAD_FACTOR) {
    LOG_WARN("put_into_simulator_engine_map: High load; size=%zu buckets=%zu "
             "load=%.2f",
             size, buckets, load_factor);
  }
}

SIMULATOR_ENGINE *get_from_simulator_engine_map(key_t key) {
  khint_t k = kh_get(simulator_engine_map, simulator_engines, key);
  if (k == kh_end(simulator_engines)) {
    return NULL;
  }
  return kh_value(simulator_engines, k);
}