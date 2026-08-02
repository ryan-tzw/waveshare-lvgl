#pragma once

#include <stdbool.h>

#include "init.h"
#include "lvgl.h"

void init_widgets(void);
bool take_link_state_database_print_request(void);
void write_to_label(const char buf[], uint32_t count);
