#include "epd_async.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include "common.h"

// 刷新进行中标记：放在 RTC 内存，深度休眠后仍可读到
// （“30 秒后唤醒补断电”这一路径依赖该标记判断本次唤醒的来意）
RTC_DATA_ATTR static bool s_refresh_pending = false;
static uint32_t s_refresh_start_ms = 0;

void epdAsyncMarkStarted(void) {
  s_refresh_pending = true;
  s_refresh_start_ms = millis();
  Serial.println("[EPD] async refresh started");
}

// 等待面板刷新结束（带超时保险 + 诊断日志）
static void epdWaitIdle(void) {
  if (epdPanelIsIdle()) {
    return;
  }

  Serial.printf("[EPD] wait idle: BUSY raw=%d\n", epdPanelBusyRaw());

  uint32_t start = millis();
  while (!epdPanelIsIdle()) {
    // 轮询间隔 ≥ tickless idle 入睡阈值，等待期间允许 CPU 进入 light sleep
    delay(20);
    if (millis() - start > EPD_ASYNC_WAIT_TIMEOUT_MS) {
      // 已经睡满整个刷新窗口，BUSY 仍报忙（引脚被干扰/极性不符）时直接断电，
      // 面板会在下一帧初始化时重新复位，不会因此损坏
      Serial.printf("[EPD] wait idle timeout (%u ms), BUSY raw=%d -> power off anyway\n",
                    (unsigned)EPD_ASYNC_WAIT_TIMEOUT_MS, epdPanelBusyRaw());
      break;
    }
  }
}

void epdAsyncHoldPinsImpl(const int* pins, int count, bool hold) {
  if (hold) {
    for (int i = 0; i < count; i++) {
      gpio_hold_en((gpio_num_t)pins[i]);
    }
    gpio_deep_sleep_hold_en();
  } else {
    gpio_deep_sleep_hold_dis();
    for (int i = 0; i < count; i++) {
      gpio_hold_dis((gpio_num_t)pins[i]);
    }
  }
}

void epdAsyncPrepareSleep(void) {
  if (!s_refresh_pending) {
    return;
  }
  // 刷新期间 MCU 深度休眠：保持 RST/CS/DC/SCK/MOSI 电平，避免浮空电平干扰面板
  epdPanelHoldPins(true);
  Serial.println("[EPD] panel control pins held during sleep");
}

void epdAsyncReleasePins(void) {
  epdPanelHoldPins(false);
}

void epdAsyncWaitPrevious(void) {
  if (!s_refresh_pending) {
    return;
  }
  Serial.println("[EPD] sync: waiting previous refresh to finish");
  epdWaitIdle();
  s_refresh_pending = false;
}

bool epdAsyncIsPending(void) {
  return s_refresh_pending;
}

void epdAsyncClearPending(void) {
  s_refresh_pending = false;
}

void epdAsyncPowerOffNow(void) {
  epdPanelPowerOff();
}

bool epdAsyncMaintain(void) {
  if (!s_refresh_pending) {
    return true;
  }
  uint32_t elapsed = millis() - s_refresh_start_ms;
  if (elapsed < EPD_PANEL_POWEROFF_DELAY_MS) {
    return false;
  }
  // 非阻塞：刷新还没结束就等下一轮主循环，避免长时间占住 loop（BLE 期间尤其明显）。
  // BUSY 读不到空闲电平（引脚/极性不可靠）时，按时间兜底断电，保证面板不会一直通电
  if (!epdPanelIsIdle() && elapsed < EPD_ASYNC_FORCE_POWEROFF_MS) {
    return false;
  }

  if (!epdPanelIsIdle()) {
    Serial.printf("[EPD] maintain: BUSY raw=%d after %u ms, power off by timeout\n",
                  epdPanelBusyRaw(), (unsigned)elapsed);
  } else {
    Serial.println("[EPD] panel power off (no sleep after refresh)");
  }
  epdPanelPowerOff();
  s_refresh_pending = false;
  return true;
}

void epdAsyncProbeBusy(uint32_t timeout_ms, uint32_t sample_ms) {
  Serial.printf("[EPD] probe: BUSY timeline (timeout %u ms, sample %u ms)\n",
                (unsigned)timeout_ms, (unsigned)sample_ms);
  uint32_t start = millis();
  uint32_t next_tick = 1000;
  uint32_t level_since = 0;   // 当前电平的起始时刻（判断是否已稳定）
  int first = -1;
  int last = -1;
  bool changed = false;

  while (millis() - start < timeout_ms) {
    uint32_t t = millis() - start;
    int raw = epdPanelBusyRaw();
    if (raw != last) {
      Serial.printf("[EPD] probe: t=%u ms BUSY=%d idle=%d\n",
                    (unsigned)t, raw, epdPanelIsIdle() ? 1 : 0);
      if (first < 0) {
        first = raw;
      } else if (raw != first) {
        changed = true;
      }
      last = raw;
      level_since = t;
      next_tick = t + 1000;
    } else if (t >= next_tick) {
      // 电平没变也每秒打印一次，证明探针还在跑
      Serial.printf("[EPD] probe: t=%u ms BUSY=%d (stable)\n", (unsigned)t, raw);
      next_tick = t + 1000;
    }
    // 已经看到电平翻转并且稳定 2 秒 → 刷新结束，提前结束探针
    if (changed && (t - level_since) >= 2000) {
      break;
    }
    delay(sample_ms);
  }
  Serial.println("[EPD] probe: BUSY timeline end");
}
