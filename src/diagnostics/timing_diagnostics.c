/*
 * Collects main-loop timing samples and presents periodic summaries through
 * the LVGL timing diagnostics page.
 */

#include "timing_diagnostics.h"

#include "init.h"
#include "pico/time.h"

#include <stdbool.h>
#include <stdio.h>


#define TIMING_REPORT_INTERVAL_MS 1000

static lv_obj_t *td_table;

static bool initialized                           = false;
static bool display_enabled                       = false;
static absolute_time_t next_report_time;
static uint64_t total_work_time_us                = 0;
static uint64_t total_loop_time_us                = 0;
static uint64_t maximum_loop_time_us              = 0;
static uint32_t loop_count                        = 0;
static uint32_t peak_work_share_tenths_percent    = 0;

static void reset_timing_diagnostics_table(void);
static void update_timing_diagnostics(
    uint32_t average_work_share_tenths_percent,
    uint32_t peak_work_share_tenths_percent,
    uint64_t average_loop_time_us,
    uint64_t maximum_loop_time_us,
    uint32_t sample_count
);

void timing_diagnostics_init(lv_obj_t *parent) {
    hard_assert(parent != NULL);
    hard_assert(!initialized);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text          (title, "Timing");
    lv_obj_set_style_text_font (title, &lv_font_montserrat_24, 0);
    lv_obj_align               (title, LV_ALIGN_TOP_MID, 0, 32);

    td_table = lv_table_create(parent);
    lv_obj_set_size             (td_table, DISP_HOR_RES - 16, 144);
    lv_obj_align                (td_table, LV_ALIGN_CENTER, 0, 32);
    lv_obj_clear_flag           (td_table, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font  (td_table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor    (td_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver    (td_table, 4, LV_PART_ITEMS);
    lv_table_set_col_width      (td_table, 0, 92);
    lv_table_set_col_width      (td_table, 1, 68);
    lv_table_set_col_width      (td_table, 2, 64);
    lv_table_set_cell_value     (td_table, 0, 0, "Metric");
    lv_table_set_cell_value     (td_table, 0, 1, "Average");
    lv_table_set_cell_value     (td_table, 0, 2, "Peak");
    lv_table_set_cell_value     (td_table, 1, 0, "Work share");
    lv_table_set_cell_value     (td_table, 2, 0, "Loop (ms)");
    lv_table_set_cell_value     (td_table, 3, 0, "Samples");
    reset_timing_diagnostics_table();

    next_report_time = make_timeout_time_ms(TIMING_REPORT_INTERVAL_MS);
    initialized      = true;
}

void timing_diagnostics_set_enabled(bool enabled) {
    hard_assert(initialized);

    display_enabled = enabled;
    if (display_enabled) { reset_timing_diagnostics_table(); }
}

void timing_diagnostics_record_loop(uint64_t work_time_us, uint64_t loop_time_us) {
    hard_assert(initialized);
    hard_assert(loop_time_us > 0);

    total_work_time_us += work_time_us;
    total_loop_time_us += loop_time_us;
    loop_count++;

    /*
     * Work share is the portion of the complete loop spent doing firmware
     * work before the deliberate delay. Loop duration includes the work and
     * delay. Samples is the number of loops in this reporting interval.
     */
    uint32_t work_share_tenths_percent = (uint32_t)((work_time_us * 1000) / loop_time_us);
    if (work_share_tenths_percent > peak_work_share_tenths_percent) {
        peak_work_share_tenths_percent = work_share_tenths_percent;
    }

    if (loop_time_us > maximum_loop_time_us) { maximum_loop_time_us = loop_time_us; }
    if (!time_reached(next_report_time)) { return; }

    uint64_t average_loop_time_us = total_loop_time_us / loop_count;
    uint32_t average_work_share_tenths_percent =
        (uint32_t)((total_work_time_us * 1000) / total_loop_time_us);

    if (display_enabled) {
        update_timing_diagnostics(
            average_work_share_tenths_percent,
            peak_work_share_tenths_percent,
            average_loop_time_us,
            maximum_loop_time_us,
            loop_count
        );
    }

    total_work_time_us                 = 0;
    total_loop_time_us                 = 0;
    maximum_loop_time_us               = 0;
    loop_count                         = 0;
    peak_work_share_tenths_percent     = 0;
    next_report_time                   = make_timeout_time_ms(TIMING_REPORT_INTERVAL_MS);
}

static void reset_timing_diagnostics_table(void) {
    lv_table_set_cell_value(td_table, 1, 1, "--");
    lv_table_set_cell_value(td_table, 1, 2, "--");
    lv_table_set_cell_value(td_table, 2, 1, "--");
    lv_table_set_cell_value(td_table, 2, 2, "--");
    lv_table_set_cell_value(td_table, 3, 1, "--");
    lv_table_set_cell_value(td_table, 3, 2, "");
}

static void update_timing_diagnostics(
    uint32_t average_work_share_tenths_percent,
    uint32_t peak_work_share_tenths_percent,
    uint64_t average_loop_time_us,
    uint64_t maximum_loop_time_us,
    uint32_t sample_count
) {
    char cell_value[16];

    snprintf(
        cell_value,
        sizeof(cell_value),
        "%lu.%lu%%",
        (unsigned long)(average_work_share_tenths_percent / 10),
        (unsigned long)(average_work_share_tenths_percent % 10)
    );
    lv_table_set_cell_value(td_table, 1, 1, cell_value);

    snprintf(
        cell_value,
        sizeof(cell_value),
        "%lu.%lu%%",
        (unsigned long)(peak_work_share_tenths_percent / 10),
        (unsigned long)(peak_work_share_tenths_percent % 10)
    );
    lv_table_set_cell_value(td_table, 1, 2, cell_value);

    snprintf(
        cell_value,
        sizeof(cell_value),
        "%llu.%02llu",
        (unsigned long long)(average_loop_time_us / 1000),
        (unsigned long long)((average_loop_time_us % 1000) / 10)
    );
    lv_table_set_cell_value(td_table, 2, 1, cell_value);

    snprintf(
        cell_value,
        sizeof(cell_value),
        "%llu.%02llu",
        (unsigned long long)(maximum_loop_time_us / 1000),
        (unsigned long long)((maximum_loop_time_us % 1000) / 10)
    );
    lv_table_set_cell_value(td_table, 2, 2, cell_value);

    snprintf(cell_value, sizeof(cell_value), "%lu", (unsigned long)sample_count);
    lv_table_set_cell_value(td_table, 3, 1, cell_value);
}
