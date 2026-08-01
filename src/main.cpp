#include "common.h"
#include "touch_icons.h"
#include "esp_sleep.h"
#include "sleep_manager.h"
#include "ble_config.h"
#include "app_control.h"
#include "module_registry.h"
#include "lua_hardware_api.h"
#include "auto_ota.h"
#include "time_calibration.h"
#include <Preferences.h>
#include "battery.h"
#include "cmd_handler.h"
#include "eink.h"

// Button status enumeration for better code readability
enum ButtonStatus {
  BTN_NONE = 0,      // No action
  BTN_CLICK = 1,     // Short click (subpage switch)
  BTN_MODULE = 2,    // Long press - switch module
  BTN_SLEEP = 3,     // Very long press - enter sleep
  BTN_BLE = 4        // Long press - toggle BLE config
};

ButtonStatus btn_status = BTN_NONE;
uint32_t tm_chk_bat=0;
uint32_t key_up_hold=0,key_down_hold=0;
bool key_up_triggered=false,key_down_triggered=false;

void main_load_config(){
  Preferences prefs;
  prefs.begin("bottle", true);
  page_index = prefs.getInt("page_index");
  user_brightness_max = prefs.getInt("brightness", user_brightness_max);
  s_idle_timeout_ms = prefs.getInt("sleep_sec",IDLE_TIMEOUT_DEFAULT)*1000;
  is_chk_bat = prefs.getInt("chk_bat",0);
  prefs.end();
}

void main_save_config(){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putInt("page_index",page_index);
  prefs.putInt("brightness",user_brightness_max);
  prefs.end();

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->unload) {
    module->unload();
  }
}

int32_t app_get_page_count(void) {
  return module_registry_count();
}

int32_t app_get_page_index(void) {
  return page_index;
}

int32_t app_get_subpage_index(void) {
  return subpage_index;
}

void app_set_subpage(int32_t subpage) {
  subpage_index = subpage;
}

bool app_set_module_enabled(int32_t page, bool enabled) {
  if (page < 0 || page >= module_registry_count()) {
    return false;
  }

  bool was_enabled = module_registry_is_enabled((uint8_t)page);
  module_registry_set_enabled((uint8_t)page, enabled);
  bool now_enabled = module_registry_is_enabled((uint8_t)page);
  if (was_enabled == now_enabled) {
    return now_enabled == enabled;
  }

  if (!now_enabled && page == page_index) {
    const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
    if (current && current->unload) {
      current->unload();
    }
    page_index = module_registry_normalize_index(page_index);
    subpage_index = 0;
    const module_descriptor_t* next = module_registry_get((uint8_t)page_index);
    if (next && next->setup) {
      next->setup();
    }

  }

  return true;
}

void app_set_page(int32_t page, int32_t subpage) {
  if (page < 0 || page >= module_registry_count()) {
    return;
  }
  if (!module_registry_is_enabled((uint8_t)page)) {
    return;
  }

  if (page == page_index) {
    app_set_subpage(subpage);
    return;
  }

  const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
  if (current && current->unload) {
    current->unload();
  }
  page_index = page;
  subpage_index = subpage;
  const module_descriptor_t* next = module_registry_get((uint8_t)page_index);
  if (next && next->setup) {
    next->setup();
  }

  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putInt("page_index", page_index);
  prefs.end();
}


void check_btn(){
  uint32_t now = millis();

  if (digitalRead(KEY_UP) == LOW) {
    delay(100); // Debounce delay
    if (digitalRead(KEY_UP) == LOW && key_up_triggered==false) {
      if (key_up_hold>0) {
        uint32_t hold_duration = now - key_up_hold;
        if (hold_duration >= 3000) {
          key_up_triggered=true;
          key_up_hold = 0;
          Serial.println("KEY_UP long press detected");
          btn_status = BTN_BLE;
        }
      }
      else {
        key_up_hold=millis();
      }
    }
  }
  else {
    if (key_up_hold > 0 && key_up_triggered==false) {
      uint32_t hold_duration = now - key_up_hold;
      key_up_hold = 0;
      Serial.println("KEY_UP press detected");

      // gui_draw();
      ink_draw();
    }
    key_up_triggered=false;
  }
  if (digitalRead(KEY_DOWN) == LOW) {
    delay(100); // Debounce delay
    if (digitalRead(KEY_DOWN) == LOW && key_down_triggered==false) {
      if (key_down_hold>0) {
        uint32_t hold_duration = now - key_down_hold;
        if (hold_duration >= 3000) {
          key_down_triggered=true;
          key_down_hold = 0;
          Serial.println("KEY_DOWN long press detected");
          // btn_status = BTN_SLEEP;
        }
      }
      else {
        key_down_hold=millis();
      }
    }
  }
  else {
    if (key_down_hold > 0 && key_down_triggered==false) {
      uint32_t hold_duration = now - key_down_hold;
      key_down_hold = 0;
      Serial.println("KEY_DOWN press detected");

      // enter_deep_sleep();
      btn_status = BTN_CLICK;
    }
    key_down_triggered=false;
  }
 
  if (btn_status == BTN_CLICK){
    btn_status = BTN_NONE;
    if (ble_config_is_enabled()) {
      return;
    }
    subpage_index++;
  }
  else if (btn_status == BTN_MODULE){
    if (ble_config_is_enabled()) {
      btn_status = BTN_NONE;
      return;
    }
    btn_status = BTN_NONE;
    const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
    if (current && current->unload) {
      current->unload();
    }

    page_index = module_registry_next_enabled(page_index);
    if (page_index < 0) {
      page_index = module_registry_normalize_index(0);
    }
    subpage_index=0;

    const module_descriptor_t* next = module_registry_get((uint8_t)page_index);
    if (next && next->setup) {
      next->setup();
    }

  }
  else if (btn_status == BTN_BLE){
    btn_status = BTN_NONE;
    ble_config_toggle();
    if (!ble_config_is_enabled()){ //退出蓝牙后重新启动模块
      // 重置休眠计时器，避免立即休眠
      sleep_manager_reset_idle_timer();

      const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
      int ret;
      if (current && current->unload) {
        ret = current->unload();
      }
      if (current && current->setup) {
        ret = current->setup();
      }
    }
  }
  else if (btn_status == BTN_SLEEP){
    // Sleep action - save config and enter deep sleep
    main_save_config();
    enter_deep_sleep();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(KEY_UP, INPUT);
  pinMode(KEY_DOWN, INPUT);

  // rgb_init();

  // // 6. 检查并执行自动 OTA 更新（如果有 firmware.bin）
  // if (auto_ota_check_and_update()) {
  //   // OTA 更新执行中或失败，函数内部会处理重启或清理
  //   // 如果到这里说明更新失败，继续正常启动
  //   log_e("[Setup] OTA update failed or completed, continuing normal boot");
  // }

  // // 7. 恢复时区设置
  // restore_timezone();

  // // 8. 初始化时间校准模块
  // TimeCalibration::init();

  if (is_chk_bat)
    check_battery_init();

  // main_load_config();

  // module_registry_init();

  // page_index = module_registry_normalize_index(page_index);

  // const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  // if (module && module->setup) {
  //   module->setup();
  // }

  sleep_manager_init();

  sleep_manager_start();

  init_eink();

  // btn_status = BTN_BLE;
}

void loop() {

  check_btn();

  if (is_chk_bat && millis() - tm_chk_bat > 3000) { // 每30秒检查一次电池状态
    tm_chk_bat = millis();
    check_bat();
    // if (is_low_bat) {
    //   draw_low_battery_hint();
    //   enter_deep_sleep();
    // }
  }

  check_cmd();

  ble_config_update();

  if (ble_config_is_enabled()) {
    ble_config_render_mode();
    delay(30);
    return;
  }

  sleep_manager_update();

  // const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  // if (module && module->loop) {
  //   module->loop();
  // }

  delay(10);

}
