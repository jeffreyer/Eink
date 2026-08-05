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

#ifdef __cplusplus
}
#endif
