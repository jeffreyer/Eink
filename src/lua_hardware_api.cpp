#include "lua_hardware_api.h"

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "app_control.h"
#include "common.h"
#include "sleep_manager.h"
#include "time_calibration.h"
#include "GUI_Paint.h"
#ifdef INK6
#include "eink6.h"
#else
#include "eink.h"
#include "Display_EPD_W21.h"
#endif

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

// time.get() -> returns hour, minute, second
static int lua_time_get(lua_State* L) {
  time_t now = TimeCalibration::get_calibrated_time();
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  lua_pushinteger(L, timeinfo.tm_hour);
  lua_pushinteger(L, timeinfo.tm_min);
  lua_pushinteger(L, timeinfo.tm_sec);
  return 3;
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
  {"get", lua_time_get},
  {"now", lua_time_now},
  {NULL, NULL}
};

// ============================================================================
// Display API (e-ink)
// ============================================================================

// 前向声明（GB2312 中文字库渲染，定义于本文件后方）
static bool gb_map_load();
static void draw_utf8_text(int x, int y, const char* str, int cn_cell, sFONT* ascii_font, UWORD color);

// 画布缓冲（4色屏定义于 eink.cpp，6色屏定义于 eink6.cpp）
extern unsigned char BlackImage[ALLSCREEN_BYTES];

// Lua 颜色值(0=黑,1=白,2=黄,3=红,4=蓝,5=绿) -> 画布颜色值
static UWORD lua_color_to_paint(int color) {
#ifdef INK6
  switch (color) {
    case 1:  return NIBBLE_WHITE;   // 白
    case 2:  return NIBBLE_YELLOW;  // 黄
    case 3:  return NIBBLE_RED;     // 红
    case 4:  return NIBBLE_BLUE;    // 蓝
    case 5:  return NIBBLE_GREEN;   // 绿
    case 0:
    default: return NIBBLE_BLACK;   // 黑
  }
#else
  switch (color) {
    case 1:  return WHITE0;   // 白
    case 2:  return YELLOW0;  // 黄
    case 3:  return RED0;     // 红
    case 0:
    default: return BLACK0;   // 黑
  }
#endif
}

// 画布“白底”颜色（4色屏为 2bit 白，6色屏为 4bit 白半字节）
#ifdef INK6
#define PAINT_BG_WHITE NIBBLE_WHITE
#else
#define PAINT_BG_WHITE WHITE0
#endif

static void display_prepare_canvas() {
#ifdef INK6
  // 6 色屏：240x240，4bpp（2 像素/字节），直接对应 JD7601 帧格式
  Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, PAINT_BG_WHITE);
  Paint_SetScale(7);
#else
  Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, WHITE0);
  Paint_SetScale(4);
#endif
  // 全局显示方向（模块页“显示方向”）：对 Lua 绘制模块统一生效
  Paint_SetRotate((UWORD)load_config_ns("gallery", "rotation"));
  Paint_SelectImage(BlackImage);
}

// display.clear()
static int lua_display_clear(lua_State* L) {
  display_prepare_canvas();
  Paint_Clear(PAINT_BG_WHITE);
  return 0;
}

// display.pixel(x, y, color)
static int lua_display_pixel(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int c = (int)luaL_optnumber(L, 3, 0);
  display_prepare_canvas();
  Paint_SetPixel(x, y, lua_color_to_paint(c));
  return 0;
}

// display.line(x0, y0, x1, y1, color)
static int lua_display_line(lua_State* L) {
  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int c = (int)luaL_optnumber(L, 5, 0);
  display_prepare_canvas();
  Paint_DrawLine(x0, y0, x1, y1, lua_color_to_paint(c), LINE_STYLE_SOLID, DOT_PIXEL_1X1);
  return 0;
}

// display.rect(x, y, w, h, color)
static int lua_display_rect(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int c = (int)luaL_optnumber(L, 5, 0);
  display_prepare_canvas();
  Paint_DrawRectangle(x, y, x + w, y + h, lua_color_to_paint(c), DRAW_FILL_EMPTY, DOT_PIXEL_1X1);
  return 0;
}

// display.fill_rect(x, y, w, h, color)
static int lua_display_fill_rect(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int c = (int)luaL_optnumber(L, 5, 0);
  display_prepare_canvas();
  Paint_DrawRectangle(x, y, x + w, y + h, lua_color_to_paint(c), DRAW_FILL_FULL, DOT_PIXEL_1X1);
  return 0;
}

// display.circle(x, y, r, color)
static int lua_display_circle(lua_State* L) {
  int cx = (int)luaL_checknumber(L, 1);
  int cy = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  int c = (int)luaL_optnumber(L, 4, 0);
  display_prepare_canvas();
  Paint_DrawCircle(cx, cy, r, lua_color_to_paint(c), DRAW_FILL_EMPTY, DOT_PIXEL_1X1);
  return 0;
}

// display.fill_circle(x, y, r, color)
static int lua_display_fill_circle(lua_State* L) {
  int cx = (int)luaL_checknumber(L, 1);
  int cy = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  int c = (int)luaL_optnumber(L, 4, 0);
  display_prepare_canvas();
  Paint_DrawCircle(cx, cy, r, lua_color_to_paint(c), DRAW_FILL_FULL, DOT_PIXEL_1X1);
  return 0;
}

// display.text(x, y, str, size, color)
// size: 1=Font8(5x8), 2=Font12(7x12), 3=Font16(11x16), 4=Font24(17x24)
static int lua_display_text(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  const char* str = luaL_checkstring(L, 3);
  int size = (int)luaL_optnumber(L, 4, 2);
  int c = (int)luaL_optnumber(L, 5, 0);

  display_prepare_canvas();

  sFONT* font = &Font12;
  int cn_cell = 16;
  switch (size) {
    case 1: font = &Font8;  cn_cell = 16; break;
    case 2: font = &Font12; cn_cell = 16; break;
    case 3: font = &Font16; cn_cell = 16; break;
    case 4: font = &Font24; cn_cell = 24; break;
    default: font = &Font12; cn_cell = 16; break;
  }

  // 检测是否包含多字节（中文）字符
  bool has_utf8 = false;
  for (const char* p = str; *p; p++) {
    if ((uint8_t)*p >= 0x80) {
      has_utf8 = true;
      break;
    }
  }

  if (has_utf8) {
    gb_map_load();
    draw_utf8_text(x, y, str, cn_cell, font, lua_color_to_paint(c));
  } else {
    Paint_DrawString_EN(x, y, str, font, lua_color_to_paint(c), PAINT_BG_WHITE);
  }
  return 0;
}

// display.show() -> 刷新到墨水屏（约12秒）
static int lua_display_show(lua_State* L) {
  display_prepare_canvas();
#ifdef INK6
  epdDisplayImage(BlackImage, ALLSCREEN_BYTES);
#else
  EPD_init_Fast2();
  PIC_display(BlackImage);
  EPD_sleep();
#endif
  return 0;
}

// ============================================================================
// GB2312 中文字库（SPIFFS: /spiffs/fonts/）
// ============================================================================

static const char* GB_FONT_16_PATH = "/spiffs/fonts/gb2312_16.bin";
static const char* GB_FONT_24_PATH = "/spiffs/fonts/gb2312_24.bin";
static const char* GB_MAP_PATH = "/spiffs/fonts/gb2312_map.bin";

// 映射表缓存：[unicode, gbcode] 交替，按 unicode 升序（小端）
static uint16_t* s_gb_map = nullptr;
static uint32_t s_gb_map_count = 0;
static bool s_gb_map_loaded = false;

static bool gb_map_load() {
  if (s_gb_map_loaded) {
    return s_gb_map != nullptr;
  }
  s_gb_map_loaded = true;

  FILE* fp = fopen(GB_MAP_PATH, "rb");
  if (!fp) {
    Serial.println("Lua: GB2312 map not found");
    return false;
  }

  uint32_t count = 0;
  if (fread(&count, 4, 1, fp) != 1 || count == 0 || count > 20000) {
    fclose(fp);
    return false;
  }

  uint16_t* buf = (uint16_t*)malloc((size_t)count * 4);
  if (!buf) {
    fclose(fp);
    return false;
  }
  if (fread(buf, 4, count, fp) != count) {
    free(buf);
    fclose(fp);
    return false;
  }
  fclose(fp);

  s_gb_map = buf;
  s_gb_map_count = count;
  Serial.printf("Lua: GB2312 map loaded (%u entries)\n", count);
  return true;
}

// 二分查找 unicode -> gbcode（0 表示未收录）
static uint16_t gb_map_lookup(uint32_t unicode) {
  if (!s_gb_map) {
    return 0;
  }
  int32_t lo = 0, hi = (int32_t)s_gb_map_count - 1;
  while (lo <= hi) {
    int32_t mid = (lo + hi) / 2;
    uint16_t u = s_gb_map[mid * 2];
    if (u == unicode) {
      return s_gb_map[mid * 2 + 1];
    }
    if (u < unicode) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return 0;
}

// UTF-8 解码，返回 Unicode 码点（BMP）
static uint32_t utf8_decode(const char* s, int* len) {
  uint8_t b0 = (uint8_t)s[0];
  if (b0 < 0x80) {
    *len = 1;
    return b0;
  }
  if ((b0 & 0xE0) == 0xC0 && s[1] != '\0') {
    *len = 2;
    return ((b0 & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
  }
  if ((b0 & 0xF0) == 0xE0 && s[1] != '\0' && s[2] != '\0') {
    *len = 3;
    return ((b0 & 0x0F) << 12) | (((uint8_t)s[1] & 0x3F) << 6) | ((uint8_t)s[2] & 0x3F);
  }
  *len = 1;
  return b0;
}

// 从字库文件读取并绘制一个 GB2312 字形到 (x, y)
static void gb_glyph_draw(FILE* fp, uint16_t gbcode, int x, int y, int cell, UWORD color) {
  int hi = gbcode >> 8;
  int lo = gbcode & 0xFF;
  int bytes_per_row = cell / 8;
  long offset = ((long)(hi - 0xA1) * 94 + (lo - 0xA1)) * bytes_per_row * cell;

  if (fseek(fp, offset, SEEK_SET) != 0) {
    return;
  }

  uint8_t row[3];
  for (int r = 0; r < cell; r++) {
    if (fread(row, 1, bytes_per_row, fp) != (size_t)bytes_per_row) {
      return;
    }
    for (int c = 0; c < cell; c++) {
      if (row[c / 8] & (0x80 >> (c % 8))) {
        Paint_SetPixel(x + c, y + r, color);
      }
    }
  }
}

// 绘制 UTF-8 文本（ASCII + 中文混排）
static void draw_utf8_text(int x, int y, const char* str, int cn_cell, sFONT* ascii_font, UWORD color) {
  FILE* fp = nullptr;
  const char* p = str;
  int cx = x;

  while (*p) {
    int len;
    uint32_t uni = utf8_decode(p, &len);

    if (uni < 0x80) {
      Paint_DrawChar(cx, y, (char)uni, ascii_font, color, PAINT_BG_WHITE);
      cx += ascii_font->Width;
    } else {
      if (!fp) {
        fp = fopen(cn_cell == 24 ? GB_FONT_24_PATH : GB_FONT_16_PATH, "rb");
        if (!fp) {
          Paint_DrawChar(cx, y, '?', ascii_font, color, PAINT_BG_WHITE);
          cx += ascii_font->Width;
          p += len;
          continue;
        }
      }

      uint16_t gb = gb_map_lookup(uni);
      if (gb == 0) {
        // 字库未收录，回退画 '?'
        Paint_DrawChar(cx, y, '?', ascii_font, color, PAINT_BG_WHITE);
      } else {
        gb_glyph_draw(fp, gb, cx, y, cn_cell, color);
      }
      cx += cn_cell;
    }

    p += len;
  }

  if (fp) {
    fclose(fp);
  }
}

static const luaL_Reg display_lib[] = {
  {"clear", lua_display_clear},
  {"pixel", lua_display_pixel},
  {"line", lua_display_line},
  {"rect", lua_display_rect},
  {"fill_rect", lua_display_fill_rect},
  {"circle", lua_display_circle},
  {"fill_circle", lua_display_fill_circle},
  {"text", lua_display_text},
  {"show", lua_display_show},
  {NULL, NULL}
};

static int lua_sys_page_index(lua_State* L) {
  lua_pushnumber(L, subpage_index);
  return 1;
}

// 本次启动的"唤醒意图"是否已被初始显示消费
static bool s_boot_wake_consumed = false;

// 唤醒源：0=上电/未知, 1=GPIO按键唤醒, 2=定时器唤醒
// 启动后首次调用返回真实唤醒源；被标记消费后（初始显示完成）返回 0，
// 这样 BLE"刷新显示"等显式重载时 setup 会正常重绘
static int lua_sys_wake_source(lua_State* L) {
  int source = 0;
  if (!s_boot_wake_consumed) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
      source = 1;
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
      source = 2;
    }
  }
  lua_pushnumber(L, source);
  return 1;
}

// 标记本次启动的初始显示已完成（主程序在首个模块 setup 后调用），
// 之后 sys.wake_source() 返回 0，显式刷新/切换模块会正常重绘
void lua_hardware_mark_boot_wake_consumed() {
  s_boot_wake_consumed = true;
}

static const luaL_Reg sys_lib[] = {
  {"page_index", lua_sys_page_index},
  {"wake_source", lua_sys_wake_source},
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

  // Register display library
  luaL_newlib(L, display_lib);
  lua_setglobal(L, "display");

  // Register display size constants
  lua_pushnumber(L, EPD_WIDTH);
  lua_setglobal(L, "WIDTH");

  lua_pushnumber(L, EPD_HEIGHT);
  lua_setglobal(L, "HEIGHT");

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
              lua_pushinteger(L, int_value);
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
          } else if (type == "select") {
            // select 也可能保存为整数（如月份/日期选项），回退读取整数
            int int_value = prefs.getInt(key.c_str(), 0);
            if (int_value != 0) {
              lua_pushstring(L, key.c_str());
              lua_pushinteger(L, int_value);
              lua_settable(L, -3);
            }
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
