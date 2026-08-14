#pragma once
#include <Arduino.h>

// Color byte values are copied from the original MSP430 reference code.
enum EpdColorByte : uint8_t {
    COLOR_BLACK = 0x00,
    COLOR_WHITE = 0x11,
    COLOR_YELLOW = 0x22,
    COLOR_RED = 0x33,
    COLOR_BLUE = 0x55,
    COLOR_GREEN = 0x66,
};

// 画布绘制用的 4bit 颜色值（每个像素 4bit，2 像素/字节）。
// 与 GUI_Paint Scale=7 模式配合使用，最终写入帧缓冲时左移 4 位即得 EpdColorByte。
enum EpdColorNibble : uint8_t {
    NIBBLE_BLACK  = 0x0,
    NIBBLE_WHITE  = 0x1,
    NIBBLE_YELLOW = 0x2,
    NIBBLE_RED    = 0x3,
    NIBBLE_BLUE   = 0x5,
    NIBBLE_GREEN  = 0x6,
};

// EPD resolution from original code: 480x720, 4bpp packed (2 pixels per byte).
// Panel is GDEH0154E01: 240x240, 4bpp packed (2 pixels per byte)
static const uint16_t EPD_WIDTH = 240;
static const uint16_t EPD_HEIGHT = 240;
static const uint32_t EPD_FRAME_BYTES = (EPD_WIDTH * EPD_HEIGHT) / 2; // 240*240/2 = 28800

#define ALLSCREEN_BYTES EPD_FRAME_BYTES

// 系统级异步刷屏：epdDisplayImage 发完 0x12 刷新命令后立即返回（不等待 BUSY/不断电）。
// 若随后进入深度休眠，等 EPD_ASYNC_REFRESH_SLEEP_S 秒后唤醒完成面板断电；
// 若未休眠（实时交互/BLE），由主循环 epdPanelPowerMaintain() 延迟断电兜底
#define EPD_ASYNC_REFRESH_SLEEP_S 30
#define EPD_PANEL_POWEROFF_DELAY_MS 3000

int init_eink6();
int gui_drawtext(const char* str);
void epdDisplaySolid(EpdColorByte color);
void epdDisplayImage(const unsigned char* imgData, uint32_t dataLen);
void epdEnterDeepSleep();
// 面板刷新是否仍在进行（RTC 标志，跨深度休眠保留）
bool epdIsRefreshPending(void);
void epdClearRefreshPending(void);
// 唤醒后完成面板断电：等待刷新结束 → Power OFF → 面板进入深度休眠
void epdFinishPowerOff(void);
// 主循环调用：异步刷新后若系统未进入休眠，延迟断电面板
void epdPanelPowerMaintain(void);
void epdReset();
void epdTryFixBusyPolarity();
void epdInitJD7601();
void epdInitJD7601WaterRipple_v2();
bool epdWaitBusy(uint32_t timeoutMs);
bool epdWaitBusyStage(const char* stage, uint32_t timeoutMs);
void epdWriteCommand(uint8_t cmd);
void epdWriteData(uint8_t data);
