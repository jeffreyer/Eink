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

int draw_led_text(const char* text,int x,int y,int r,int g,int b);
int draw_led_text_rotated(const char* text, int x, int y, int r, int g, int b);