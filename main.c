#include "gateway.h"
#include "init.h"
#include "init_widgets.h"
#include "network.h"
#include "node_identity.h"
#include "pico/time.h"
#include "tusb.h"
#include "web_usb.h"


#define TIMING_REPORT_INTERVAL_MS 1000

static NodeIdentity node_identity = {0};
static const bool timing_output_enabled = false;

int main(void) {
    hard_assert(node_identity_init(&node_identity));
    if (DEV_Module_Init() != 0) { return -1; }

    network_init(&node_identity, timing_output_enabled);

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

    absolute_time_t next_timing_report_time = make_timeout_time_ms(
        TIMING_REPORT_INTERVAL_MS
    );
    uint64_t loop_start_time_us = time_us_64();
    uint64_t total_work_time_us = 0;
    uint64_t maximum_work_time_us = 0;
    uint64_t total_loop_time_us = 0;
    uint64_t maximum_loop_time_us = 0;
    uint32_t loop_count = 0;

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
        lv_task_handler();

        uint64_t work_time_us = time_us_64() - loop_start_time_us;
        DEV_Delay_ms(5);
        uint64_t loop_time_us = time_us_64() - loop_start_time_us;

        total_work_time_us += work_time_us;
        total_loop_time_us += loop_time_us;
        loop_count++;

        if (work_time_us > maximum_work_time_us) {
            maximum_work_time_us = work_time_us;
        }

        if (loop_time_us > maximum_loop_time_us) {
            maximum_loop_time_us = loop_time_us;
        }

        if (time_reached(next_timing_report_time)) {
            uint64_t average_work_time_us = total_work_time_us / loop_count;
            uint64_t average_loop_time_us = total_loop_time_us / loop_count;

            if (timing_output_enabled) {
                printf(
                    "Loop timing: work avg %llu us, work max %llu us, "
                    "total avg %llu us, total max %llu us, loops %lu\n",
                    (unsigned long long)average_work_time_us,
                    (unsigned long long)maximum_work_time_us,
                    (unsigned long long)average_loop_time_us,
                    (unsigned long long)maximum_loop_time_us,
                    (unsigned long)loop_count
                );
            }

            total_work_time_us = 0;
            maximum_work_time_us = 0;
            total_loop_time_us = 0;
            maximum_loop_time_us = 0;
            loop_count = 0;
            next_timing_report_time = make_timeout_time_ms(
                TIMING_REPORT_INTERVAL_MS
            );
        }

        loop_start_time_us = time_us_64();
    }

    DEV_Module_Exit();
    return 0;
}
