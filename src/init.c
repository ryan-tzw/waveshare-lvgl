#include "init.h"
#include <stdint.h>
#include <string.h>

// LVGL
static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf0[DISP_HOR_RES * DISP_VER_RES/2];
static lv_color_t buf1[DISP_HOR_RES * DISP_VER_RES/2];

static lv_obj_t *tileview;
static lv_obj_t *tile0;
static lv_obj_t *tile1;
static lv_obj_t *tile2;

static lv_obj_t *label;
static lv_obj_t *btn_counter;
static lv_obj_t *btn_counter_label;
static lv_obj_t *writable_label;

static uint16_t counter = 0;

// Touch
static uint16_t ts_x;
static uint16_t ts_y;
static lv_indev_state_t ts_act;
static uint8_t gesture = 0;
static lv_indev_drv_t indev_ts;

// Timer 
static struct repeating_timer lvgl_timer;
 
static void disp_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
static void touch_cb(uint gpio, uint32_t events);
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
static void dma_handler(void);
static void scroll_begin_event_cb(lv_event_t *event);
static bool repeating_lvgl_timer_cb(struct repeating_timer *t); 
static void button_event_cb(lv_event_t *event);


void init_lvgl(void) {
    /* 1. Init Timer */ 
    add_repeating_timer_ms(5, repeating_lvgl_timer_cb, NULL, &lvgl_timer);
    
    /* 2. Init LVGL core */
    lv_init();

    /* 3. Init LVGL display */
    lv_disp_draw_buf_init(&disp_buf, buf0, buf1, DISP_HOR_RES * DISP_VER_RES / 2); 
    lv_disp_drv_init(&disp_drv);    
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &disp_buf;        
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    lv_disp_t *disp= lv_disp_drv_register(&disp_drv);   

    /* 4. Init touch screen as input device */ 
    lv_indev_drv_init(&indev_ts); 
    indev_ts.type = LV_INDEV_TYPE_POINTER;    
    indev_ts.read_cb = touch_read_cb;            
    lv_indev_t * ts_indev = lv_indev_drv_register(&indev_ts);
    // Enable touch IRQ
    DEV_IRQ_SET(Touch_INT_PIN, GPIO_IRQ_EDGE_RISE, &touch_cb);

    /* 5. Init DMA for transmit color data from memory to SPI */
    dma_channel_set_irq0_enabled(dma_tx, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void init_widgets(void) {
    // Styles
    static lv_style_t style_label;
    lv_style_init(&style_label);
    lv_style_set_text_font(&style_label, &lv_font_montserrat_24);
    lv_style_set_pad_all(&style_label, 24);

    // Create tileview and tiles
    tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_scrollbar_mode(tileview,  LV_SCROLLBAR_MODE_ON);
    tile0 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_BOTTOM);
    tile1 = lv_tileview_add_tile(tileview, 0, 1, LV_DIR_TOP|LV_DIR_BOTTOM);
    tile2 = lv_tileview_add_tile(tileview, 0, 2, LV_DIR_TOP);

    /*
        Widgets
    */
    // Tile 0
    label = lv_label_create(tile0);
    lv_label_set_text(label, "Hello");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(label, &style_label, 0);

    // Tile 1
    btn_counter = lv_btn_create(tile1);
    lv_obj_add_event_cb(btn_counter, button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_counter, LV_ALIGN_CENTER, 0, 0);

    btn_counter_label = lv_label_create(btn_counter);
    lv_label_set_text(btn_counter_label, "Count: 0");
    lv_obj_center(btn_counter_label);
    lv_obj_add_style(btn_counter_label, &style_label, 0);

    // Tile 2
    writable_label = lv_label_create(tile2);
    lv_obj_center(writable_label);
    lv_obj_add_style(writable_label, &style_label, 0);
}

/* Disable scroll animations when a tab button is clicked in a tabview*/
static void scroll_begin_event_cb(lv_event_t * event) {
    lv_anim_t * a = lv_event_get_param(event);
    if (a) a->time = 0; 
}

/* Refresh image by transferring the color data to the SPI bus by DMA*/
static void disp_flush_cb(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p) {
    LCD_2IN_SetWindows(area->x1, area->y1, area->x2+1 , area->y2+1);  // Set the LVGL interface display position
    DEV_Digital_Write(LCD_DC_PIN, 1);
    DEV_Digital_Write(LCD_CS_PIN, 0);
    dma_channel_configure(dma_tx,   
                          &c,
                          &spi_get_hw(SPI_PORT)->dr, 
                          color_p, // read address
                          ((area->x2 + 1 - area-> x1)*(area->y2 + 1 - area -> y1))*2,
                          true);// Start DMA transfer
}

/* Touch interrupt handler */
static void touch_cb(uint gpio, uint32_t events) {
    if (gpio == Touch_INT_PIN) {
        CST816D_Get_Point(); // Get coordinate data
        gesture = CST816D_Get_Gesture(); // Get gesture data
        ts_x = Touch_CTS816.x_point;
        ts_y = Touch_CTS816.y_point;
        ts_act = LV_INDEV_STATE_PRESSED;
    }
}

/* Update touch screen input device status */
static void touch_read_cb(lv_indev_drv_t * drv, lv_indev_data_t*data) {
    data->point.x = ts_x;
    data->point.y = ts_y; 
    data->state = ts_act;
    ts_act = LV_INDEV_STATE_RELEASED;
}

/* Indicate ready with the flushing when DMA complete transmission */
static void dma_handler(void) {
    if (dma_channel_get_irq0_status(dma_tx)) {
        dma_channel_acknowledge_irq0(dma_tx);
        DEV_Digital_Write(LCD_CS_PIN, 1);
        lv_disp_flush_ready(&disp_drv); // Indicate you are ready with the flushing
    }
}

/* Check if the page needs to be updated */
static bool update_check(lv_obj_t *tv,lv_obj_t *tilex) {
    uint8_t ret = true; 

    // Get the current active interface
    lv_obj_t *active_tile = lv_tileview_get_tile_act(tv); 
    if (active_tile != tilex) {
        ret = false;
    }

    // The current gesture is not empty and is not a click
    if(gesture != CST816D_Gesture_None && gesture != CST816D_Gesture_Click) {
        gesture = CST816D_Gesture_None;
        ret = false;
    }

    return ret;
}

/* Report the elapsed time to LVGL each 5ms */
static bool repeating_lvgl_timer_cb(struct repeating_timer *t) {
    lv_tick_inc(5);
    return true;
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