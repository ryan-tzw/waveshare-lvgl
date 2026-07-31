#pragma once

#include <stdbool.h>

#include "init.h"
#include "lvgl.h"

void init_widgets(void);
bool take_uart_test_send_request(void);
void write_to_label(const char buf[], uint32_t count);
