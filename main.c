#include "gateway.h"
#include "init.h"
#include "init_widgets.h"
#include "network.h"
#include "node_identity.h"
#include "pico/time.h"
#include "timing_diagnostics.h"
#include "transport_diagnostics.h"
#include "tusb.h"
#include "web_usb.h"


static NodeIdentity node_identity = {0};

int main(void) {
    hard_assert(node_identity_init(&node_identity));
    if (DEV_Module_Init() != 0) { return -1; }

    network_init(&node_identity);

    /* Init LCD */
    Scan_dir = VERTICAL;
    LCD_2IN_Init(Scan_dir);
    LCD_2IN_Clear(WHITE);
    DEV_SET_PWM(60);

    /* Init touch screen */
    CST816D_init(CST816D_Point_Mode);

    init_lvgl();
    init_widgets();

    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    uint64_t loop_start_time_us = time_us_64();

    while (1) {
        if (take_link_state_database_print_request()) {
            network_print_link_state_database();
            network_print_gateway_routes();
        }

        network_update();

        tud_task(); // tinyusb device task
        gateway_update(&node_identity);
        web_usb_update();
        tud_cdc_write_flush();
        transport_diagnostics_update();
        lv_task_handler();

        uint64_t work_time_us = time_us_64() - loop_start_time_us;
        DEV_Delay_ms(5);
        uint64_t loop_time_us = time_us_64() - loop_start_time_us;

        timing_diagnostics_record_loop(work_time_us, loop_time_us);

        loop_start_time_us = time_us_64();
    }

    DEV_Module_Exit();
    return 0;
}
