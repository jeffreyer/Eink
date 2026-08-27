#pragma once

#include <lua.hpp>

// 注册所有硬件相关的 Lua API
// 包括: led, spectrum, gravity, config, time, button
void register_lua_hardware_apis(lua_State* L);

// 注入 CONFIG 全局表（从配置文件读取配置定义，从 NVS 读取配置值）
void inject_lua_config_table(lua_State* L, const char* module_id, const char* script_path);

// 启动已声明的硬件资源
void lua_hardware_start_resources();

// 停止所有硬件资源
void lua_hardware_stop_resources();

// 检查当前模块是否声明了 button 权限
bool lua_hardware_is_button_used();

// 检查当前模块是否使用了 is_holding() 函数
bool lua_hardware_is_holding_used();

// 发送按键事件给 Lua 模块 (1=click, 2=long_press)
void lua_hardware_send_button_event(int event_type);

// 设置按键按住状态
void lua_hardware_set_button_holding(bool holding);

// C++ 侧画布工具：清空画布（白色）
void lua_hardware_clear_canvas(void);

// C++ 侧画布工具：绘制 UTF-8 文本（中文走 GB2312 字库，size/color 同 display.text）
void lua_hardware_draw_utf8(int x, int y, const char* str, int size, int color);

// 预加载 GB2312 字库映射表（开机内存充足时调用，避免 BLE 中首次绘制失败）
bool lua_hardware_preload_gb2312(void);

int draw_led_text(const char* text,int x,int y,int r,int g,int b);
int draw_led_text_rotated(const char* text, int x, int y, int r, int g, int b);
