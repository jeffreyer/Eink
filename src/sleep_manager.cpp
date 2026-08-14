#include "sleep_manager.h"
#include "common.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"
#include "module_registry.h"
#ifdef INK6
#include "eink6.h"
#endif

uint32_t s_idle_timeout_ms = IDLE_TIMEOUT_DEFAULT*1000;
static bool s_initialized = false;
static uint32_t last_active = 0;

// 进入深度睡眠
void enter_deep_sleep(void) {
  Serial.println("Entering deep sleep mode...");

  // 按当前模块的定时唤醒间隔配置深度休眠定时唤醒（0 = 不启用，仅按键唤醒）
  uint32_t wake_seconds = module_registry_get_wake_interval();
  bool async_refresh_pending = false;

  #ifdef INK6
  // 异步刷新：面板刷新进行中，MCU 直接休眠，仅定时唤醒完成面板断电，
  // 不启用 GPIO 唤醒（避免按键中途唤醒打断刷新）
  async_refresh_pending = epdIsRefreshPending();
  if (async_refresh_pending) {
    wake_seconds = EPD_ASYNC_REFRESH_SLEEP_S;
    Serial.printf("Deep sleep: async EPD refresh pending, wake in %u seconds to power off\n",
                  (unsigned)wake_seconds);
  }
  #endif

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

  esp_deep_sleep_start();


  // gpio_wakeup_enable((gpio_num_t)KEY_UP, GPIO_INTR_LOW_LEVEL);
  // gpio_wakeup_enable((gpio_num_t)KEY_DOWN, GPIO_INTR_LOW_LEVEL);

  // // 设置唤醒源为 GPIO（必须调用）
  // esp_sleep_enable_gpio_wakeup();
  // esp_light_sleep_start();

  // sleep_manager_reset_idle_timer();
  // Serial.end();
  // delay(100);
  // Serial.begin(115200);
}

int sleep_manager_init() {
  if (s_initialized) {
    return -1;
  }
  s_initialized = true;

  return 0;
}

int sleep_manager_start(void) {
  if (!s_initialized) {
    return -1;
  }
  
  return 0;
}

void sleep_manager_reset_idle_timer() {
  last_active = millis();
}

void sleep_manager_update() {
  if (!s_initialized) {
    return;
  }
  
  uint32_t now = millis();
  
  if (s_idle_timeout_ms==0)
    return;

  uint32_t elapsed = now - last_active;
  
  if (elapsed >= s_idle_timeout_ms) {
    // main_save_config();
    enter_deep_sleep();
  }
}
