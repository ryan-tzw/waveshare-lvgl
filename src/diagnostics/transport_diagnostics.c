/*
 * Presents cumulative receive statistics for each physical network port.
 */

#include "transport_diagnostics.h"

#include "init.h"
#include "network.h"
#include "pico/time.h"

#include <stdbool.h>
#include <stdint.h>


#define TRANSPORT_DIAGNOSTICS_UPDATE_INTERVAL_MS 1000

static lv_obj_t *transport_table;
static bool initialized = false;
static bool display_enabled = false;
static absolute_time_t next_update_time;

static void update_transport_table(void) {
    for (uint32_t local_port = 0; local_port < NETWORK_PORT_COUNT; local_port++) {
        NetworkPortStatistics statistics;
        hard_assert(network_get_port_statistics(local_port, &statistics));

        uint16_t table_row = (uint16_t)local_port + 1;
        lv_table_set_cell_value_fmt(transport_table, table_row, 0, "%lu", (unsigned long)local_port);
        lv_table_set_cell_value_fmt(transport_table, table_row, 1, "%lu", (unsigned long)statistics.received_frames);
        lv_table_set_cell_value_fmt(transport_table, table_row, 2, "%lu", (unsigned long)statistics.cobs_errors);
        lv_table_set_cell_value_fmt(transport_table, table_row, 3, "%lu", (unsigned long)statistics.crc_errors);
        lv_table_set_cell_value_fmt(transport_table, table_row, 4, "%lu", (unsigned long)statistics.oversized_frames);
        lv_table_set_cell_value_fmt(transport_table, table_row, 5, "%lu", (unsigned long)statistics.uart_dropped_bytes);
    }
}

void transport_diagnostics_init(lv_obj_t *parent) {
    hard_assert(parent != NULL);
    hard_assert(!initialized);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text          (title, "UART transport");
    lv_obj_set_style_text_font (title, &lv_font_montserrat_24, 0);
    lv_obj_align               (title, LV_ALIGN_TOP_MID, 0, 32);

    transport_table = lv_table_create(parent);
    lv_obj_set_size             (transport_table, DISP_HOR_RES - 16, 144);
    lv_obj_align                (transport_table, LV_ALIGN_CENTER, 0, 32);
    lv_obj_clear_flag           (transport_table, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font  (transport_table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor    (transport_table, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver    (transport_table, 4, LV_PART_ITEMS);
    lv_table_set_col_width      (transport_table, 0, 28);
    lv_table_set_col_width      (transport_table, 1, 60);
    lv_table_set_col_width      (transport_table, 2, 34);
    lv_table_set_col_width      (transport_table, 3, 34);
    lv_table_set_col_width      (transport_table, 4, 34);
    lv_table_set_col_width      (transport_table, 5, 34);

    /* OK counts valid frames; Dropped counts bytes lost from the UART queue. */
    lv_table_set_cell_value     (transport_table, 0, 0, "P");
    lv_table_set_cell_value     (transport_table, 0, 1, "OK");
    lv_table_set_cell_value     (transport_table, 0, 2, "CO");
    lv_table_set_cell_value     (transport_table, 0, 3, "CR");
    lv_table_set_cell_value     (transport_table, 0, 4, "OV");
    lv_table_set_cell_value     (transport_table, 0, 5, "DR");

    update_transport_table();
    next_update_time = make_timeout_time_ms(TRANSPORT_DIAGNOSTICS_UPDATE_INTERVAL_MS);
    initialized      = true;
}

void transport_diagnostics_set_enabled(bool enabled) {
    hard_assert(initialized);

    display_enabled = enabled;
    if (!display_enabled) { return; }

    update_transport_table();
    next_update_time = make_timeout_time_ms(TRANSPORT_DIAGNOSTICS_UPDATE_INTERVAL_MS);
}

void transport_diagnostics_update(void) {
    hard_assert(initialized);
    if (!display_enabled) { return; }
    if (!time_reached(next_update_time)) { return; }

    update_transport_table();
    next_update_time = make_timeout_time_ms(TRANSPORT_DIAGNOSTICS_UPDATE_INTERVAL_MS);
}
