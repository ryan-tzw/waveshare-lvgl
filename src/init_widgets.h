#pragma once

#include "init.h"
#include "lvgl.h"
#include "framed_uart.h"

void init_widgets(FramedUart *framed_uart);
void write_to_label(const char buf[], uint32_t count);
