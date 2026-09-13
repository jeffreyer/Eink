#pragma once

#include <stdint.h>
#include "eink_display.h"   // 各驱动头文件在此定义 EPD_ASYNC_REFRESH_SLEEP_S / EPD_PANEL_POWEROFF_DELAY_MS

// ============================================================================
// 通用异步刷屏（省电）
//
// 墨水屏刷新期间面板自己定时序，MCU 只需把命令发出去，等待毫无意义。
// 因此三种屏统一走同一套流程：
//   1. 驱动发完“刷新”命令后调用 epdAsyncMarkStarted() 立即返回；
//   2. 系统随即进入深度休眠（不等待 BUSY，也不断电），省下刷新期间的 MCU 电流；
//   3. EPD_ASYNC_REFRESH_SLEEP_S 秒后定时唤醒，由 epdAsyncPowerOffNow()
//      关闭面板电源 → 面板进入深度休眠，然后继续休眠；
//   4. 若刷屏后没有进入休眠（BLE 交互等），主循环 epdAsyncMaintain() 在刷新结束后
//      补一次断电（非阻塞，不拖慢 BLE）。
//
// 驱动侧需要实现两个钩子（见各自 .cpp）：
//   bool epdPanelIsIdle(void);  // 刷新是否已结束
//   void epdPanelPowerOff(void); // 关闭面板电源 + 面板深度休眠（假定刷新已结束）
// ============================================================================

// 刷新结束的等待上限（保险：BUSY 读不到空闲时不要卡在这里）
// 唤醒时刻本就选在"刷新已完成"之后，这里只做兜底，故取值很短
#ifndef EPD_ASYNC_WAIT_TIMEOUT_MS
#define EPD_ASYNC_WAIT_TIMEOUT_MS 5000
#endif

// 临时诊断开关：置 1 时上电冷启动会触发一次刷新并打印 BUSY 电平时间线，
// 同时每次唤醒会打印 BUSY 原始电平；用于给新屏幕板卡确认 BUSY 极性与刷新时长。
// （串口命令 probe 不受此开关影响，随时可手动重跑时间线）
#define EPD_PROBE_BUSY_ON_BOOT 0

// 驱动未指定时的默认值（6 色屏参数）
#ifndef EPD_ASYNC_REFRESH_SLEEP_S
#define EPD_ASYNC_REFRESH_SLEEP_S 30
#endif
#ifndef EPD_PANEL_POWEROFF_DELAY_MS
#define EPD_PANEL_POWEROFF_DELAY_MS 3000
#endif
// 未休眠时，按时间兜底断电的上限（BUSY 读不到空闲电平时的保证）
#ifndef EPD_ASYNC_FORCE_POWEROFF_MS
#define EPD_ASYNC_FORCE_POWEROFF_MS 40000
#endif

// 驱动钩子
bool epdPanelIsIdle(void);
void epdPanelPowerOff(void);
int epdPanelBusyRaw(void);          // BUSY 引脚原始电平（诊断日志）
void epdPanelHoldPins(bool hold);   // 保持/释放本屏控制引脚（RST/CS/DC/SCK/MOSI）

// 驱动用：对给定引脚列表批量 hold/release（IDF 调用集中在此，见 epd_async.cpp）
void epdAsyncHoldPinsImpl(const int* pins, int count, bool hold);

// 驱动发出刷新命令后调用：进入“刷新中”状态（RTC 标记，跨深度休眠保持）
void epdAsyncMarkStarted(void);

// 新一帧开始前调用：上一帧仍在刷新则先等它结束（避免 reset 打断刷新），
// 并清除“刷新中”标记
void epdAsyncWaitPrevious(void);

// 是否有刷新正在进行
bool epdAsyncIsPending(void);
void epdAsyncClearPending(void);

// 面板断电 + 面板深度休眠（不等 BUSY）：
// 用于“已睡满整个刷新窗口”的定时唤醒路径、以及进入 BLE 前
void epdAsyncPowerOffNow(void);

// 主循环兜底（非阻塞）：刷新结束且已过 EPD_PANEL_POWEROFF_DELAY_MS 才断电，
// 返回 true 表示本次已完成断电
bool epdAsyncMaintain(void);

// 诊断：打印 BUSY 原始电平时间线（配合 eink_display_white() 触发一次刷新使用），
// 用来确认该屏 BUSY 的空闲极性与真实刷新时长
void epdAsyncProbeBusy(uint32_t timeout_ms, uint32_t sample_ms);

// 进入深度休眠前调用：若刷新仍在进行，保持面板控制引脚电平——
// 深度休眠时 GPIO 会浮空，浮空电平可能干扰甚至复位面板，
// 表现为唤醒后 BUSY 一直"忙"、补断电流程被迫走到超时
void epdAsyncPrepareSleep(void);

// 开机初始化墨水屏前调用：释放上次休眠期间保持的引脚
void epdAsyncReleasePins(void);
