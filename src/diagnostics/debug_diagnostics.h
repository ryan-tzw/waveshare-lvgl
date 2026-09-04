#pragma once

#include <stdbool.h>

#include "lvgl.h"

/* Creates the Debug controls and diagnostic-page carousel within the supplied parent. */
void debug_diagnostics_init(lv_obj_t *parent);

/* Returns and clears the pending request; repeated presses may be coalesced. */
bool debug_diagnostics_take_link_state_database_print_request(void);
