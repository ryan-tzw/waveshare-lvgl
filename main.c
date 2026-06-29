#include "LCD_2in.h"
#include "CST816D.h"
// #include "QMI8658.h"
#include "init.h"

int main (void) {
    if (DEV_Module_Init() != 0) { return -1; } 

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

    while (1) {
      lv_task_handler();
      DEV_Delay_ms(5); 
    }

    DEV_Module_Exit();
    return 0;
}
