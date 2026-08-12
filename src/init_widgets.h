#pragma once

#include <stdbool.h>

#include "init.h"
#include "lvgl.h"

void init_widgets(void);

/* Returns and clears the pending request; repeated presses may be coalesced. */
bool take_link_state_database_print_request(void);
