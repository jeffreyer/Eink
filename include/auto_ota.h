#pragma once

#include <Arduino.h>

/**
 * 检查并执行自动 OTA 更新
 *
 * 功能：
 * - 检查 /extflash/firmware.bin 是否存在
 * - 如果存在，执行 OTA 更新
 * - 更新成功后删除固件文件并重启
 * - 更新失败则删除固件文件
 *
 * @return true 如果执行了 OTA 更新（无论成功失败），false 如果没有固件文件
 */
bool auto_ota_check_and_update();
