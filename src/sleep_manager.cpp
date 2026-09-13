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

  // 按当前模块的定时唤醒间隔配置深度休眠定时唤醒（0 = 不启用，仅按键唤醒）
  uint32_t wake_seconds = module_registry_get_wake_interval();

  // 异步刷屏：面板刷新进行中，MCU 直接休眠（省电），定时唤醒完成面板断电；
  // 此时不启用 GPIO 唤醒（避免按键中途唤醒打断刷新）
  bool async_refresh_pending = epdAsyncIsPending();
  if (async_refresh_pending) {
    wake_seconds = EPD_ASYNC_REFRESH_SLEEP_S;
    Serial.printf("Deep sleep: async EPD refresh pending, wake in %u seconds to power off\n",
                  (unsigned)wake_seconds);
  }

  if (!async_refresh_pending) {
    gpio_wakeup_enable((gpio_num_t)KEY_UP,GPIO_INTR_LOW_LEVEL);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << KEY_UP,ESP_GPIO_WAKEUP_GPIO_LOW);
    gpio_wakeup_enable((gpio_num_t)KEY_DOWN,GPIO_INTR_LOW_LEVEL);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << KEY_DOWN,ESP_GPIO_WAKEUP_GPIO_LOW);
  }

  if (wake_seconds > 0) {
    esp_sleep_enable_timer_wakeup(
        (uint64_t)wake_seconds * 1000000ULL   // 单位：微秒
    );
    Serial.printf("Deep sleep: timer wakeup in %u seconds\n", (unsigned)wake_seconds);
  }

  // 异步刷屏中：保持面板控制引脚电平，避免休眠期间引脚浮空干扰/复位面板
  epdAsyncPrepareSleep();

  esp_deep_sleep_start();
}
