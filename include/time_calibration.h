#pragma once

#include <Arduino.h>
#include <sys/time.h>

// 时间校准管理器
class TimeCalibration {
public:
    // 初始化（从RTC内存恢复校准数据）
    static void init();

    // 同步时间并记录校准点（在收到前端时间同步时调用）
    static void sync_time(time_t real_timestamp);

    // 获取校准后的时间（用于提供给其他模块）
    static time_t get_calibrated_time();

private:
    // RTC内存变量（深度休眠期间保持）
    static bool s_calibrated;
    static time_t s_last_sync_real;      // 上次同步时的真实时间戳
    static time_t s_last_sync_device;    // 上次同步时的设备时间戳
    static float s_drift_rate;           // 时钟漂移率（秒/秒）

    // 计算漂移率
    static void calculate_drift_rate(time_t device_before_sync, time_t real_timestamp);
};
