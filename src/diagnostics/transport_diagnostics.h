#pragma once

#include <stdbool.h>

#include "lvgl.h"

/* Creates the per-port UART statistics table within the supplied LVGL parent. */
void transport_diagnostics_init(lv_obj_t *parent);

/* Controls periodic table updates without resetting the cumulative counters. */
void transport_diagnostics_set_enabled(bool enabled);

/* Refreshes the displayed cumulative statistics at a limited rate. */
void transport_diagnostics_update(void);
