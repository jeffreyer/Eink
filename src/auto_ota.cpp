#include "auto_ota.h"
#include <Update.h>
#include <sys/stat.h>
#include "lua_hardware_api.h"
#include <FastLED.h>

#define FIRMWARE_PATH "/extflash/firmware.bin"
#define BUFFER_SIZE 4096

bool auto_ota_check_and_update() {
    // 检查固件文件是否存在
    struct stat st;
    if (stat(FIRMWARE_PATH, &st) != 0) {
        // 文件不存在，正常启动
        return false;
    }

    Serial.println("========================================");
    Serial.println("发现固件文件，开始自动 OTA 更新...");
    Serial.printf("固件大小: %d 字节\n", (int)st.st_size);
    Serial.println("========================================");

    FastLED.clear();
    draw_led_text("OTA",1,1,50,0,0);
    FastLED.show();
    // 打开固件文件
    FILE* firmware_file = fopen(FIRMWARE_PATH, "rb");
    if (!firmware_file) {
        Serial.println("错误: 无法打开固件文件");
        // 尝试删除损坏的文件
        remove(FIRMWARE_PATH);
        return true;
    }

    // 开始 OTA 更新
    if (!Update.begin(st.st_size)) {
        Serial.printf("错误: OTA 初始化失败 - %s\n", Update.errorString());
        fclose(firmware_file);
        remove(FIRMWARE_PATH);
        return true;
    }

    // 分块读取并写入
    uint8_t* buffer = (uint8_t*)malloc(BUFFER_SIZE);
    if (!buffer) {
        Serial.println("错误: 内存分配失败");
        fclose(firmware_file);
        Update.abort();
        remove(FIRMWARE_PATH);
        return true;
    }

    size_t total_written = 0;
    size_t bytes_read;
    int last_progress = -1;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, firmware_file)) > 0) {
        size_t written = Update.write(buffer, bytes_read);
        if (written != bytes_read) {
            Serial.printf("错误: 写入失败 (写入 %d / 读取 %d)\n", written, bytes_read);
            break;
        }

        total_written += written;

        // 显示进度（每 10% 显示一次）
        int progress = (total_written * 100) / st.st_size;
        if (progress / 10 != last_progress / 10) {
            Serial.printf("OTA 进度: %d%%\n", progress);
            last_progress = progress;
        }
    }

    free(buffer);
    fclose(firmware_file);

    // 检查是否完整写入
    if (total_written != st.st_size) {
        Serial.printf("错误: 写入不完整 (%d / %d 字节)\n", total_written, (int)st.st_size);
        Update.abort();
        remove(FIRMWARE_PATH);
        return true;
    }

    // 完成 OTA 更新
    if (Update.end(true)) {
        Serial.println("========================================");
        Serial.println("OTA 更新成功！");
        Serial.println("删除固件文件并重启...");
        Serial.println("========================================");

        // 删除固件文件
        remove(FIRMWARE_PATH);

        delay(1000);

        // 重启设备
        ESP.restart();
    } else {
        Serial.printf("错误: OTA 完成失败 - %s\n", Update.errorString());
        remove(FIRMWARE_PATH);
        return true;
    }

    return true;
}
