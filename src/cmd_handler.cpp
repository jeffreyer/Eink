#include "cmd_handler.h"
#include "common.h"
#include "battery.h"
#include "module_registry.h"
#include "ble_config.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef INK6
#include "eink6.h"
#else
#include "eink.h"
#include "Display_EPD_W21.h"
#endif

void check_cmd(){
  if (Serial.available() > 0) {
    String command = Serial.readString();
    command.trim();

    // 调试：确认收到命令
    Serial.printf("[CMD] Received: %s", command.c_str());

    if (command.startsWith("sleep=")) {
      int sec=command.substring(6).toInt();
      save_config("sleep_sec",sec);
      extern uint32_t s_idle_timeout_ms;
      s_idle_timeout_ms=sec*1000;
      Serial.printf("Set sleep delay seconds: %d", sec);
    }
    else if (command.startsWith("bat?")) {
      int voltage = check_bat();
      Serial.printf("Battery Voltage: %d mV", voltage);
    }
    else if (command.startsWith("gettime?")) {
      time_t now = time(NULL);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d (timestamp: %ld)",
        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, now);
    }
    else if (command.startsWith("lsmod")) {
      Serial.printf("Registered modules:");
      uint8_t count = module_registry_count();
      for (uint8_t i = 0; i < count; i++) {
        const module_descriptor_t* module = module_registry_get(i);
        if (module && module->id) {
          bool enabled = module_registry_is_enabled(i);
          const char* type = module->script_path ? "dynamic" : "builtin";
          const char* path = module->script_path ? module->script_path : "N/A";
          Serial.printf("  [%d] %s %s (configs: %d, enabled: %s, path: %s)",
                i, module->id, type, module->config_count,
                enabled ? "yes" : "no", path);
        }
      }
      Serial.printf("Total: %d modules", count);
    }
    else if (command.startsWith("lsi")) {
      Serial.printf("Files in /spiffs:");
      DIR* dir = opendir("/spiffs");
      if (!dir) {
        Serial.printf("ERROR: Cannot open /spiffs (errno: %d)", errno);
      } else {
        struct dirent* entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {
          char fullpath[256];
          snprintf(fullpath, sizeof(fullpath), "/spiffs/%s", entry->d_name);
          struct stat st;
          if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
              Serial.printf("  [DIR]  %s", entry->d_name);
            } else {
              Serial.printf("  [FILE] %s (%u bytes)", entry->d_name, (unsigned int)st.st_size);
            }
            count++;
          }
        }
        closedir(dir);
        if (count == 0) {
          Serial.printf("  (empty)");
        }
        Serial.printf("Total: %d items", count);
      }
    }
    else if (command.startsWith("read ")) {
      String filename = command.substring(5);
      filename.trim();

      if (filename.length() == 0) {
        Serial.printf("ERROR: No filename specified. Usage: read <filename>");
      } else {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename.c_str());

        FILE* fp = fopen(filepath, "r");
        if (!fp) {
          Serial.printf("ERROR: Cannot open file %s (errno: %d)", filepath, errno);
        } else {
          Serial.printf("Reading file: %s", filepath);
          Serial.printf("--- BEGIN FILE CONTENT ---");

          char buffer[256];
          while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            Serial.printf("%s", buffer);
          }

          Serial.printf("--- END FILE CONTENT ---");
          fclose(fp);
        }
      }
    }
    else if (command.startsWith("rm ")) {
      // 删除 /spiffs 下的文件
      String filename = command.substring(3);
      filename.trim();

      if (filename.length() == 0) {
        Serial.printf("ERROR: No filename specified. Usage: rm <filename>");
      } else {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename.c_str());

        // 检查文件是否存在
        struct stat st;
        if (stat(filepath, &st) != 0) {
          Serial.printf("ERROR: File not found: %s", filepath);
        } else if (S_ISDIR(st.st_mode)) {
          Serial.printf("ERROR: Cannot remove directory: %s", filepath);
          Serial.printf("Use a different method to remove directories");
        } else {
          // 删除文件
          if (remove(filepath) == 0) {
            Serial.printf("File deleted successfully: %s", filepath);
          } else {
            Serial.printf("ERROR: Failed to delete file: %s (errno: %d)", filepath, errno);
          }
        }
      }
    }
    else if (command.startsWith("dis ")) {
      // 删除 /spiffs 下的文件
      String str = command.substring(4);

      if (str.length() == 0) {
        Serial.printf("ERROR: No str specified");
      } else {
        Serial.printf("Display string: %s", str.c_str());
        gui_drawtext(str.c_str());
      }
    }
    else if (command.startsWith("ls")) {
      Serial.printf("Files in /extflash:");
      Serial.printf("Attempting to open directory...");
      DIR* dir = opendir("/extflash");
      if (!dir) {
        Serial.printf("ERROR: Cannot open /extflash (errno: %d)", errno);
        Serial.printf("Storage may still be locked by MSC");
      } else {
        struct dirent* entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {
          char fullpath[256];
          snprintf(fullpath, sizeof(fullpath), "/extflash/%s", entry->d_name);
          struct stat st;
          if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
              Serial.printf("  [DIR]  %s", entry->d_name);
            } else {
              Serial.printf("  [FILE] %s (%u bytes)", entry->d_name, (unsigned int)st.st_size);
            }
            count++;
          }
        }
        closedir(dir);
        if (count == 0) {
          Serial.printf("  (empty)");
        }
        Serial.printf("Total: %d items", count);
      }
    }
    else if (command.startsWith("dfu")) {
      // 进入 ROM 下载模式（等同于 GPIO0 拉低 + 复位）
      Serial.printf("[DFU] Entering ROM download mode...");

      // 设置 RTC 寄存器强制进入下载模式
      REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);

      // 重启
      esp_restart();
    }
    else if (command.startsWith("unbind")) {
      // 清除蓝牙绑定状态
      ble_config_unbind();
      Serial.printf("[BLE] Device unbound, password regenerated");
    }
    else if (command.startsWith("white")) {
#ifdef INK6
      epdDisplaySolid(COLOR_WHITE);
#else
      EPD_init_Fast2();
      Display_All_White();
      EPD_sleep();
#endif
    }
    else if (command.startsWith("nvs")) {
      // 读取 NVS 中所有 key 和值
      Serial.printf("Reading NVS storage:");

      Preferences prefs;

      // 读取 bottle 命名空间
      if (prefs.begin("bottle", true)) {
        Serial.printf("\n[Namespace: bottle]");
        Serial.printf("  page_index = %d", prefs.getInt("page_index", -1));
        Serial.printf("  sleep_sec = %d", prefs.getInt("sleep_sec", -1));
        Serial.printf("  i2s_mic = %d", prefs.getInt("i2s_mic", -1));
        Serial.printf("  led = %d", prefs.getInt("led", -1));
        prefs.end();
      } else {
        Serial.printf("  ERROR: Cannot open namespace 'bottle'");
      }

      // 读取 ble_config 命名空间
      if (prefs.begin("ble_sec", true)) {
        Serial.printf("\n[Namespace: ble_sec]");
        Serial.printf("  bound = %d", prefs.getBool("bound", false));
        prefs.end();
      } else {
        Serial.printf("  ERROR: Cannot open namespace 'ble_sec'");
      }

      // 读取模块配置（遍历可能的模块 ID）
      uint8_t module_count = module_registry_count();
      for (uint8_t i = 0; i < module_count; i++) {
        const module_descriptor_t* module = module_registry_get(i);
        if (module && module->id) {
          if (prefs.begin("modules", true)) {
            Serial.printf("\n[Namespace: %s]", module->id);

            String key=String(module->id) + "_en";
            // 尝试读取 enabled 状态
            if (prefs.isKey(key.c_str())) {
              Serial.printf("  enabled = %d", prefs.getBool(key.c_str(), false));
            }

            prefs.end();
          }
        }
      }

      Serial.printf("\nNVS dump complete");
    }
    else if (command.startsWith("ver")) {
      // 查询固件版本号
      Serial.printf("Firmware Version: %s", FIRMWARE_VERSION);
      Serial.printf("Build Date: %s %s", __DATE__, __TIME__);
    }
  }

}
