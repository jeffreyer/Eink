#include "module_registry.h"
#include "common.h"
#include "lua_hardware_api.h"
#include "gallery.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <vector>
#include <dirent.h>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#define MAX_DYNAMIC_MODULES 20

// Built-in modules (static) - configs now stored in NVS as JSON
// 移除 const 以便在初始化时更新 config_count
static module_descriptor_t k_builtin_modules[] = {
    {
        .id = "gallery",
        .name = "相册",
        .version = "1.0.0",
        .author = "System",
        .description = "照片相册展示",
        .host = "native",
        .runtime = "esp32",
        .script_path = nullptr,
        .setup = gallery_setup,
        .unload = gallery_unload,
        .loop = nullptr,              // 统一模型：模块无常驻 loop，周期刷新由定时唤醒驱动
        .subpage_next = gallery_next_image,
        .subpage_prev = gallery_prev_image,
        .wake_interval = gallery_wake_interval,
        .config_count = 0,
        .built_in = true
    }
};

// Dynamic modules storage
static module_descriptor_t s_dynamic_modules[MAX_DYNAMIC_MODULES];
static uint8_t s_dynamic_module_count = 0;

// Combined module list
static const module_descriptor_t* s_all_modules[sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]) + MAX_DYNAMIC_MODULES];
static uint8_t s_total_module_count = 0;

// Enabled state for all modules
static bool s_enabled[sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]) + MAX_DYNAMIC_MODULES];

// Dynamic module string storage (for id, name, etc.)
// 增加缓冲区大小以容纳 UTF-8 中文字符（每个中文字符占3字节）
static char s_dynamic_strings[MAX_DYNAMIC_MODULES][6][128]; // 6 strings per module, 128 chars each

// Lua state for each dynamic module
static lua_State* s_dynamic_lua_states[MAX_DYNAMIC_MODULES];
static bool s_dynamic_lua_loaded[MAX_DYNAMIC_MODULES];

// Forward declarations for dynamic Lua module functions
static int dynamic_lua_setup(void);
static int dynamic_lua_load_module(bool call_setup);
static int dynamic_lua_loop(void);
static int dynamic_lua_unload(void);
static int dynamic_lua_wake_interval(void);
static int dynamic_lua_subpage_next(void);
static int dynamic_lua_subpage_prev(void);

// Current dynamic module being executed
static int s_current_dynamic_module = -1;

// 待处理配置变更的模块 id（BLE 写入 NVS 后标记，主循环消费）
static char s_pending_config_module[64] = {0};

// KEY_DOWN 唤醒标记（"下一项"意图，由当前模块 setup 消费）
static uint8_t s_wake_key = 0;  // 0=无, KEY_UP/KEY_DOWN

static String json_escape(const char* value) {
  String s;
  if (!value) return s;
  while (*value) {
    if (*value == '"' || *value == '\\') {
      s += '\\';
    }
    s += *value;
    value++;
  }
  return s;
}


// 更新built-in模块的 config_count（从NVS读取配置定义）
static void update_module_config_counts() {
  uint8_t builtin_count = sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]);

  // 更新 builtin 模块的 config_count（从配置文件读取）
  for (uint8_t i = 0; i < builtin_count; i++) {
    module_descriptor_t* module = &k_builtin_modules[i];
    if (module->id != nullptr && strlen(module->id) > 0) {
      // 内置模块的 script_path 为 nullptr，默认从 /spiffs 读取配置
      String config_def = load_config_definition(module->id, nullptr);
      if (config_def.length() > 0) {
        // 计算配置项数量
        int count = 0;
        int pos = 0;
        while ((pos = config_def.indexOf("\"key\"", pos)) >= 0) {
          count++;
          pos += 5;
        }
        module->config_count = count;
        Serial.print("Module registry: Updated ");
        Serial.print(module->id);
        Serial.print(" config_count = ");
        Serial.println(count);
      }
    }
  }
}

// Parse metadata from Lua file comments
// Format: -- @key: value
static void parse_lua_metadata(const String& content, module_descriptor_t* module, uint8_t module_idx) {
  String id, name, version, author, description;

  // Default values
  name = "Unknown";
  version = "1.0.0";
  author = "Unknown";
  description = "Lua module";

  // Parse metadata from comments
  int pos = 0;
  while (pos < (int)content.length()) {
    int line_end = content.indexOf('\n', pos);
    if (line_end < 0) line_end = content.length();

    String line = content.substring(pos, line_end);
    line.trim();

    if (line.startsWith("-- @name:")) {
      name = line.substring(9);
      name.trim();
    } else if (line.startsWith("-- @version:")) {
      version = line.substring(12);
      version.trim();
    } else if (line.startsWith("-- @author:")) {
      author = line.substring(11);
      author.trim();
    } else if (line.startsWith("-- @description:")) {
      description = line.substring(16);
      description.trim();
    } else if (line.startsWith("-- @id:")) {
      id = line.substring(7);
      id.trim();
    }

    // Stop parsing after first non-comment line
    if (!line.startsWith("--") && line.length() > 0) {
      break;
    }

    pos = line_end + 1;
  }

  // Store strings in static storage
  if (id.length() == 0) {
    // Generate ID from filename if not specified
    id = String(module->script_path);

    // 从完整路径中提取文件名（去除目录前缀）
    int last_slash = id.lastIndexOf('/');
    if (last_slash >= 0) {
      id = id.substring(last_slash + 1);
    }

    // 去除 .lua 扩展名
    id.replace(".lua", "");
  }

  // 使用 snprintf 确保字符串以 null 结尾，缓冲区大小为 128 字节
  snprintf(s_dynamic_strings[module_idx][0], 128, "%s", id.c_str());
  snprintf(s_dynamic_strings[module_idx][1], 128, "%s", name.c_str());
  snprintf(s_dynamic_strings[module_idx][2], 128, "%s", version.c_str());
  snprintf(s_dynamic_strings[module_idx][3], 128, "%s", author.c_str());
  snprintf(s_dynamic_strings[module_idx][4], 128, "%s", description.c_str());

  module->id = s_dynamic_strings[module_idx][0];
  module->name = s_dynamic_strings[module_idx][1];
  module->version = s_dynamic_strings[module_idx][2];
  module->author = s_dynamic_strings[module_idx][3];
  module->description = s_dynamic_strings[module_idx][4];
}

// Load a dynamic Lua module from SPIFFS
static bool load_dynamic_lua_module(const char* filename, const char* base_dir) {
  if (s_dynamic_module_count >= MAX_DYNAMIC_MODULES) {
    Serial.println("Module registry: Maximum dynamic modules reached");
    return false;
  }

  // 构建完整路径
  String full_path = String(base_dir) + "/" + filename;

  // 使用 POSIX API 读取文件
  FILE* fp = fopen(full_path.c_str(), "r");
  if (!fp) {
    Serial.print("Module registry: Failed to open ");
    Serial.println(full_path);
    return false;
  }

  // 读取文件内容
  String content = "";
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
    content += buffer;
  }
  fclose(fp);

  if (content.length() == 0) {
    Serial.print("Module registry: Empty file ");
    Serial.println(full_path);
    return false;
  }

  Serial.print("Module registry: Loading ");
  Serial.print(filename);
  Serial.print(" from ");
  Serial.print(base_dir);
  Serial.print(" (");
  Serial.print(content.length());
  Serial.println(" bytes)");

  // Create module descriptor
  uint8_t idx = s_dynamic_module_count;
  module_descriptor_t* module = &s_dynamic_modules[idx];

  // Store script path (完整路径，包含目录前缀)
  snprintf(s_dynamic_strings[idx][5], 128, "%s", full_path.c_str());
  module->script_path = s_dynamic_strings[idx][5];

  // Parse metadata from file content
  parse_lua_metadata(content, module, idx);

  // Set module properties
  module->host = "lua";
  module->runtime = "lua-5.4.7";
  module->setup = dynamic_lua_setup;
  module->unload = dynamic_lua_unload;
  module->loop = dynamic_lua_loop;
  // 仅当脚本定义了按键钩子函数时注册（避免无钩子模块在按键唤醒时被无谓加载）
  module->subpage_next = (content.indexOf("function subpage_next") >= 0) ? dynamic_lua_subpage_next : nullptr;
  module->subpage_prev = (content.indexOf("function subpage_prev") >= 0) ? dynamic_lua_subpage_prev : nullptr;
  module->wake_interval = dynamic_lua_wake_interval;
  module->config_count = 0;
  module->built_in = false;

  // 从配置文件读取配置定义并计算 config_count
  // 配置文件位置根据脚本路径决定：/spiffs 或 /extflash
  if (module->id != nullptr && strlen(module->id) > 0) {
    String config_def = load_config_definition(module->id, module->script_path);
    if (config_def.length() > 0) {
      // 计算配置项数量（简单统计 "key" 出现次数）
      int count = 0;
      int pos = 0;
      while ((pos = config_def.indexOf("\"key\"", pos)) >= 0) {
        count++;
        pos += 5;
      }
      module->config_count = count;
    }
  }

  Serial.print("  ID: ");
  Serial.print(module->id ? module->id : "NULL");
  Serial.print(", Name: ");
  Serial.println(module->name ? module->name : "NULL");

  s_all_modules[s_total_module_count++] = &s_dynamic_modules[s_dynamic_module_count];

  s_dynamic_module_count++;
  return true;
}

// Scan a directory for Lua modules
static int scan_directory_for_modules(const char* dir_path) {
  DIR* dir = opendir(dir_path);
  if (!dir) {
    Serial.print("Module registry: Failed to open directory ");
    Serial.println(dir_path);

    return -1;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    String filename = String(entry->d_name);
    // Check if it's a Lua file
    if (filename.endsWith(".lua")) {
      load_dynamic_lua_module(filename.c_str(), dir_path);
    }
  }

  closedir(dir);
  return 0;
}

void delay_scan_ext_modules(void *pvParameters) {
  int ret=-1;
  uint32_t start_time=millis();
  uint8_t old_all_cnt=s_total_module_count;
  
  while (ret==-1 && millis()-start_time<5000){
    delay(200);
    ret=scan_directory_for_modules("/extflash");
  }
  
  Preferences prefs;
  prefs.begin("modules", true);
  for (uint8_t i = old_all_cnt; i < s_total_module_count; i++) {
    String key = String(s_all_modules[i]->id) + "_en";
    s_enabled[i] = prefs.getBool(key.c_str(), true);
  }
  prefs.end();
  
  vTaskDelete(NULL);
}

// Scan SPIFFS and extflash for Lua modules
static void scan_dynamic_modules(void) {
  s_dynamic_module_count = 0;

  // Initialize Lua state arrays
  for (uint8_t i = 0; i < MAX_DYNAMIC_MODULES; i++) {
    s_dynamic_lua_states[i] = nullptr;
    s_dynamic_lua_loaded[i] = false;
  }

  Serial.println("Module registry: Scanning for dynamic modules...");

  // Scan SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Module registry: SPIFFS mount failed");
  } else {
    scan_directory_for_modules("/spiffs");
  }

  // Scan extflash
  // int ret=scan_directory_for_modules("/extflash");
  // if (ret==-1){
  //   xTaskCreatePinnedToCore(delay_scan_ext_modules, "delay_scan_ext_mod", 4096, NULL, 6, NULL, 0);
  // }

  Serial.print("Module registry: Found ");
  Serial.print(s_dynamic_module_count);
  Serial.println(" dynamic modules");
}

// Build combined module list
static void build_module_list(void) {
  s_total_module_count = 0;

  // Add built-in modules
  uint8_t builtin_count = sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]);
  for (uint8_t i = 0; i < builtin_count; i++) {
    s_all_modules[s_total_module_count++] = &k_builtin_modules[i];
  }

  Serial.print("Module registry: Total modules: ");
  Serial.println(s_total_module_count);
}

void module_registry_init(void) {
  Serial.println("Module registry: Initializing...");

  build_module_list();

  // Update config_count for built-in modules from config files
  update_module_config_counts();

  // Scan for dynamic modules and add them to the list
  scan_dynamic_modules();

  // Load enabled state from preferences
  Preferences prefs;
  prefs.begin("modules", true);
  for (uint8_t i = 0; i < s_total_module_count; i++) {
    String key = String(s_all_modules[i]->id) + "_en";
    s_enabled[i] = prefs.getBool(key.c_str(), true);
  }
  prefs.end();

  // Ensure at least one module is enabled
  bool any_enabled = false;
  for (uint8_t i = 0; i < s_total_module_count; i++) {
    any_enabled = any_enabled || s_enabled[i];
  }
  if (!any_enabled && s_total_module_count > 0) {
    s_enabled[0] = true;
  }

  Serial.println("Module registry: Initialization complete");
}

// 标记指定模块的配置已变更（NVS 已保存）
void module_registry_mark_config_changed(const char* module_id) {
  if (!module_id || strlen(module_id) == 0) {
    return;
  }
  snprintf(s_pending_config_module, sizeof(s_pending_config_module), "%s", module_id);
  Serial.print("Module registry: Config changed for ");
  Serial.println(module_id);
}

// 重载指定索引的模块（unload + setup）
static void reload_module_at(int32_t index) {
  if (index < 0 || index >= s_total_module_count) {
    return;
  }

  const module_descriptor_t* module = s_all_modules[index];
  if (!module) {
    return;
  }

  if (!module_registry_is_enabled((uint8_t)index)) {
    return;
  }

  Serial.print("Module registry: Reloading module ");
  Serial.println(module->id);

  if (module->unload) {
    module->unload();
  }
  if (module->setup) {
    module->setup();
  }
}

// 主循环中调用：若当前模块的配置被修改，重新加载该模块（unload + setup）
void module_registry_update(void) {
  if (s_pending_config_module[0] == 0) {
    return;
  }

  String changed_id = String(s_pending_config_module);
  s_pending_config_module[0] = 0;

  if (page_index < 0 || page_index >= s_total_module_count) {
    return;
  }

  const module_descriptor_t* module = s_all_modules[page_index];
  if (!module || !module->id || changed_id != module->id) {
    // 配置变更属于其他模块：切换过去时 setup() 会重新读取，无需处理
    return;
  }

  reload_module_at(page_index);
}

// 强制重新加载当前模块（unload + setup），用于手动刷新显示
void module_registry_refresh_current(void) {
  // 手动刷新会立即应用最新配置，清除待处理标记避免退出 BLE 时重复重载
  s_pending_config_module[0] = 0;
  reload_module_at(page_index);
}

void module_registry_mark_wake_key(uint8_t key) {
  s_wake_key = key;
}

uint8_t module_registry_consume_wake_key(void) {
  uint8_t value = s_wake_key;
  s_wake_key = 0;
  return value;
}

uint8_t module_registry_peek_wake_key(void) {
  return s_wake_key;
}

// 查询当前模块的深度休眠定时唤醒间隔（秒；0 = 不启用）
uint32_t module_registry_get_wake_interval(void) {
  extern int32_t page_index;
  if (page_index < 0 || page_index >= s_total_module_count) {
    return 0;
  }
  const module_descriptor_t* module = s_all_modules[page_index];
  if (!module || !module->wake_interval) {
    return 0;
  }
  int seconds = module->wake_interval();
  return seconds > 0 ? (uint32_t)seconds : 0;
}

uint8_t module_registry_count(void) {
  return s_total_module_count;
}

const module_descriptor_t* module_registry_get(uint8_t index) {
  if (index >= s_total_module_count) return nullptr;
  return s_all_modules[index];
}

bool module_registry_is_enabled(uint8_t index) {
  if (index >= s_total_module_count) return false;
  return s_enabled[index];
}

void module_registry_set_enabled(uint8_t index, bool enabled) {
  if (index >= s_total_module_count) return;

  if (!enabled) {
    bool other_enabled = false;
    for (uint8_t i = 0; i < s_total_module_count; i++) {
      if (i != index && s_enabled[i]) {
        other_enabled = true;
        break;
      }
    }
    if (!other_enabled) {
      Serial.println("module_registry_set_enabled: 拒绝禁用，至少需要保留一个启用的模块");
      return;
    }
  }

  s_enabled[index] = enabled;
  Preferences prefs;
  prefs.begin("modules", false);
  String key = String(s_all_modules[index]->id) + "_en";
  prefs.putBool(key.c_str(), enabled);
  prefs.end();

}

int32_t module_registry_next_enabled(int32_t index) {
  if (s_total_module_count == 0) return -1;
  for (uint8_t step = 1; step <= s_total_module_count; step++) {
    int32_t next = (index + step) % s_total_module_count;
    if (module_registry_is_enabled((uint8_t)next)) return next;
  }
  return -1;
}

int32_t module_registry_normalize_index(int32_t index) {
  if (s_total_module_count == 0) return -1;
  if (index < 0 || index >= s_total_module_count || !module_registry_is_enabled((uint8_t)index)) {
    return module_registry_next_enabled(index);
  }
  return index;
}

// Helper function to inject current config values into config JSON
static String inject_config_values(const String& config_json, const char* module_id) {
  if (config_json.length() == 0 || config_json == "[]") {
    return config_json;
  }

  String ns = String(module_id);
  String result = "";
  int pos = 0;

  while (pos < (int)config_json.length()) {
    // Find next config object
    int obj_start = config_json.indexOf('{', pos);
    if (obj_start < 0) {
      result += config_json.substring(pos);
      break;
    }

    // Copy everything before the object
    result += config_json.substring(pos, obj_start + 1);

    // Find the end of this object (handle nested braces)
    int obj_end = obj_start + 1;
    int brace_count = 1;
    bool in_string = false;
    bool escape_next = false;

    while (obj_end < (int)config_json.length() && brace_count > 0) {
      char c = config_json[obj_end];

      if (escape_next) {
        escape_next = false;
      } else if (c == '\\') {
        escape_next = true;
      } else if (c == '"') {
        in_string = !in_string;
      } else if (!in_string) {
        if (c == '{') {
          brace_count++;
        } else if (c == '}') {
          brace_count--;
        }
      }

      if (brace_count > 0) {
        obj_end++;
      }
    }

    if (brace_count != 0) {
      result += config_json.substring(obj_start + 1);
      break;
    }

    String obj_content = config_json.substring(obj_start + 1, obj_end);

    // Extract key
    String key = "";
    int key_pos = obj_content.indexOf("\"key\"");
    if (key_pos >= 0) {
      int colon = obj_content.indexOf(':', key_pos);
      if (colon >= 0) {
        int quote1 = obj_content.indexOf('"', colon);
        int quote2 = obj_content.indexOf('"', quote1 + 1);
        if (quote1 >= 0 && quote2 > quote1) {
          key = obj_content.substring(quote1 + 1, quote2);
        }
      }
    }

    // Extract type
    String type = "";
    int type_pos = obj_content.indexOf("\"type\"");
    if (type_pos >= 0) {
      int colon = obj_content.indexOf(':', type_pos);
      if (colon >= 0) {
        int quote1 = obj_content.indexOf('"', colon);
        int quote2 = obj_content.indexOf('"', quote1 + 1);
        if (quote1 >= 0 && quote2 > quote1) {
          type = obj_content.substring(quote1 + 1, quote2);
        }
      }
    }

    // Read current value from NVS (only if key exists)
    String value_str = "";
    if (key.length() > 0 && type.length() > 0) {
      Preferences prefs;
      prefs.begin(ns.c_str(), true);
      bool key_exists = prefs.isKey(key.c_str());

      if (key_exists) {
        if (type == "slider" || type == "number") {
          // Try both float and int (slider values might be saved as either type)
          float float_value = prefs.getFloat(key.c_str(), 0.0f);
          int int_value = prefs.getInt(key.c_str(), 0);

          // Prefer non-zero float, then non-zero int
          if (float_value != 0.0f) {
            value_str = String(float_value);
          } else if (int_value != 0) {
            value_str = String((float)int_value);
          }
        } else if (type == "switch") {
          int int_value = prefs.getInt(key.c_str(), 0);
          value_str = String(int_value);
        } else if (type == "text" || type == "color") {
          String str_value = prefs.getString(key.c_str(), "");
          if (str_value.length() > 0) {
            value_str = "\"" + json_escape(str_value.c_str()) + "\"";
          }
        } else if (type == "select") {
          // Select can be either string or int, try int first then string
          int int_value = prefs.getInt(key.c_str(), -999);  // Use sentinel value
          if (int_value != -999) {
            // Found int value (including 0)
            value_str = String(int_value);
          } else {
            // Try string value
            String str_value = prefs.getString(key.c_str(), "");
            if (str_value.length() > 0) {
              value_str = "\"" + json_escape(str_value.c_str()) + "\"";
            }
          }
        }
      }

      prefs.end();
    }

    // Copy object content
    result += obj_content;

    // Add value field if we have one
    if (value_str.length() > 0) {
      result += ",\"value\":" + value_str;
    }

    result += "}";
    pos = obj_end + 1;
  }

  return result;
}

String module_registry_manifest_json(int32_t index) {
  const module_descriptor_t* module = module_registry_get((uint8_t)index);
  if (!module) return "{}";

  String s = "{";
  s += "\"id\":\"" + json_escape(module->id ? module->id : "") + "\"";
  s += ",\"name\":\"" + json_escape(module->name ? module->name : "") + "\"";
  s += ",\"version\":\"" + json_escape(module->version ? module->version : "") + "\"";
  s += ",\"author\":\"" + json_escape(module->author ? module->author : "") + "\"";
  s += ",\"description\":\"" + json_escape(module->description ? module->description : "") + "\"";
  s += ",\"host\":\"" + json_escape(module->host ? module->host : "") + "\"";
  s += ",\"runtime\":\"" + json_escape(module->runtime ? module->runtime : "") + "\"";

  if (module->script_path) {
    s += ",\"script_path\":\"" + json_escape(module->script_path) + "\"";
  }

  s += ",\"builtin\":" + String(module->built_in ? "true" : "false");
  s += ",\"enabled\":" + String(module_registry_is_enabled((uint8_t)index) ? "true" : "false");
  s += ",\"configs\":";

  // 从配置文件读取配置定义（统一使用 load_config_definition）
  if (module->id != nullptr) {
    String config_def = load_config_definition(module->id, module->script_path);
    if (config_def.length() > 0) {
      // 注入当前配置值
      String config_with_values = inject_config_values(config_def, module->id);
      s += config_with_values;
    } else {
      s += "[]";
    }
  } else {
    s += "[]";
  }

  s += "}";
  return s;
}

// Dynamic Lua module execution functions
static int dynamic_lua_load_module(bool call_setup) {
  // Find which dynamic module is being set up
  // We need to track this through the current page_index
  extern int32_t page_index;

  if (page_index < 0 || page_index >= s_total_module_count) {
    Serial.println("Dynamic Lua setup: Invalid page index");
    return -1;
  }

  const module_descriptor_t* module = s_all_modules[page_index];
  if (!module || module->built_in || !module->script_path) {
    Serial.println("Dynamic Lua setup: Not a dynamic module");
    return -1;
  }

  // Find the dynamic module index
  int dynamic_idx = -1;
  for (uint8_t i = 0; i < s_dynamic_module_count; i++) {
    if (&s_dynamic_modules[i] == module) {
      dynamic_idx = i;
      break;
    }
  }

  if (dynamic_idx < 0) {
    Serial.println("Dynamic Lua setup: Module not found in dynamic list");
    return -1;
  }

  s_current_dynamic_module = dynamic_idx;

  Serial.print("Dynamic Lua setup: Loading ");
  Serial.println(module->script_path);

  // Create Lua state
  lua_State* L = luaL_newstate();
  if (!L) {
    Serial.println("Dynamic Lua setup: Failed to create Lua state");
    return -1;
  }

  luaL_openlibs(L);
  register_lua_hardware_apis(L);

  // 注入 CONFIG 全局表（从配置文件读取配置定义，从 NVS 读取配置值）
  inject_lua_config_table(L, module->id, module->script_path);

  String script_path = String(module->script_path);

  // 流式加载 Lua 脚本（luaL_loadfile 内部按块读取并增量编译），
  // 避免把整个脚本拼进 C++ String（容量按 2 的幂翻倍）再加载，
  // 显著降低大脚本（如名言警句 36KB）的峰值内存
  if (luaL_loadfile(L, script_path.c_str()) != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    Serial.print("Dynamic Lua setup: Load error: ");
    Serial.println(error ? error : "unknown");
    lua_pop(L, 1);
    lua_close(L);
    return -1;
  }

  // Execute script（顶层函数：定义函数 + 构建数据表）
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    Serial.print("Dynamic Lua setup: Script error: ");
    Serial.println(error ? error : "unknown");
    lua_pop(L, 1);
    lua_close(L);
    return -1;
  }

  // Call setup function if it exists（仅完整加载时执行，按键唤醒只加载不绘制）
  if (call_setup) {
    lua_getglobal(L, "setup");
    if (lua_isfunction(L, -1)) {
      if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(L, -1);
        Serial.print("Dynamic Lua setup: Error in setup(): ");
        Serial.println(error);
        lua_pop(L, 1);
        lua_close(L);
        return -1;
      }
    } else {
      lua_pop(L, 1);
    }
  }

  // Start hardware resources declared by use()
  lua_hardware_start_resources();

  s_dynamic_lua_states[dynamic_idx] = L;
  s_dynamic_lua_loaded[dynamic_idx] = true;

  Serial.printf("Dynamic Lua %s: Success (free heap %u B)\n",
                call_setup ? "setup" : "load-only", (unsigned)ESP.getFreeHeap());
  return 0;
}

static int dynamic_lua_setup(void) {
  return dynamic_lua_load_module(true);
}

// 按键钩子：调用 Lua 脚本中定义的 subpage_next/subpage_prev（KEY_DOWN/KEY_UP 短按）。
// 按键唤醒时系统跳过了 setup（模块未加载）——这里先只加载脚本、不执行 setup 绘制，
// 由 Lua 钩子函数自行绘制后，主流程统一进入深度休眠
static int dynamic_lua_key_hook(const char* hook_name) {
  extern int32_t page_index;
  if (page_index < 0 || page_index >= s_total_module_count) {
    return -1;
  }
  const module_descriptor_t* module = s_all_modules[page_index];
  if (!module || module->built_in || !module->script_path) {
    return -1;
  }

  int dynamic_idx = -1;
  for (uint8_t i = 0; i < s_dynamic_module_count; i++) {
    if (&s_dynamic_modules[i] == module) {
      dynamic_idx = i;
      break;
    }
  }
  if (dynamic_idx < 0) {
    return -1;
  }

  // 状态未加载（按键唤醒）：仅加载脚本，不执行 setup 绘制
  if (!s_dynamic_lua_loaded[dynamic_idx]) {
    if (dynamic_lua_load_module(false) != 0) {
      return -1;
    }
  }

  lua_State* L = s_dynamic_lua_states[dynamic_idx];
  if (!L) {
    return -1;
  }

  lua_getglobal(L, hook_name);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return 0;  // 模块未定义按键钩子：无操作
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    Serial.print("Dynamic Lua hook error: ");
    Serial.println(error ? error : "unknown");
    lua_pop(L, 1);
    return -1;
  }
  return 0;
}

static int dynamic_lua_subpage_next(void) {
  return dynamic_lua_key_hook("subpage_next");
}

static int dynamic_lua_subpage_prev(void) {
  return dynamic_lua_key_hook("subpage_prev");
}

static int dynamic_lua_loop(void) {
  if (s_current_dynamic_module < 0 || s_current_dynamic_module >= s_dynamic_module_count) {
    return 0;
  }

  lua_State* L = s_dynamic_lua_states[s_current_dynamic_module];
  if (!L || !s_dynamic_lua_loaded[s_current_dynamic_module]) {
    return 0;
  }

  // Call loop function
  lua_getglobal(L, "loop");
  if (lua_isfunction(L, -1)) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(L, -1);
      Serial.print("Dynamic Lua loop: Error: ");
      Serial.println(error);
      lua_pop(L, 1);
      s_dynamic_lua_loaded[s_current_dynamic_module] = false;
      return -1;
    }
  } else {
    lua_pop(L, 1);
  }

  return 0;
}

static int dynamic_lua_unload(void) {
  if (s_current_dynamic_module < 0 || s_current_dynamic_module >= s_dynamic_module_count) {
    return 0;
  }

  lua_State* L = s_dynamic_lua_states[s_current_dynamic_module];
  if (L && s_dynamic_lua_loaded[s_current_dynamic_module]) {
    // Call unload function if it exists
    lua_getglobal(L, "unload");
    if (lua_isfunction(L, -1)) {
      lua_pcall(L, 0, 0, 0);
    } else {
      lua_pop(L, 1);
    }

    // Stop hardware resources
    lua_hardware_stop_resources();

    lua_close(L);
    s_dynamic_lua_states[s_current_dynamic_module] = nullptr;
    s_dynamic_lua_loaded[s_current_dynamic_module] = false;
  }

  s_current_dynamic_module = -1;

  Serial.println("Dynamic Lua unload: Success");
  return 0;
}

// 动态 Lua 模块的定时唤醒间隔：
// 统一键 interval（分钟，1~1440，名言/倒计时通用）→ 返回秒；
// refresh（小时）仅作旧版倒计时配置兼容（interval 未保存时回退）
static int dynamic_lua_wake_interval(void) {
  extern int32_t page_index;
  if (page_index < 0 || page_index >= s_total_module_count) {
    return 0;
  }
  const module_descriptor_t* module = s_all_modules[page_index];
  if (!module || module->built_in || !module->id) {
    return 0;
  }

  int minutes = load_config_ns(String(module->id), "interval");
  if (minutes > 0) {
    if (minutes > 1440) minutes = 1440;
    return minutes * 60;
  }

  int hours = load_config_ns(String(module->id), "refresh");
  if (hours <= 0) {
    return 0;
  }
  return hours * 3600;
}
