#pragma once

#define EPD_WIDTH   200
#define EPD_HEIGHT  200

//2bit
#define black   0x00	/// 00
#define white   0x01	///	01
#define yellow  0x02	///	10
#define red     0x03	///	11

const uint8_t IMAGE_DATA[EPD_WIDTH*EPD_HEIGHT/4] = {};

int init_eink();
int ink_draw();
int gui_drawtext(const char* str);
int gui_draw();
