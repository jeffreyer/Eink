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
#include <SPIFFS.h>
#ifdef INK6
#include "eink6.h"
#else
#include "eink.h"
#include "Display_EPD_W21.h"
#endif

// Button status enumeration for better code readability
enum ButtonStatus {
  BTN_NONE = 0,      // No action
  BTN_CLICK = 1,     // Short click (subpage switch)
  BTN_MODULE = 2,    // Long press - switch module
  BTN_BLE = 3        // Long press - toggle BLE config
};

ButtonStatus btn_status = BTN_NONE;
uint32_t tm_chk_bat=0;
uint32_t key_up_hold=0,key_down_hold=0;
bool key_up_triggered=false,key_down_triggered=false;
// 休眠唤醒后长按切模块：等待 KEY_DOWN 释放再进入深度休眠
// （按键还按着时不能直接休眠，低电平 GPIO 唤醒会立刻再次唤醒）
static bool s_sleep_after_wake_switch = false;

void main_load_config(){
  Preferences prefs;
  prefs.begin("bottle", true);
  page_index = prefs.getInt("page_index");
  s_idle_timeout_ms = prefs.getInt("sleep_sec",IDLE_TIMEOUT_DEFAULT)*1000;
  prefs.end();
}

void main_save_config(){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putInt("page_index",page_index);
  prefs.end();

  // const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  // if (module && module->unload) {
  //   module->unload();
  // }
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


// KEY_UP 短按动作：消费唤醒意图（KEY_UP 唤醒短按后进入休眠）；
// 模块"上一项"钩子（相册=上一张），无钩子则 subpage-1
static void key_up_short_press_action() {
  Serial.println("KEY_UP press detected");
  if (ble_config_is_enabled()) {
    return;
  }
  bool wake_short_press = (module_registry_consume_wake_key() == KEY_UP);
  const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
  if (current && current->subpage_prev) {
    // 模块自定义"上一项"（如相册显示上一张）
    current->subpage_prev();
    Serial.println("KEY_UP: module prev action");
  } else if (!wake_short_press && subpage_index > 0) {
    subpage_index--;
    Serial.printf("Subpage: %d\n", subpage_index);
  }
  if (wake_short_press) {
    Serial.println("KEY_UP wake short press -> enter deep sleep");
    enter_deep_sleep();
  }
  // sleep_manager_reset_idle_timer();  // 有按键活动，推迟休眠便于查看
}

// KEY_DOWN 短按动作：消费唤醒意图（KEY_DOWN 唤醒短按后进入休眠）；
// 模块"下一项"钩子（相册=下一张），无钩子则 subpage+1
static void key_down_short_press_action() {
  Serial.println("KEY_DOWN press detected");
  if (ble_config_is_enabled()) {
    Serial.println("KEY_DOWN: ignored while BLE enabled");
    return;
  }
  bool wake_short_press = (module_registry_consume_wake_key() == KEY_DOWN);
  const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
  if (current && current->subpage_next) {
    // 模块自定义"下一项"（如相册显示下一张）
    current->subpage_next();
    Serial.println("KEY_DOWN: module next action");
  } else if (!wake_short_press) {
    subpage_index++;
    Serial.printf("Subpage: %d\n", subpage_index);
  }
  if (wake_short_press) {
    Serial.println("KEY_DOWN wake short press -> enter deep sleep");
    enter_deep_sleep();
  }
  // sleep_manager_reset_idle_timer();  // 有按键活动，推迟休眠便于查看
}

void check_btn(){
  uint32_t now = millis();

  // 深度休眠按键唤醒：若唤醒按键在轮询开始前已松开（setup 显示耗时较长），
  // 补一次短按事件，避免按键事件丢失（若仍按住则由下方轮询正常判定长按）
  uint8_t wake_key = module_registry_peek_wake_key();
  if (wake_key != 0 && digitalRead(wake_key) != LOW) {
    if (wake_key == KEY_DOWN) {
      key_down_short_press_action();  // 内部消费 KEY_DOWN 唤醒意图并休眠
    } else {
      key_up_short_press_action();    // 内部消费 KEY_UP 唤醒意图并休眠
    }
  }

  if (digitalRead(KEY_UP) == LOW) {
    delay(100); // Debounce delay
    if (digitalRead(KEY_UP) == LOW && key_up_triggered==false) {
      if (key_up_hold>0) {
        uint32_t hold_duration = now - key_up_hold;
        // 长按（3秒）：开关 BLE 配置（按住即触发，保持原行为）
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
      key_up_short_press_action();
    }
    key_up_triggered=false;
  }
  if (digitalRead(KEY_DOWN) == LOW) {
    delay(100); // Debounce delay
    if (digitalRead(KEY_DOWN) == LOW && key_down_triggered==false) {
      if (key_down_hold>0) {
        uint32_t hold_duration = now - key_down_hold;
        // 长按（3秒）：切换下一个模块（按住即触发，与 KEY_UP 一致）
        if (hold_duration >= 3000) {
          key_down_triggered=true;
          key_down_hold = 0;
          Serial.println("KEY_DOWN long press detected");
          btn_status = BTN_MODULE;
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
      key_down_short_press_action();
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
    // 长按已消费唤醒意图：切换模块时清除，避免残留标记影响下次按键
    uint8_t wake_key = module_registry_consume_wake_key();
    bool wake_switch = (wake_key == KEY_DOWN);
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
    main_save_config();
    if (wake_switch) {
      // 休眠唤醒后的长按：切换完成后等按键释放再进入深度休眠
      s_sleep_after_wake_switch = true;
      Serial.println("KEY_DOWN wake long press: module switched, sleep after key release");
    }

  }
  else if (btn_status == BTN_BLE){
    btn_status = BTN_NONE;
    // 长按已消费唤醒意图：进入 BLE 时清除，避免残留标记影响下次按键
    module_registry_consume_wake_key();
    ble_config_toggle();
    if (!ble_config_is_enabled()){ //退出蓝牙后重新启动模块

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
}

void setup() {
  Serial.begin(115200);
  pinMode(KEY_UP, INPUT);
  pinMode(KEY_DOWN, INPUT);
  pinMode(BLE_LIGHT, OUTPUT);
  digitalWrite(BLE_LIGHT, HIGH);

  SPIFFS.begin(true);

  #ifdef INK6
  init_eink6();
  #else
  init_eink();
  EPD_init_Fast2();
  EPD_sleep();
  #endif

  // // 6. 检查并执行自动 OTA 更新（如果有 firmware.bin）
  // if (auto_ota_check_and_update()) {
  //   // OTA 更新执行中或失败，函数内部会处理重启或清理
  //   // 如果到这里说明更新失败，继续正常启动
  //   log_e("[Setup] OTA update failed or completed, continuing normal boot");
  // }

  // 7. 恢复时区设置
  restore_timezone();

  // 8. 初始化时间校准模块
  TimeCalibration::init();

  main_load_config();

  check_battery_init();

  module_registry_init();

  page_index = module_registry_normalize_index(page_index);

  #ifdef INK6
  // 异步刷新唤醒：30 秒前休眠时面板仍在刷新，本次唤醒仅完成面板断电后继续休眠
  // （不重绘屏幕，避免打断刷新）
  if (epdIsRefreshPending()) {
    Serial.println("[EPD] async refresh wake: finishing power off");
    epdFinishPowerOff();
    epdClearRefreshPending();
    enter_deep_sleep();
  }
  #endif

  // 深度休眠按键唤醒：记录唤醒按键，由 check_btn 判定短按/长按
  // （短按 → 上一项/下一项，KEY_DOWN 唤醒短按后休眠；长按 → 切换模块/开关 BLE）
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
    uint64_t gpio_status = esp_sleep_get_gpio_wakeup_status();
    if (gpio_status & (1ULL << KEY_DOWN)) {
      module_registry_mark_wake_key(KEY_DOWN);
      Serial.println("Woke by KEY_DOWN");
    } else if (gpio_status & (1ULL << KEY_UP)) {
      module_registry_mark_wake_key(KEY_UP);
      Serial.println("Woke by KEY_UP");
    }
  } else {
    // 非按键唤醒：清除可能残留的标记，避免误触发"唤醒短按"
    module_registry_consume_wake_key();
  }

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->setup) {
    Serial.println("Setting up module: " + String(module->name));
    module->setup();
  }

  // 定时唤醒：模块已重绘，立即进入深度休眠节省电量。
  // 异步刷屏期间 MCU 直接休眠，30 秒后唤醒完成面板断电，再按模块周期继续休眠
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Timer wake: module redrawn, entering deep sleep");
    enter_deep_sleep();
  }

  // 初始显示已完成：之后 sys.wake_source() 返回 0，
  // BLE"刷新显示"等显式重载时 Lua setup 会正常重绘
  lua_hardware_mark_boot_wake_consumed();

  sleep_manager_init();

  sleep_manager_start();

}

void loop() {

  check_btn();

  #ifdef INK6
  // 面板断电兜底：异步刷新后若系统未进入休眠（实时交互/BLE），延迟断电面板
  epdPanelPowerMaintain();
  #endif

  // 休眠唤醒长按切模块：KEY_DOWN 释放后进入深度休眠
  if (s_sleep_after_wake_switch && digitalRead(KEY_DOWN) != LOW) {
    s_sleep_after_wake_switch = false;
    enter_deep_sleep();
  }

  // if (millis() - tm_chk_bat > 30000) { // 每30秒检查一次电池状态
  //   tm_chk_bat = millis();
  //   check_bat();
  //   // if (is_low_bat) {
  //   //   draw_low_battery_hint();
  //   //   enter_deep_sleep();
  //   // }
  // }

  check_cmd();

  ble_config_update();

  sleep_manager_update();

  if (ble_config_is_enabled()) {
    delay(30);
    return;
  }

  // 若当前模块配置在 BLE 会话中被修改，重新加载模块使新配置生效
  module_registry_update();

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->loop) {
    module->loop();
  }

  delay(10);

}
