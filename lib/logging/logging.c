#include "logging.h"

#include "pico/mutex.h"
#include <stdio.h>

auto_init_mutex(print_mutex);

void safe_printf(const char *fmt, ...) {
  va_list args;

  mutex_enter_blocking(&print_mutex);

  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  mutex_exit(&print_mutex);
}
