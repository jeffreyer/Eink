#pragma once

#include <Arduino.h>
#include <stdint.h>

// 1.54" 黑白墨水屏（200x200，SSD1681 兼容控制器，1bpp，1 字节 = 8 像素，bit=1 为白）
// 驱动实现见 src/eink_bw.cpp，仅在定义 INK_BW 时参与编译。

#define EPD_WIDTH   200
#define EPD_HEIGHT  200
#define EPD_FRAME_BYTES (EPD_WIDTH * EPD_HEIGHT / 8)  // 200*200/8 = 5000 字节
#define ALLSCREEN_BYTES EPD_FRAME_BYTES

// 面板接线（与 MiniEink 硬件一致；换硬件只需改这里）
#define EPD_BW_MOSI_PIN 7
#define EPD_BW_CLK_PIN  6
#define EPD_BW_BUSY_PIN 10
#define EPD_BW_DC_PIN   4
#define EPD_BW_CS_PIN   5
#define EPD_BW_RST_PIN  3

// 面板 BUSY 等待上限（初始化/补断电保险用）
#define EPD_BW_BUSY_TIMEOUT_MS 15000

// 纯色刷屏参数（与 Lua 颜色值 0=黑 / 1=白 对齐）
#define BW_BLACK 0
#define BW_WHITE 1

// 异步刷屏参数（见 include/epd_async.h）：黑白屏全刷约 2 秒，留足余量
#define EPD_ASYNC_REFRESH_SLEEP_S 3
#define EPD_PANEL_POWEROFF_DELAY_MS 3000
#define EPD_ASYNC_WAIT_TIMEOUT_MS 3000

bool epdPanelIsIdle(void);
void epdPanelPowerOff(void);
int epdPanelBusyRaw(void);
void epdPanelHoldPins(bool hold);

int init_eink_bw();
int gui_drawtext(const char* str);

// 整帧刷新（发完刷新命令立即返回，刷新期间由系统深度休眠，
// 刷新结束后由 epd_async 调度断电 + 面板深度休眠）
void epdBWDisplayImage(const unsigned char* imgData, uint32_t dataLen);

// 纯色刷屏：BW_BLACK / BW_WHITE
void epdBWDisplaySolid(uint8_t color);

void epdBWReset();
void epdBWWaitBusy();
void epdBWEnterDeepSleep();
