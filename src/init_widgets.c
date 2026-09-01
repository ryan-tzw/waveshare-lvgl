#include "init_widgets.h"
#include "device_state.h"
#include "timing_diagnostics.h"

// tileview
static lv_obj_t *tileview;
static lv_obj_t *tile00;
static lv_obj_t *tile01;
static lv_obj_t *tile02;

// widgets
static lv_obj_t *btn_print_database;

static bool link_state_database_print_requested = false;

// carousel stuff
typedef struct {
    const lv_img_dsc_t *image;
    const char *label;
    DeviceType device_type;
} carousel_item_t;

LV_IMG_DECLARE(lightbulb);
LV_IMG_DECLARE(battery);
LV_IMG_DECLARE(light_switch);
static const carousel_item_t items[] = {
    { &lightbulb,    "Bulb",    DEVICE_TYPE_BULB    },
    { &battery,      "Battery", DEVICE_TYPE_BATTERY },
    { &light_switch, "Switch",  DEVICE_TYPE_SWITCH  },
};
enum { ITEMS_LEN = (sizeof(items) / sizeof(items[0])) };

static lv_obj_t *selected_button = NULL;
static lv_obj_t *carousel_buttons[ITEMS_LEN];

// callbacks
static void carousel_cb          (lv_event_t *event);
static void btn_print_database_cb(lv_event_t *event);

void init_widgets(void) {
    // Create tileview and tiles
    tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_scrollbar_mode(tileview,  LV_SCROLLBAR_MODE_ON);
    tile00 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_BOTTOM);
    tile01 = lv_tileview_add_tile(tileview, 0, 1, LV_DIR_TOP | LV_DIR_BOTTOM);
    tile02 = lv_tileview_add_tile(tileview, 0, 2, LV_DIR_TOP);

    /*==================== 
        Widgets
    ====================*/
    // Styles
    static lv_style_t style_label;
    lv_style_init          (&style_label);
    lv_style_set_text_font (&style_label, &lv_font_montserrat_24);
    lv_style_set_pad_all   (&style_label, 24);
    
    /*
        Row 0
    */
    // flex container
    lv_obj_t *carousel = lv_obj_create(tile00);
    lv_obj_set_size               (carousel, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_center                 (carousel);
    lv_obj_set_flex_flow          (carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align         (carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius       (carousel,  0, 0);
    lv_obj_set_style_pad_left     (carousel, 64, 0);
    lv_obj_set_style_pad_right    (carousel, 64, 0);
    lv_obj_set_style_border_width (carousel,  0, 0);

    // button styles
    static lv_style_t style_icon_btn;
    lv_style_init                 (&style_icon_btn);
    lv_style_set_bg_color         (&style_icon_btn, lv_color_hex3(0xfff));
    lv_style_set_border_color     (&style_icon_btn, lv_color_hex3(0xccc));
    lv_style_set_border_width     (&style_icon_btn, 1                   );
    lv_style_set_shadow_width     (&style_icon_btn, 0                   );
    lv_style_set_layout           (&style_icon_btn, LV_LAYOUT_FLEX      );
    lv_style_set_flex_flow        (&style_icon_btn, LV_FLEX_FLOW_COLUMN );
    lv_style_set_flex_main_place  (&style_icon_btn, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_cross_place (&style_icon_btn, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place (&style_icon_btn, LV_FLEX_ALIGN_CENTER);
    lv_style_set_align            (&style_icon_btn, LV_ALIGN_CENTER);
    lv_style_set_pad_all          (&style_icon_btn, 24);
    lv_style_set_pad_row          (&style_icon_btn, 8);

    static lv_style_t style_icon_btn_checked;
    lv_style_init           (&style_icon_btn_checked);
    lv_style_set_bg_color   (&style_icon_btn_checked, lv_color_hex(0xa394f7));

    static lv_style_t style_btn_label;
    lv_style_init           (&style_btn_label);
    lv_style_set_text_color (&style_btn_label, lv_color_hex3(0x333));

    // create buttons
    for (int i = 0; i < ITEMS_LEN; i++) {
        lv_obj_t *btn = lv_btn_create(carousel);
        carousel_buttons[i] = btn;

        lv_obj_add_style(btn, &style_icon_btn,         LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_style(btn, &style_icon_btn_checked, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_flag (btn, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *img = lv_img_create(btn);
        lv_img_set_src(img, items[i].image);

        lv_obj_t *label = lv_label_create(btn);
        lv_obj_add_style (label, &style_btn_label, 0);
        lv_label_set_text(label, items[i].label);
    }

    lv_obj_add_event_cb(carousel, carousel_cb, LV_EVENT_CLICKED, NULL);

    /*
        Row 1
    */
    // Tile 01
    btn_print_database = lv_btn_create(tile01);
    lv_obj_add_event_cb (btn_print_database, btn_print_database_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align        (btn_print_database, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_print_database_label = lv_label_create(btn_print_database);
    lv_label_set_text   (btn_print_database_label, "Print database");
    lv_obj_center       (btn_print_database_label);
    lv_obj_add_style    (btn_print_database_label, &style_label, 0);

    /*
        Row 2
    */
    timing_diagnostics_init(tile02);
}

static void carousel_cb(lv_event_t *event) {
    lv_obj_t *target   = lv_event_get_target(event);
    lv_obj_t *carousel = lv_event_get_current_target(event);

    if (target == carousel) return;
    if (target == selected_button) {
        lv_obj_clear_state(target, LV_STATE_CHECKED);
        selected_button = NULL;
        device_state_set_type(DEVICE_TYPE_NONE);
        return;
    }
    if (selected_button != NULL) {
        lv_obj_clear_state(selected_button, LV_STATE_CHECKED);
    }

    selected_button = target;
    lv_obj_add_state(target, LV_STATE_CHECKED);

    for (int i = 0; i < ITEMS_LEN; i++) {
        if (target == carousel_buttons[i]) {
            device_state_set_type(items[i].device_type);
            break;
        }
    }
}

static void btn_print_database_cb(lv_event_t *event) {
    link_state_database_print_requested = true;
}

bool take_link_state_database_print_request(void) {
    bool print_requested = link_state_database_print_requested;
    link_state_database_print_requested = false;
    return print_requested;
}
