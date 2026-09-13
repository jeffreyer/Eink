#pragma once

#include <Arduino.h>
#include <FastLED.h>

// 固件版本号
#define FIRMWARE_VERSION "1.0.0"

#define KEY_UP 1
#define KEY_DOWN 2
#define BLE_LIGHT 20

// ===== 墨水屏型号选择（三选一）=====
// INK6   : 1.54" 六色屏 240x240（JD7601）
// INK_BW : 1.54" 黑白屏 200x200（SSD1681，1bpp）
// 都不定义: 1.54" 四色屏 200x200（2bpp）
// 当前使用四色屏（INK6 / INK_BW 均不定义）；6 色屏打开 INK6，黑白屏打开 INK_BW
#define INK6
// #define INK_BW

// 墨水屏颜色数（供小程序端区分黑白 / 4 色 / 6 色屏）
#if defined(INK6)
#define INK_COLORS 6
#elif defined(INK_BW)
#define INK_COLORS 2
#else
#define INK_COLORS 4
#endif

//蓝牙名称和设备型号
#define BLE_DEVICE_NAME "MiniEink"
#define DEVICE_MODEL "Eink-V1"  // 设备型号，用于区分应用市场可用的应用


//电池检测已在battery.h文件处理

extern int32_t page_index,subpage_index;

int load_config(String key);
void save_config(String key,int value);

// 字符串配置支持
String load_config_string(String key);
void save_config_string(String key, String value);

// 配置定义加载（从文件读取，根据 script_path 决定文件系统）
String load_config_definition(const char* module_id, const char* script_path);

// 浮点数配置支持
float load_config_float(String key);
void save_config_float(String key, float value);

// 带命名空间的配置函数
void save_config_ns(String ns, String key, int value);
int load_config_ns(String ns, String key);
void save_config_string_ns(String ns, String key, String value);
String load_config_string_ns(String ns, String key);
void save_config_float_ns(String ns, String key, float value);
float load_config_float_ns(String ns, String key);

void main_load_config();
void main_save_config();

// 时区恢复函数（从 RTC 内存恢复时区设置）
void restore_timezone();
