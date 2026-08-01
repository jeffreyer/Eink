#include "lua_hardware_api.h"

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "app_control.h"
#include "common.h"
#include "sleep_manager.h"
#include "time_calibration.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace {

// 全局重力数据快照（由调用者更新）
struct GravitySnapshot {
  float x, y, z;
  bool valid;
};

GravitySnapshot g_gravity_snapshot = {0, 0, 0, false};

// 资源使用标志
bool g_use_button = false;

// 按键事件回调
struct ButtonEvent {
  enum Type { NONE = 0, CLICK = 1, LONG_PRESS = 2 };
  Type type;
  uint32_t timestamp;
};

ButtonEvent g_button_event = {ButtonEvent::NONE, 0};
bool g_button_is_holding = false;  // 按键是否正在被按住
bool g_button_is_holding_used = false;  // 当前模块是否使用了 is_holding() 函数

}  // namespace


// 启动已声明的硬件资源
void lua_hardware_start_resources() {

}

// 停止所有硬件资源
void lua_hardware_stop_resources() {
  if (g_use_button) {
    Serial.println("Lua: Releasing button control...");
    g_use_button = false;
    g_button_event.type = ButtonEvent::NONE;
    g_button_is_holding = false;
    g_button_is_holding_used = false;  // 重置 is_holding 使用标志
  }
}
// ============================================================================
// Button API
// ============================================================================

// button.poll() -> returns event_type (0=none, 1=click, 2=long_press)
static int lua_button_poll(lua_State* L) {
  int event_type = (int)g_button_event.type;
  g_button_event.type = ButtonEvent::NONE;  // 清除事件
  lua_pushnumber(L, event_type);
  return 1;
}

// button.is_holding() -> returns true if button is currently being held
static int lua_button_is_holding(lua_State* L) {
  g_button_is_holding_used = true;  // 标记当前模块使用了 is_holding()
  lua_pushboolean(L, g_button_is_holding);
  return 1;
}

static const luaL_Reg button_lib[] = {
  {"poll", lua_button_poll},
  {"get_event", lua_button_poll},
  {"is_holding", lua_button_is_holding},
  {NULL, NULL}
};

// ============================================================================
// Config API
// ============================================================================

// config.get(key) -> returns int value
static int lua_config_get(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  int value = load_config(key);
  lua_pushnumber(L, value);
  return 1;
}

static const luaL_Reg config_lib[] = {
  {"get", lua_config_get},
  {NULL, NULL}
};

// ============================================================================
// Time API
// ============================================================================

// time.millis() -> returns milliseconds since boot
static int lua_time_millis(lua_State* L) {
  lua_pushnumber(L, millis());
  return 1;
}

// time.delay(ms) -> blocking delay
static int lua_time_delay(lua_State* L) {
  unsigned long ms = luaL_checknumber(L, 1);
  delay(ms);
  return 0;
}

// time.now() -> returns table with current time {year, month, day, hour, min, sec, wday}
static int lua_time_now(lua_State* L) {
  // 使用校准后的时间而不是原始系统时间
  time_t now = TimeCalibration::get_calibrated_time();
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  lua_newtable(L);

  lua_pushinteger(L, timeinfo.tm_year + 1900);
  lua_setfield(L, -2, "year");

  lua_pushinteger(L, timeinfo.tm_mon + 1);
  lua_setfield(L, -2, "month");

  lua_pushinteger(L, timeinfo.tm_mday);
  lua_setfield(L, -2, "day");

  lua_pushinteger(L, timeinfo.tm_hour);
  lua_setfield(L, -2, "hour");

  lua_pushinteger(L, timeinfo.tm_min);
  lua_setfield(L, -2, "min");

  lua_pushinteger(L, timeinfo.tm_sec);
  lua_setfield(L, -2, "sec");

  lua_pushinteger(L, timeinfo.tm_wday);
  lua_setfield(L, -2, "wday");

  return 1;
}

static const luaL_Reg time_lib[] = {
  {"millis", lua_time_millis},
  {"delay", lua_time_delay},
  {"now", lua_time_now},
  {NULL, NULL}
};

static int lua_sys_page_index(lua_State* L) {
  lua_pushnumber(L, subpage_index);
  return 1;
}

static const luaL_Reg sys_lib[] = {
  {"page_index", lua_sys_page_index},
  {NULL, NULL}
};


// ============================================================================
// Math Extensions
// ============================================================================

// math.clamp(value, min, max)
static int lua_math_clamp(lua_State* L) {
  lua_Number value = luaL_checknumber(L, 1);
  lua_Number min_val = luaL_checknumber(L, 2);
  lua_Number max_val = luaL_checknumber(L, 3);
  lua_pushnumber(L, constrain(value, min_val, max_val));
  return 1;
}

// ============================================================================
// Resource Declaration API
// ============================================================================

// use(resource_name) -> declare resource usage
static int lua_use(lua_State* L) {
  const char* resource = luaL_checkstring(L, 1);
  if (strcmp(resource, "button") == 0) {
    g_use_button = true;
    Serial.println("Lua: Declared use of button control");
  } else {
    Serial.print("Lua: Unknown resource: ");
    Serial.println(resource);
  }

  return 0;
}

// ============================================================================
// Custom print function that redirects to log_e
// ============================================================================

static int lua_print(lua_State* L) {
  int n = lua_gettop(L);  // Number of arguments
  String output = "";

  for (int i = 1; i <= n; i++) {
    if (i > 1) output += "\t";  // Tab separator between arguments

    if (lua_isstring(L, i)) {
      output += lua_tostring(L, i);
    } else if (lua_isboolean(L, i)) {
      output += lua_toboolean(L, i) ? "true" : "false";
    } else if (lua_isnumber(L, i)) {
      output += String(lua_tonumber(L, i));
    } else if (lua_isnil(L, i)) {
      output += "nil";
    } else {
      output += lua_typename(L, lua_type(L, i));
    }
  }

  log_e("%s", output.c_str());
  return 0;
}

// ============================================================================
// 注册所有 API
// ============================================================================

void register_lua_hardware_apis(lua_State* L) {
  // Override print function to use log_e
  lua_pushcfunction(L, lua_print);
  lua_setglobal(L, "print");

  // Register button library
  luaL_newlib(L, button_lib);
  lua_setglobal(L, "button");

  // Register config library
  luaL_newlib(L, config_lib);
  lua_setglobal(L, "config");

  // Register time library
  luaL_newlib(L, time_lib);
  lua_setglobal(L, "time");

  // Add clamp to math library
  lua_getglobal(L, "math");
  lua_pushcfunction(L, lua_math_clamp);
  lua_setfield(L, -2, "clamp");
  lua_pop(L, 1);

  luaL_newlib(L, sys_lib);
  lua_setglobal(L, "sys");

  // Register use() function
  lua_pushcfunction(L, lua_use);
  lua_setglobal(L, "use");

}

// 检查当前模块是否声明了 button 权限
bool lua_hardware_is_button_used() {
  return g_use_button;
}

// 检查当前模块是否使用了 is_holding() 函数
bool lua_hardware_is_holding_used() {
  return g_button_is_holding_used;
}

// 发送按键事件给 Lua 模块
void lua_hardware_send_button_event(int event_type) {
  g_button_event.type = (ButtonEvent::Type)event_type;
  g_button_event.timestamp = millis();
}

// 设置按键按住状态
void lua_hardware_set_button_holding(bool holding) {
  g_button_is_holding = holding;
}


// 注入 CONFIG 全局表（从 NVS 读取配置）
void inject_lua_config_table(lua_State* L, const char* module_id, const char* script_path) {
  if (!L || !module_id) return;

  // 创建 CONFIG 表
  lua_newtable(L);

  // 使用 module_id 作为命名空间
  String ns = String(module_id);

  // 从配置文件读取配置定义（根据 script_path 决定文件系统）
  String config_def_json = load_config_definition(module_id, script_path);

  if (config_def_json.length() == 0) {
    // 没有配置定义，设置空的 CONFIG 表
    lua_setglobal(L, "CONFIG");
    return;
  }

  // 解析配置定义 JSON（简单的手动解析）
  // 格式: [{"key":"move_interval","type":"slider",...},...]
  int pos = 0;
  while (pos < config_def_json.length()) {
    // 查找下一个配置项
    int obj_start = config_def_json.indexOf("{", pos);
    if (obj_start < 0) break;

    int obj_end = config_def_json.indexOf("}", obj_start);
    if (obj_end < 0) break;

    String config_item = config_def_json.substring(obj_start, obj_end + 1);

    // 提取 key
    int key_pos = config_item.indexOf("\"key\"");
    if (key_pos >= 0) {
      int key_start = config_item.indexOf("\"", key_pos + 6);
      if (key_start < 0) {
        pos = obj_end + 1;
        continue;
      }
      key_start += 1;

      int key_end = config_item.indexOf("\"", key_start);
      if (key_end < 0 || key_end <= key_start) {
        pos = obj_end + 1;
        continue;
      }

      String key = config_item.substring(key_start, key_end);
      if (key.length() == 0) {
        pos = obj_end + 1;
        continue;
      }

      // 提取 type
      int type_pos = config_item.indexOf("\"type\"");
      String type = "";
      if (type_pos >= 0) {
        int type_start = config_item.indexOf("\"", type_pos + 7);
        if (type_start >= 0) {
          type_start += 1;
          int type_end = config_item.indexOf("\"", type_start);
          if (type_end > type_start) {
            type = config_item.substring(type_start, type_end);
          }
        }
      }
      if (type.length() == 0) {
        if (config_item.indexOf("\"options\"") >= 0) {
          type = "select";
        }
      }

      // 根据类型从 NVS 读取配置值（使用命名空间）
      // 使用 isKey() 检查配置是否存在，避免将未保存的配置设置为默认值
      Preferences prefs;
      prefs.begin(ns.c_str(), true);
      bool key_exists = prefs.isKey(key.c_str());

      if (key_exists) {
        if (type == "slider" || type == "number") {
          // 尝试读取浮点数
          float float_value = prefs.getFloat(key.c_str(), 0.0f);
          if (float_value != 0.0f) {
            lua_pushstring(L, key.c_str());
            lua_pushnumber(L, float_value);
            lua_settable(L, -3);
          } else {
            // 尝试读取整数
            int int_value = prefs.getInt(key.c_str(), 0);
            if (int_value != 0) {
              lua_pushstring(L, key.c_str());
              lua_pushnumber(L, int_value);
              lua_settable(L, -3);
            }
          }
        } else if (type == "text" || type == "color" || type == "select") {
          // 字符串类型
          String string_value = prefs.getString(key.c_str(), "");
          if (string_value.length() > 0) {
            lua_pushstring(L, key.c_str());
            lua_pushstring(L, string_value.c_str());
            lua_settable(L, -3);
          }
        } else if (type == "switch") {
          // 布尔类型 - key 存在时读取实际值
          int bool_value = prefs.getInt(key.c_str(), 0);
          lua_pushstring(L, key.c_str());
          lua_pushboolean(L, bool_value != 0);
          lua_settable(L, -3);
        }
      }

      prefs.end();
    }

    pos = obj_end + 1;
  }

  // 设置为全局变量 CONFIG
  lua_setglobal(L, "CONFIG");
}
