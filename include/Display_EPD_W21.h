#ifndef _DISPLAY_EPD_W21_H_
#define _DISPLAY_EPD_W21_H_

#include <stdint.h>

//2bit
#define black   0x00  /// 00
#define white   0x01  /// 01
#define yellow  0x02  /// 10
#define red     0x03  /// 11


#define Source_BITS     200
#define Gate_BITS   200
#define ALLSCREEN_BYTES   Source_BITS*Gate_BITS/4


//EPD
void EPD_init2(void);
void EPD_init(void);
void PIC_display(const unsigned char* picData);
void EPD_sleep(void);
void EPD_update(void);
void lcd_chkstatus(void);

// 异步刷屏拆分接口（见 epd_async.h）：
// PIC_write_ram + EPD_update_async 只发命令不等待，等待/断电由异步层调度
void PIC_write_ram(const unsigned char* picData);
void EPD_update_async(void);
bool EPD_wait_idle(uint32_t timeout_ms);
void EPD_reset_only(void);
void EPD_poweroff_sleep(void);

void Display_All_Black(void);
void Display_All_White(void);
void Display_All_Yellow(void);
void Display_All_Red(void);

void Acep_color(unsigned char color);
void EPD_init_Fast(void);  
void EPD_init_Fast2(void);  

#endif
/***********************************************************
            end file
***********************************************************/
