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

// 异步刷屏参数（见 include/epd_async.h）：4 色全刷约 12 秒，留足余量
// （唤醒即断电，不再依赖 BUSY 判断刷新是否结束，窗口必须盖住最坏刷新时长）
#define EPD_ASYNC_REFRESH_SLEEP_S 15
#define EPD_PANEL_POWEROFF_DELAY_MS 13000
// 唤醒时刷新早已结束，这里只做兜底等待（BUSY 读不到空闲时不长时间卡住）
#define EPD_ASYNC_WAIT_TIMEOUT_MS 3000

bool epdPanelIsIdle(void);
void epdPanelPowerOff(void);
int epdPanelBusyRaw(void);
void epdPanelHoldPins(bool hold);
