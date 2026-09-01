#pragma once

#include <stdint.h>

#include "lvgl.h"

/* Creates the timing controls and table within the supplied LVGL parent. */
void timing_diagnostics_init(lv_obj_t *parent);

/* Records one completed main-loop iteration and reports accumulated timing. */
void timing_diagnostics_record_loop(
    uint64_t work_time_us,
    uint64_t loop_time_us
);
