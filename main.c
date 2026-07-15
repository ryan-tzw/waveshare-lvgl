#include "LCD_2in.h"
#include "CST816D.h"
#include "QMI8658.h"
#include "init.h"
#include "simulator_engine.h"
#include "core1_task_queue.h"

int main (void) {
    sleep_ms(2000);
    if (DEV_Module_Init() != 0) { return -1; } 

    /* Init core1_task_queue */
    core1_task_queue_init();

    /* Init LCD */
    LCD_2IN_Init(VERTICAL);
    LCD_2IN_Clear(WHITE);
    DEV_SET_PWM(60);

    /* Init touch screen */ 
    CST816D_init(CST816D_Point_Mode);
    
    /* Init IMU */
    // QMI8658_init();

    /* Init LVGL */
    init_lvgl();
    init_widgets();

	/* Init engine */
    init_simulator_engine();

    while (1) {
		lv_task_handler();
      	DEV_Delay_ms(5); 
    }

    DEV_Module_Exit();
    return 0;
}
