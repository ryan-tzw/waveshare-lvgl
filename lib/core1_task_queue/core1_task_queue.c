#include "core1_task_queue.h"

#define CORE1_QUEUE_SIZE 16

typedef struct {
    core1_task_fn_t fn;
    void* arg;
} task_item_t;


static task_item_t queue[CORE1_QUEUE_SIZE];

auto_init_mutex(head_mutex);
static volatile uint32_t head = 0;
// No need for mutex as only 1 thread (core1_worker) modifies tail
static volatile uint32_t tail = 0;

static void core1_worker(void) {
    while (true) {
        while (head == tail) {
            // wait for work
            tight_loop_contents();
        }
        task_item_t task = queue[tail];
        tail = (tail + 1) % CORE1_QUEUE_SIZE;
        if (task.fn) {
            task.fn(task.arg);
        }
    }
}


void core1_task_queue_init(void) {
    multicore_launch_core1(core1_worker);
}

bool core1_task_queue_post(core1_task_fn_t fn, void *arg) {
    mutex_enter_blocking(&head_mutex);
    uint32_t next =
        (head + 1) % CORE1_QUEUE_SIZE;

    if (next == tail) {
        // queue full
        printf("core1_task_queue_post: queue full\n");
        mutex_exit(&head_mutex);
        return false;
    }
    // printf("core1_task_queue_post: %d, %u, %u (size, tail, next)\n", CORE1_QUEUE_SIZE, tail, next);

    queue[head].fn = fn;
    queue[head].arg = arg;

    head = next;
    mutex_exit(&head_mutex);
    return true;
}