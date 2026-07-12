#pragma once

#include "pico/stdlib.h"
#include "DEV_Config.h"
#include "LCD_2in.h"
#include "CST816D.h"
#include "lvgl.h"

#define DISP_HOR_RES 240
#define DISP_VER_RES 320

void init_lvgl(void);