#pragma once

#include "init.h"
#include "lvgl.h"
#include "pio_uart.h"

void init_widgets(PioUart *test_uart);
void write_to_label(const char buf[], uint32_t count);
