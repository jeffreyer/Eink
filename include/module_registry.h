#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*module_func_t)(void);

typedef struct {
  const char* id;
  const char* name;
  const char* version;
  const char* author;
  const char* description;
  const char* host;
  const char* runtime;
  const char* script_path;
  module_func_t setup;
  module_func_t unload;
  module_func_t loop;
  module_func_t subpage_next;  // KEY_DOWN 短按/唤醒："下一项"钩子（nullptr → 默认 subpage_index++）
  module_func_t subpage_prev;  // KEY_UP 短按："上一项"钩子（nullptr → 默认 subpage_index--）
  module_func_t wake_interval; // 深度休眠定时唤醒间隔（返回秒；0/空 = 不启用定时唤醒）
  uint8_t config_count;
  bool built_in;
} module_descriptor_t;

void module_registry_init(void);
uint8_t module_registry_count(void);
const module_descriptor_t* module_registry_get(uint8_t index);
bool module_registry_is_enabled(uint8_t index);
void module_registry_set_enabled(uint8_t index, bool enabled);
int32_t module_registry_next_enabled(int32_t index);
int32_t module_registry_normalize_index(int32_t index);
String module_registry_manifest_json(int32_t index);

// 标记指定模块的配置已变更（NVS 已保存，由 BLE 配置写入时调用）
void module_registry_mark_config_changed(const char* module_id);
// 主循环中调用：若当前模块配置被修改，重新加载该模块（unload + setup）
void module_registry_update(void);
// 强制重新加载当前模块（unload + setup），用于手动刷新显示
void module_registry_refresh_current(void);
// 深度休眠按键唤醒标记：主程序在 setup 检测到 GPIO 唤醒时记录唤醒按键
// （KEY_UP / KEY_DOWN），由 check_btn 判定：短按 → 上一项/下一项
// （KEY_DOWN 唤醒短按后进入休眠）；长按 → 切换模块 / 开关 BLE
void module_registry_mark_wake_key(uint8_t key);
uint8_t module_registry_consume_wake_key(void);
// 只读查询（不消费）：模块 setup 据此决定是否预显示（按键唤醒时相册不预显示）
uint8_t module_registry_peek_wake_key(void);
// 查询当前模块的深度休眠定时唤醒间隔（秒；0 = 不启用）
uint32_t module_registry_get_wake_interval(void);

#ifdef __cplusplus
}
#endif
