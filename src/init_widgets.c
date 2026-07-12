#include  "init_widgets.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static lv_obj_t *tileview;
static lv_obj_t *tile00, *tile10;
static lv_obj_t *tile01;
static lv_obj_t *tile02;

static lv_obj_t *label;
static lv_obj_t *btn_counter;
static lv_obj_t *btn_counter_label;
static lv_obj_t *writable_label;

static uint16_t counter = 0;
static void button_event_cb(lv_event_t *event);

void init_widgets(void) {
    // Styles
    static lv_style_t style_label;
    lv_style_init(&style_label);
    lv_style_set_text_font(&style_label, &lv_font_montserrat_24);
    lv_style_set_pad_all(&style_label, 24);

    // Create tileview and tiles
    tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_scrollbar_mode(tileview,  LV_SCROLLBAR_MODE_ON);
    tile00 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_BOTTOM);
    tile01 = lv_tileview_add_tile(tileview, 0, 1, LV_DIR_TOP|LV_DIR_BOTTOM);
    tile02 = lv_tileview_add_tile(tileview, 0, 2, LV_DIR_TOP);

    /*
        Widgets
    */
    // Tile 0
    static lv_style_t style_img;
    lv_style_init(&style_img);
    lv_style_set_pad_all(&style_img, 24);

    static lv_style_t style_icon_btn;
    lv_style_init(&style_icon_btn);
    lv_style_set_bg_color(&style_icon_btn, lv_color_hex3(0xfff));
    lv_style_set_outline_color(&style_icon_btn, lv_color_hex3(0xccc));
    lv_style_set_outline_width(&style_icon_btn, 1);


    LV_IMG_DECLARE(lightbulb);
    lv_obj_t *btn_lightbulb = lv_btn_create(tile00);
    lv_obj_center(btn_lightbulb);
    lv_obj_add_style(btn_lightbulb, &style_icon_btn, 0);
    lv_obj_t *img_lightbulb = lv_img_create(btn_lightbulb);
    lv_img_set_src(img_lightbulb, &lightbulb);
    lv_obj_center(img_lightbulb);
    lv_obj_add_style(img_lightbulb, &style_img, 0);

    // Tile 1
    btn_counter = lv_btn_create(tile01);
    lv_obj_add_event_cb(btn_counter, button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_counter, LV_ALIGN_CENTER, 0, 0);

    btn_counter_label = lv_label_create(btn_counter);
    lv_label_set_text(btn_counter_label, "Count: 0");
    lv_obj_center(btn_counter_label);
    lv_obj_add_style(btn_counter_label, &style_label, 0);

    // Tile 2
    writable_label = lv_label_create(tile02);
    lv_obj_center(writable_label);
    lv_obj_add_style(writable_label, &style_label, 0);
}

static void button_event_cb(lv_event_t *event) {
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