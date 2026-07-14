#include "init_widgets.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// tileview
static lv_obj_t *tileview;
static lv_obj_t *tile00;
static lv_obj_t *tile01;
static lv_obj_t *tile02;

// widgets
static lv_obj_t *btn_counter;
static lv_obj_t *btn_counter_label;
static lv_obj_t *writable_label;

static uint16_t counter = 0;

// carousel stuff
typedef struct {
    const lv_img_dsc_t *image;
    const char *label;
} carousel_item_t;

LV_IMG_DECLARE(lightbulb);
LV_IMG_DECLARE(battery);
LV_IMG_DECLARE(light_switch);
static const carousel_item_t items[] = {
    { &lightbulb, "Bulb" },
    { &battery, "Battery" },
    { &light_switch, "Switch" },
};
enum { ITEMS_LEN = (sizeof(items) / sizeof(items[0])) };

static lv_obj_t *selected_button = NULL;

// callbacks
static void carousel_cb(lv_event_t *event);
static void btn_counter_cb(lv_event_t *event);

void init_widgets(void) {
    // Create tileview and tiles
    tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_scrollbar_mode(tileview,  LV_SCROLLBAR_MODE_ON);
    tile00 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_BOTTOM);
    tile01 = lv_tileview_add_tile(tileview, 0, 1, LV_DIR_TOP|LV_DIR_BOTTOM);
    tile02 = lv_tileview_add_tile(tileview, 0, 2, LV_DIR_TOP);

    /*==================== 
        Widgets
    ====================*/
    // Styles
    static lv_style_t style_label;
    lv_style_init(&style_label);
    lv_style_set_text_font(&style_label, &lv_font_montserrat_24);
    lv_style_set_pad_all(&style_label, 24);
    
    /*
        Row 0
    */
    // flex container
    lv_obj_t *carousel = lv_obj_create(tile00);
    lv_obj_set_size(carousel, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_center(carousel);
    lv_obj_set_flex_flow(carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(carousel, 0, 0);
    lv_obj_set_style_pad_left(carousel, 64, 0);
    lv_obj_set_style_pad_right(carousel, 64, 0);
    lv_obj_set_style_border_width(carousel, 0, 0);

    // button styles
    static lv_style_t style_icon_btn;
    lv_style_init(&style_icon_btn);
    lv_style_set_bg_color(&style_icon_btn, lv_color_hex3(0xfff));
    lv_style_set_border_color(&style_icon_btn, lv_color_hex3(0xccc));
    lv_style_set_border_width(&style_icon_btn, 1);
    lv_style_set_shadow_width(&style_icon_btn, 0);
    lv_style_set_layout(&style_icon_btn, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&style_icon_btn, LV_FLEX_FLOW_COLUMN);
    lv_style_set_flex_main_place(&style_icon_btn, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_cross_place(&style_icon_btn, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&style_icon_btn, LV_FLEX_ALIGN_CENTER);
    lv_style_set_align(&style_icon_btn, LV_ALIGN_CENTER);
    lv_style_set_pad_all(&style_icon_btn, 24);
    lv_style_set_pad_row(&style_icon_btn, 8);

    static lv_style_t style_icon_btn_checked;
    lv_style_init(&style_icon_btn_checked);
    lv_style_set_bg_color(&style_icon_btn_checked, lv_color_hex(0xa394f7));

    static lv_style_t style_btn_label;
    lv_style_init(&style_btn_label);
    lv_style_set_text_color(&style_btn_label, lv_color_hex3(0x333));

    // create buttons
    for (int i = 0; i < ITEMS_LEN; i++) {
        lv_obj_t *btn = lv_btn_create(carousel);
        lv_obj_add_style(btn, &style_icon_btn, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_style(btn, &style_icon_btn_checked, LV_PART_MAIN | LV_STATE_CHECKED);

        lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *img = lv_img_create(btn);
        lv_img_set_src(img, items[i].image);

        lv_obj_t *label = lv_label_create(btn);
        lv_obj_add_style(label, &style_btn_label, 0);
        lv_label_set_text(label, items[i].label);
    }

    lv_obj_add_event_cb(carousel, carousel_cb, LV_EVENT_CLICKED, NULL);

    /*
        Row 1
    */
    // Tile 01
    btn_counter = lv_btn_create(tile01);
    lv_obj_add_event_cb(btn_counter, btn_counter_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_counter, LV_ALIGN_CENTER, 0, 0);

    btn_counter_label = lv_label_create(btn_counter);
    lv_label_set_text(btn_counter_label, "Count: 0");
    lv_obj_center(btn_counter_label);
    lv_obj_add_style(btn_counter_label, &style_label, 0);

    /*
        Row 2
    */
    // Tile 02
    writable_label = lv_label_create(tile02);
    lv_obj_center(writable_label);
    lv_obj_add_style(writable_label, &style_label, 0);
}

static void carousel_cb(lv_event_t *event) {
    lv_obj_t *target = lv_event_get_target(event);
    lv_obj_t *carousel = lv_event_get_current_target(event);

    if (target == carousel) return;
    if (target == selected_button) {
        lv_obj_clear_state(target, LV_STATE_CHECKED);
        selected_button = NULL;
        return;
    }
    if (selected_button != NULL) {
        lv_obj_clear_state(selected_button, LV_STATE_CHECKED);
    }

    selected_button = target;
    lv_obj_add_state(target, LV_STATE_CHECKED);
}

static void btn_counter_cb(lv_event_t *event) {
    counter++;
    printf("Button pressed: %d\n", counter);

    char buf[12] = "Count: ";
    size_t len = strlen(buf);
    snprintf(buf + len, sizeof(buf) - len, "%d", counter);

    lv_label_set_text(btn_counter_label, buf);
}

void write_to_label(const char buf[], uint32_t count) {
    char local_buf[65];
    memcpy(local_buf, buf, count);
    local_buf[count] = '\0';

    lv_label_set_text(writable_label, local_buf);
}
