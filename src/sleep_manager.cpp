#include "sleep_manager.h"
#include "common.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"

uint8_t brightness_max=10;
uint8_t user_brightness_max=10;
uint8_t fade_delay_ms=20;

uint32_t s_idle_timeout_ms = IDLE_TIMEOUT_DEFAULT*1000;
static uint32_t s_last_fade_time = 0;
static bool s_initialized = false;
static bool s_fading = false;
static bool s_preparing_sleep = false;
static uint32_t last_active = 0;
static float last_gx=0,last_gy=0,last_gz=0;
static uint32_t last_p = 0;
long last_trigger_time = 0;

// 检查按键是否被按下（低电平）
bool is_key_pressed(void) {
  return false;
}

// 进入深度睡眠
void enter_deep_sleep(void) {
  Serial.println("Entering deep sleep mode...");

  // pinMode(KEY_DOWN, INPUT);

  // gpio_wakeup_enable(
  //     (gpio_num_t)KEY_DOWN,
  //     GPIO_INTR_LOW_LEVEL
  // );

  // esp_deep_sleep_enable_gpio_wakeup(
  //     1ULL << KEY_DOWN,
  //     ESP_GPIO_WAKEUP_GPIO_LOW
  // );

  // esp_deep_sleep_start();


  gpio_wakeup_enable((gpio_num_t)KEY_DOWN, GPIO_INTR_LOW_LEVEL);

  // 设置唤醒源为 GPIO（必须调用）
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();

  sleep_manager_reset_idle_timer();
  Serial.end();
  delay(100);
  Serial.begin(115200);
}

int sleep_manager_init() {
  if (s_initialized) {
    return -1;
  }
  s_last_fade_time = 0;
  s_initialized = true;
  s_fading = false;
  s_preparing_sleep = false;

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
  Serial.println("Sleep manager: Idle timer reset");
}

void sleep_manager_update() {
  if (!s_initialized) {
    return;
  }
  
  uint32_t now = millis();
  
  // 如果正在准备睡眠（降低亮度），执行渐变步骤并检查是否完成
  if (s_preparing_sleep) {
      s_preparing_sleep = false;
      main_save_config();
      enter_deep_sleep();
  }

  if (s_idle_timeout_ms==0)
    return;


  uint32_t elapsed = now - last_active;
  
  if (elapsed >= s_idle_timeout_ms) {
    main_save_config();
    enter_deep_sleep();
  }
}
