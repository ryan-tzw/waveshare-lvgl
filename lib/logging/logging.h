
#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Logging levels:
 *
 * 0 - disabled
 * 1 - error
 * 2 - warning
 * 3 - info
 * 4 - debug
 */
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

void safe_printf(const char *fmt, ...);

#if LOG_LEVEL >= LOG_LEVEL_ERROR

#define LOG_ERROR(fmt, ...) safe_printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

#else

#define LOG_ERROR(fmt, ...)                                                    \
  do {                                                                         \
  } while (0)

#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN

#define LOG_WARN(fmt, ...) safe_printf("[WARN] " fmt "\n", ##__VA_ARGS__)

#else

#define LOG_WARN(fmt, ...)                                                     \
  do {                                                                         \
  } while (0)

#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO

#define LOG_INFO(fmt, ...) safe_printf("[INFO] " fmt "\n", ##__VA_ARGS__)

#else

#define LOG_INFO(fmt, ...)                                                     \
  do {                                                                         \
  } while (0)

#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG

#define LOG_DEBUG(fmt, ...) safe_printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

#else

#define LOG_DEBUG(fmt, ...)                                                    \
  do {                                                                         \
  } while (0)

#endif

#endif // LOGGING_H