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
static absolute_time_t next_update_time;

static void update_transport_table(void) {
    for (uint32_t local_port = 0; local_port < NETWORK_PORT_COUNT; local_port++) {
        NetworkPortStatistics statistics;
        hard_assert(network_get_port_statistics(local_port, &statistics));

        uint16_t table_column = (uint16_t)local_port + 1;
        lv_table_set_cell_value_fmt(transport_table, 1, table_column, "%lu", (unsigned long)statistics.received_frames);
        lv_table_set_cell_value_fmt(transport_table, 2, table_column, "%lu", (unsigned long)statistics.cobs_errors);
        lv_table_set_cell_value_fmt(transport_table, 3, table_column, "%lu", (unsigned long)statistics.crc_errors);
        lv_table_set_cell_value_fmt(transport_table, 4, table_column, "%lu", (unsigned long)statistics.oversized_frames);
        lv_table_set_cell_value_fmt(transport_table, 5, table_column, "%lu", (unsigned long)statistics.uart_dropped_bytes);
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
    lv_table_set_col_width      (transport_table, 0, 80);
    lv_table_set_col_width      (transport_table, 1, 36);
    lv_table_set_col_width      (transport_table, 2, 36);
    lv_table_set_col_width      (transport_table, 3, 36);
    lv_table_set_col_width      (transport_table, 4, 36);

    lv_table_set_cell_value     (transport_table, 0, 0, "Metric");
    lv_table_set_cell_value     (transport_table, 0, 1, "P0");
    lv_table_set_cell_value     (transport_table, 0, 2, "P1");
    lv_table_set_cell_value     (transport_table, 0, 3, "P2");
    lv_table_set_cell_value     (transport_table, 0, 4, "P3");

    /* OK counts valid frames; Dropped counts bytes lost from the UART queue. */
    lv_table_set_cell_value     (transport_table, 1, 0, "OK");
    lv_table_set_cell_value     (transport_table, 2, 0, "COBS");
    lv_table_set_cell_value     (transport_table, 3, 0, "CRC");
    lv_table_set_cell_value     (transport_table, 4, 0, "Over");
    lv_table_set_cell_value     (transport_table, 5, 0, "Drop");

    update_transport_table();
    next_update_time = make_timeout_time_ms(TRANSPORT_DIAGNOSTICS_UPDATE_INTERVAL_MS);
    initialized      = true;
}

void transport_diagnostics_update(void) {
    hard_assert(initialized);
    if (!time_reached(next_update_time)) { return; }

    update_transport_table();
    next_update_time = make_timeout_time_ms(TRANSPORT_DIAGNOSTICS_UPDATE_INTERVAL_MS);
}
