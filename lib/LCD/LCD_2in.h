/*****************************************************************************
* | File      	:   LCD_2in.h
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master 
*                and enhance portability
*----------------
* |	This version:   V1.0
* | Date        :   2025-03-13
* | Info        :   Basic version
*
******************************************************************************/
#ifndef __LCD_2IN_H
#define __LCD_2IN_H

#include "DEV_Config.h"
#include <stdint.h>

#include <stdlib.h>     //itoa()
#include <stdio.h>

#define LCD_2IN_HEIGHT 320
#define LCD_2IN_WIDTH 240

#define HORIZONTAL 0
#define VERTICAL   1

#define WHITE         0xFFFF
#define BLACK		  0x0000
#define BLUE 		  0x001F
#define BRED 	      0XF81F
#define GRED 		  0XFFE0
#define GBLUE		  0X07FF
#define RED  		  0xF800
#define MAGENTA		  0xF81F
#define GREEN		  0x07E0
#define CYAN 		  0x7FFF
#define YELLOW		  0xFFE0
#define BROWN		  0XBC40
#define BRRED		  0XFC07
#define GRAY 	      0X8430
#define DARKBLUE	  0X01CF
#define LIGHTBLUE	  0X7D7C
#define GRAYBLUE      0X5458
#define LIGHTGREEN    0X841F
#define LGRAY 		  0XC618
#define LGRAYBLUE     0XA651
#define LBBLUE        0X2B12

#define LCD_2IN_SetBacklight(Value); 

extern UBYTE Scan_dir;

typedef struct{
    UWORD WIDTH;
    UWORD HEIGHT;
    UBYTE SCAN_DIR;
}LCD_2IN_ATTRIBUTES;
extern LCD_2IN_ATTRIBUTES LCD_2IN;

/********************************************************************************
function:	
			Macro definition variable name
********************************************************************************/
void LCD_2IN_Init(UBYTE Scan_dir);
void LCD_2IN_Clear(UWORD Color);
void LCD_2IN_Display(UWORD *Image);
void LCD_2IN_SetWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend);
void LCD_2IN_DisplayWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD *Image);
void LCD_2IN_DisplayPoint(UWORD X, UWORD Y, UWORD Color);
void Handler_2IN_LCD(int signo);
#endif
