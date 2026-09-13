#pragma once

#include "common.h"

// 墨水屏统一入口：按 INK6 / INK_BW / 四色屏 选择具体驱动头文件，
// 调用点只使用下面的统一接口，不再各写一份条件编译。
#if defined(INK6)
#include "eink6.h"
#elif defined(INK_BW)
#include "eink_bw.h"
#else
#include "eink.h"
#include "Display_EPD_W21.h"
#endif

// 当前是否为黑白（单色）屏
#if defined(INK_BW)
#define EINK_IS_MONO 1
#else
#define EINK_IS_MONO 0
#endif

// 整屏画布缓冲：尺寸 = ALLSCREEN_BYTES（200x200 1bpp = 5000，200x200 2bpp = 10000，240x240 4bpp = 28800）
extern unsigned char BlackImage[ALLSCREEN_BYTES];

// 初始化 SPI 与面板（setup 时调用一次）
int eink_display_init(void);

// 把 BlackImage 整帧刷到墨水屏（各驱动内部自理上电/断电/休眠）
void eink_display_frame(void);

// 全屏刷白
void eink_display_white(void);
