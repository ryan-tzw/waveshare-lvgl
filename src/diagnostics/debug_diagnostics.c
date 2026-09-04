/*
 * Presents the on-device Debug controls and coordinates the diagnostic pages.
 */

#include "debug_diagnostics.h"

#include "init.h"
#include "timing_diagnostics.h"
#include "transport_diagnostics.h"

#include <stdbool.h>


#define SCROLL_ANIMATION_TIME_MS 100

static lv_obj_t *debug_carousel;
static lv_obj_t *timing_debug_page;
static lv_obj_t *transport_debug_page;
static bool link_state_database_print_requested = false;

static void debug_switch_cb         (lv_event_t *event);
static void print_database_button_cb(lv_event_t *event);
static void scroll_animation_cb     (lv_event_t *event);

void debug_diagnostics_init(lv_obj_t *parent) {
    hard_assert(parent != NULL);

    /* The nested carousel keeps vertical tile navigation independent of its horizontal page. */
    debug_carousel = lv_obj_create(parent);
    lv_obj_set_size               (debug_carousel, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_center                 (debug_carousel);
    lv_obj_set_flex_flow          (debug_carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align         (debug_carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir         (debug_carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x      (debug_carousel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode     (debug_carousel, LV_SCROLLBAR_MODE_ON);
    lv_obj_add_flag               (debug_carousel, LV_OBJ_FLAG_SCROLL_ONE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb           (debug_carousel, scroll_animation_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_set_style_radius       (debug_carousel, 0, 0);
    lv_obj_set_style_border_width (debug_carousel, 0, 0);
    lv_obj_set_style_pad_all      (debug_carousel, 0, 0);
    lv_obj_set_style_pad_column   (debug_carousel, 0, 0);

    lv_obj_t *debug_control_page = lv_obj_create(debug_carousel);
    lv_obj_set_size               (debug_control_page, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_clear_flag             (debug_control_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius       (debug_control_page, 0, 0);
    lv_obj_set_style_border_width (debug_control_page, 0, 0);

    lv_obj_t *debug_title = lv_label_create(debug_control_page);
    lv_label_set_text          (debug_title, "Debug");
    lv_obj_set_style_text_font (debug_title, &lv_font_montserrat_24, 0);
    lv_obj_align               (debug_title, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *debug_switch = lv_switch_create(debug_control_page);
    lv_obj_add_event_cb (debug_switch, debug_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align        (debug_switch, LV_ALIGN_CENTER, 0, -24);

    lv_obj_t *print_database_button = lv_btn_create(debug_control_page);
    lv_obj_add_event_cb         (print_database_button, print_database_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align                (print_database_button, LV_ALIGN_CENTER, 0, 64);

    lv_obj_t *print_database_label = lv_label_create(print_database_button);
    lv_label_set_text           (print_database_label, "Print DB");
    lv_obj_set_style_text_font  (print_database_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_hor    (print_database_label, 12, 0);
    lv_obj_set_style_pad_ver    (print_database_label, 8, 0);
    lv_obj_center               (print_database_label);

    timing_debug_page = lv_obj_create(debug_carousel);
    lv_obj_set_size               (timing_debug_page, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_clear_flag             (timing_debug_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius       (timing_debug_page, 0, 0);
    lv_obj_set_style_border_width (timing_debug_page, 0, 0);
    timing_diagnostics_init       (timing_debug_page);
    lv_obj_add_flag               (timing_debug_page, LV_OBJ_FLAG_HIDDEN);

    transport_debug_page = lv_obj_create(debug_carousel);
    lv_obj_set_size               (transport_debug_page, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_clear_flag             (transport_debug_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius       (transport_debug_page, 0, 0);
    lv_obj_set_style_border_width (transport_debug_page, 0, 0);
    transport_diagnostics_init    (transport_debug_page);
    lv_obj_add_flag               (transport_debug_page, LV_OBJ_FLAG_HIDDEN);
}

static void debug_switch_cb(lv_event_t *event) {
    lv_obj_t *debug_switch = lv_event_get_target(event);
    bool debug_enabled = lv_obj_has_state(debug_switch, LV_STATE_CHECKED);

    if (debug_enabled) {
        lv_obj_clear_flag(timing_debug_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(transport_debug_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(debug_carousel);
    } else {
        lv_obj_scroll_to_x(debug_carousel, 0, LV_ANIM_OFF);
        lv_obj_add_flag(timing_debug_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(transport_debug_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(debug_carousel);
    }

    timing_diagnostics_set_enabled(debug_enabled);
    transport_diagnostics_set_enabled(debug_enabled);
}

static void print_database_button_cb(lv_event_t *event) {
    link_state_database_print_requested = true;
}

static void scroll_animation_cb(lv_event_t *event) {
    lv_anim_t *animation = lv_event_get_scroll_anim(event);
    if (animation != NULL) lv_anim_set_time(animation, SCROLL_ANIMATION_TIME_MS);
}

bool debug_diagnostics_take_link_state_database_print_request(void) {
    bool print_requested = link_state_database_print_requested;
    link_state_database_print_requested = false;
    return print_requested;
}
