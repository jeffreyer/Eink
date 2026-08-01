#include "cmd_handler.h"
#include "common.h"
#include "battery.h"
#include "module_registry.h"
#include "ble_config.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>
#include <sys/stat.h>
#include <dirent.h>
#include "eink.h"

void check_cmd(){
  if (Serial.available() > 0) {
    String command = Serial.readString();
    command.trim();

    // 调试：确认收到命令
    log_e("[CMD] Received: %s", command.c_str());

    if (command.startsWith("sleep=")) {
      int sec=command.substring(6).toInt();
      save_config("sleep_sec",sec);
      extern uint32_t s_idle_timeout_ms;
      s_idle_timeout_ms=sec*1000;
      log_e("Set sleep delay seconds: %d", sec);
    }
    else if (command.startsWith("bat=")) {
      String value = command.substring(4);
      bool enabled = value.toInt() != 0;
      save_config("chk_bat", enabled);
      extern bool is_chk_bat;
      is_chk_bat = enabled;
      log_e("Battery Check %s", enabled ? "enabled" : "disabled");
    }
    else if (command.startsWith("bat?")) {
      int voltage = check_bat();
      log_e("Battery Voltage: %d mV", voltage);
    }
    else if (command.startsWith("gettime?")) {
      time_t now = time(NULL);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      log_e("Current time: %04d-%02d-%02d %02d:%02d:%02d (timestamp: %ld)",
        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, now);
    }
    else if (command.startsWith("lsmod")) {
      log_e("Registered modules:");
      uint8_t count = module_registry_count();
      for (uint8_t i = 0; i < count; i++) {
        const module_descriptor_t* module = module_registry_get(i);
        if (module && module->id) {
          bool enabled = module_registry_is_enabled(i);
          const char* type = module->script_path ? "dynamic" : "builtin";
          const char* path = module->script_path ? module->script_path : "N/A";
          log_e("  [%d] %s %s (configs: %d, enabled: %s, path: %s)",
                i, module->id, type, module->config_count,
                enabled ? "yes" : "no", path);
        }
      }
      log_e("Total: %d modules", count);
    }
    else if (command.startsWith("lsi")) {
      log_e("Files in /spiffs:");
      DIR* dir = opendir("/spiffs");
      if (!dir) {
        log_e("ERROR: Cannot open /spiffs (errno: %d)", errno);
      } else {
        struct dirent* entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {
          char fullpath[256];
          snprintf(fullpath, sizeof(fullpath), "/spiffs/%s", entry->d_name);
          struct stat st;
          if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
              log_e("  [DIR]  %s", entry->d_name);
            } else {
              log_e("  [FILE] %s (%u bytes)", entry->d_name, (unsigned int)st.st_size);
            }
            count++;
          }
        }
        closedir(dir);
        if (count == 0) {
          log_e("  (empty)");
        }
        log_e("Total: %d items", count);
      }
    }
    else if (command.startsWith("read ")) {
      String filename = command.substring(5);
      filename.trim();

      if (filename.length() == 0) {
        log_e("ERROR: No filename specified. Usage: read <filename>");
      } else {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename.c_str());

        FILE* fp = fopen(filepath, "r");
        if (!fp) {
          log_e("ERROR: Cannot open file %s (errno: %d)", filepath, errno);
        } else {
          log_e("Reading file: %s", filepath);
          log_e("--- BEGIN FILE CONTENT ---");

          char buffer[256];
          while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            log_e("%s", buffer);
          }

          log_e("--- END FILE CONTENT ---");
          fclose(fp);
        }
      }
    }
    else if (command.startsWith("rm ")) {
      // 删除 /spiffs 下的文件
      String filename = command.substring(3);
      filename.trim();

      if (filename.length() == 0) {
        log_e("ERROR: No filename specified. Usage: rm <filename>");
      } else {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename.c_str());

        // 检查文件是否存在
        struct stat st;
        if (stat(filepath, &st) != 0) {
          log_e("ERROR: File not found: %s", filepath);
        } else if (S_ISDIR(st.st_mode)) {
          log_e("ERROR: Cannot remove directory: %s", filepath);
          log_e("Use a different method to remove directories");
        } else {
          // 删除文件
          if (remove(filepath) == 0) {
            log_e("File deleted successfully: %s", filepath);
          } else {
            log_e("ERROR: Failed to delete file: %s (errno: %d)", filepath, errno);
          }
        }
      }
    }
    else if (command.startsWith("dis ")) {
      // 删除 /spiffs 下的文件
      String str = command.substring(4);

      if (str.length() == 0) {
        log_e("ERROR: No str specified");
      } else {
        log_e("Display string: %s", str.c_str());
        gui_drawtext(str.c_str());
      }
    }
    else if (command.startsWith("ls")) {
      log_e("Files in /extflash:");
      log_e("Attempting to open directory...");
      DIR* dir = opendir("/extflash");
      if (!dir) {
        log_e("ERROR: Cannot open /extflash (errno: %d)", errno);
        log_e("Storage may still be locked by MSC");
      } else {
        struct dirent* entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {
          char fullpath[256];
          snprintf(fullpath, sizeof(fullpath), "/extflash/%s", entry->d_name);
          struct stat st;
          if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
              log_e("  [DIR]  %s", entry->d_name);
            } else {
              log_e("  [FILE] %s (%u bytes)", entry->d_name, (unsigned int)st.st_size);
            }
            count++;
          }
        }
        closedir(dir);
        if (count == 0) {
          log_e("  (empty)");
        }
        log_e("Total: %d items", count);
      }
    }
    else if (command.startsWith("dfu")) {
      // 进入 ROM 下载模式（等同于 GPIO0 拉低 + 复位）
      log_e("[DFU] Entering ROM download mode...");

      // 设置 RTC 寄存器强制进入下载模式
      REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);

      // 重启
      esp_restart();
    }
    else if (command.startsWith("unbind")) {
      // 清除蓝牙绑定状态
      ble_config_unbind();
      log_e("[BLE] Device unbound, password regenerated");
    }
    else if (command.startsWith("nvs")) {
      // 读取 NVS 中所有 key 和值
      log_e("Reading NVS storage:");

      Preferences prefs;

      // 读取 bottle 命名空间
      if (prefs.begin("bottle", true)) {
        log_e("\n[Namespace: bottle]");
        log_e("  page_index = %d", prefs.getInt("page_index", -1));
        log_e("  brightness = %d", prefs.getInt("brightness", -1));
        log_e("  sleep_sec = %d", prefs.getInt("sleep_sec", -1));
        log_e("  i2s_mic = %d", prefs.getInt("i2s_mic", -1));
        log_e("  led = %d", prefs.getInt("led", -1));
        prefs.end();
      } else {
        log_e("  ERROR: Cannot open namespace 'bottle'");
      }

      // 读取 ble_config 命名空间
      if (prefs.begin("ble_sec", true)) {
        log_e("\n[Namespace: ble_sec]");
        log_e("  bound = %d", prefs.getBool("bound", false));
        prefs.end();
      } else {
        log_e("  ERROR: Cannot open namespace 'ble_sec'");
      }

      // 读取模块配置（遍历可能的模块 ID）
      uint8_t module_count = module_registry_count();
      for (uint8_t i = 0; i < module_count; i++) {
        const module_descriptor_t* module = module_registry_get(i);
        if (module && module->id) {
          if (prefs.begin("modules", true)) {
            log_e("\n[Namespace: %s]", module->id);

            String key=String(module->id) + "_en";
            // 尝试读取 enabled 状态
            if (prefs.isKey(key.c_str())) {
              log_e("  enabled = %d", prefs.getBool(key.c_str(), false));
            }

            prefs.end();
          }
        }
      }

      log_e("\nNVS dump complete");
    }
    else if (command.startsWith("ver")) {
      // 查询固件版本号
      log_e("Firmware Version: %s", FIRMWARE_VERSION);
      log_e("Build Date: %s %s", __DATE__, __TIME__);
    }
  }

}
