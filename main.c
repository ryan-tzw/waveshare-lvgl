#include <stdio.h>
#include "LCD_2in.h"
#include "CST816D.h"
// #include "QMI8658.h"
#include "init.h"
#include "tusb.h"


#define URL "localhost:8080"

const tusb_desc_webusb_url_t desc_url = {
    .bLength         = 3 + sizeof(URL) - 1,
    .bDescriptorType = 3, // WEBUSB URL type (https://wicg.github.io/webusb/#webusb-descriptors)
    .bScheme         = 0, // 0: http, 1: https
    .url             = URL
};

static bool web_serial_connected = false;

int main (void) {
    if (DEV_Module_Init() != 0) { return -1; } 

    /* Init LCD */
    Scan_dir = VERTICAL;
    LCD_2IN_Init(Scan_dir);
    LCD_2IN_Clear(WHITE);
    DEV_SET_PWM(60);

    /* Init touch screen */ 
    CST816D_init(CST816D_Point_Mode);
    
    /* Init IMU */
    // QMI8658_init();

    /* Init LVGL */
    init_lvgl();
    init_widgets();

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    while (1) {
        tud_task(); // tinyusb device task
        lv_task_handler();
        DEV_Delay_ms(5); 
    }

    DEV_Module_Exit();
    return 0;
}
