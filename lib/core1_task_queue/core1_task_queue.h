#ifndef CORE1_TASK_QUEUE_H
#define CORE1_TASK_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/multicore.h"
#include "pico/mutex.h"

typedef void (*core1_task_fn_t)(void* arg);

/**
 * Start core 1 task processor.
 */
void core1_task_queue_init(void);

/**
 * Submit a task to run on core 1.
 *
 * Returns false if the queue is full.
 */
bool core1_task_queue_post(core1_task_fn_t fn, void* arg);

#endif