#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Creates the timing table within the supplied LVGL parent. */
void timing_diagnostics_init(lv_obj_t *parent);

/* Controls table updates without stopping timing sample collection. */
void timing_diagnostics_set_enabled(bool enabled);

/* Records one completed main-loop iteration and reports accumulated timing. */
void timing_diagnostics_record_loop(
    uint64_t work_time_us,
    uint64_t loop_time_us
);
