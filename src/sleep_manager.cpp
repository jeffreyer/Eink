#include "sleep_manager.h"
#include "common.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "module_registry.h"
#include "epd_async.h"

// 进入深度睡眠
void enter_deep_sleep(void) {
  Serial.println("Entering deep sleep mode...");

  uint32_t wake_ms = 0;

  // 异步刷屏：面板刷新进行中，MCU 直接休眠（省电），定时唤醒完成面板断电；
  // 此时不启用 GPIO 唤醒（避免按键中途唤醒打断刷新）
  bool async_refresh_pending = epdAsyncIsPending();
  if (async_refresh_pending) {
    // 唤醒时刻按本次刷新类型取（局部刷比全刷快得多）
    wake_ms = epdAsyncWakeMs();
    Serial.printf("Deep sleep: async EPD refresh pending, wake in %u ms to power off\n",
                  (unsigned)wake_ms);
  } else {
    gpio_wakeup_enable((gpio_num_t)KEY_UP,GPIO_INTR_LOW_LEVEL);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << KEY_UP,ESP_GPIO_WAKEUP_GPIO_LOW);
    gpio_wakeup_enable((gpio_num_t)KEY_DOWN,GPIO_INTR_LOW_LEVEL);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << KEY_DOWN,ESP_GPIO_WAKEUP_GPIO_LOW);

    // 按当前模块的定时唤醒间隔配置定时唤醒（0 = 不启用，仅按键唤醒）
    uint32_t wake_seconds = module_registry_get_wake_interval();
    if (wake_seconds > 0) {
      wake_ms = wake_seconds * 1000;
      Serial.printf("Deep sleep: timer wakeup in %u seconds\n", (unsigned)wake_seconds);
    }
  }

  if (wake_ms > 0) {
    esp_sleep_enable_timer_wakeup((uint64_t)wake_ms * 1000ULL);   // 参数单位：微秒
  }

  // 异步刷屏中：保持面板控制引脚电平，避免休眠期间引脚浮空干扰/复位面板
  epdAsyncPrepareSleep();

  esp_deep_sleep_start();
}
