#include <Arduino.h>
#include <Preferences.h>
#include <FastLED.h>
#include <SPIFFS.h>
#include "common.h"

int32_t page_index=0,subpage_index=0;
bool is_chk_bat=false;

int load_config(String key){
  Preferences prefs;
  prefs.begin("bottle", true);
  int v=prefs.getInt(key.c_str());
  prefs.end();
  return v;
}

void save_config(String key,int value){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putInt(key.c_str(),value);
  prefs.end();
}

String load_config_string(String key){
  Preferences prefs;
  prefs.begin("bottle", true);
  String v = prefs.getString(key.c_str(), "");
  prefs.end();
  return v;
}

void save_config_string(String key, String value){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putString(key.c_str(), value.c_str());
  prefs.end();
}

float load_config_float(String key){
  Preferences prefs;
  prefs.begin("bottle", true);
  float v = prefs.getFloat(key.c_str(), 0.0f);
  prefs.end();
  return v;
}

void save_config_float(String key, float value){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putFloat(key.c_str(), value);
  prefs.end();
}

// 从文件读取配置定义，根据 script_path 决定文件系统
String load_config_definition(const char* module_id, const char* script_path) {
  if (!module_id || strlen(module_id) == 0) {
    return "";
  }

  // 根据 script_path 决定从哪个文件系统读取
  String cfg_path;

  if (script_path && strstr(script_path, "/extflash/") != nullptr) {
    // 脚本在 /extflash，配置文件也在 /extflash
    cfg_path = String("/extflash/") + module_id + ".cfg";
  } else {
    // 脚本在 /spiffs 或内置模块，配置文件在 /spiffs
    cfg_path = String("/spiffs/") + module_id + ".cfg";

    // 确保 SPIFFS 已挂载
    if (!SPIFFS.begin(true)) {
      Serial.println("load_config_definition: SPIFFS mount failed");
      return "";
    }
  }

  FILE* fp = fopen(cfg_path.c_str(), "r");
  if (!fp) {
    // 配置文件不存在，返回空字符串（模块可能没有配置）
    return "";
  }

  // 读取文件内容
  String config_def = "";
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
    config_def += buffer;
  }

  fclose(fp);
  return config_def;
}

// 带命名空间的配置函数
void save_config_ns(String ns, String key, int value) {
  Preferences prefs;
  prefs.begin(ns.c_str(), false);
  prefs.putInt(key.c_str(), value);
  prefs.end();
}

int load_config_ns(String ns, String key) {
  Preferences prefs;
  prefs.begin(ns.c_str(), true);
  int v = prefs.getInt(key.c_str(), 0);
  prefs.end();
  return v;
}

void save_config_string_ns(String ns, String key, String value) {
  Preferences prefs;
  prefs.begin(ns.c_str(), false);
  prefs.putString(key.c_str(), value.c_str());
  prefs.end();
}

String load_config_string_ns(String ns, String key) {
  Preferences prefs;
  prefs.begin(ns.c_str(), true);
  String v = prefs.getString(key.c_str(), "");
  prefs.end();
  return v;
}

void save_config_float_ns(String ns, String key, float value) {
  Preferences prefs;
  prefs.begin(ns.c_str(), false);
  prefs.putFloat(key.c_str(), value);
  prefs.end();
}

float load_config_float_ns(String ns, String key) {
  Preferences prefs;
  prefs.begin(ns.c_str(), true);
  float v = prefs.getFloat(key.c_str(), 0.0f);
  prefs.end();
  return v;
}