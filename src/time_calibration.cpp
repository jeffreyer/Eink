#include "time_calibration.h"

// RTC 内存变量定义（深度休眠期间保持）
RTC_DATA_ATTR bool TimeCalibration::s_calibrated = false;
RTC_DATA_ATTR time_t TimeCalibration::s_last_sync_real = 0;
RTC_DATA_ATTR time_t TimeCalibration::s_last_sync_device = 0;
RTC_DATA_ATTR float TimeCalibration::s_drift_rate = 0.0f;

void TimeCalibration::init() {
    // RTC 变量在深度休眠后会保持，这里只需打印状态
    if (s_calibrated) {
        Serial.printf("TimeCalibration: Restored from RTC memory\n");
        Serial.printf("  Last sync real time: %ld\n", s_last_sync_real);
        Serial.printf("  Last sync device time: %ld\n", s_last_sync_device);
        Serial.printf("  Drift rate: %.6f s/s\n", s_drift_rate);
    } else {
        Serial.println("TimeCalibration: No calibration data");
    }
}

void TimeCalibration::calculate_drift_rate(time_t device_before_sync, time_t real_timestamp) {
    // 如果是第一次校准，无法计算漂移率
    if (!s_calibrated) {
        s_drift_rate = 0.0f;
        Serial.println("TimeCalibration: First sync, drift rate = 0");
        return;
    }

    // 计算自上次同步以来的时间差
    time_t elapsed_device = device_before_sync - s_last_sync_device;
    time_t elapsed_real = real_timestamp - s_last_sync_real;

    // 如果间隔太短（小于30分钟），不更新漂移率（数据不够准确）
    if (elapsed_real < 1800) {
        Serial.printf("TimeCalibration: Sync interval too short (%ld s), keeping old drift rate\n", elapsed_real);
        return;
    }

    // 计算漂移：设备时间的偏差
    int32_t drift_seconds = elapsed_device - elapsed_real;

    // 计算漂移率（秒/秒）
    float new_drift_rate = (float)drift_seconds / (float)elapsed_real;

    // 使用加权平均来平滑漂移率（避免单次测量误差）
    if (s_drift_rate != 0.0f) {
        s_drift_rate = s_drift_rate * 0.7f + new_drift_rate * 0.3f;
    } else {
        s_drift_rate = new_drift_rate;
    }

    Serial.printf("TimeCalibration: Drift calculation:\n");
    Serial.printf("  Elapsed real: %ld s (%.2f hours)\n", elapsed_real, elapsed_real / 3600.0f);
    Serial.printf("  Elapsed device: %ld s\n", elapsed_device);
    Serial.printf("  Drift: %ld s (%.2f minutes)\n", drift_seconds, drift_seconds / 60.0f);
    Serial.printf("  New drift rate: %.6f s/s (%.2f ppm)\n", new_drift_rate, new_drift_rate * 1e6);
    Serial.printf("  Smoothed drift rate: %.6f s/s (%.2f ppm)\n", s_drift_rate, s_drift_rate * 1e6);
}

void TimeCalibration::sync_time(time_t real_timestamp) {
    // 获取同步前的设备时间
    time_t device_before_sync = time(NULL);

    // 计算漂移率（如果有上次的校准数据）
    calculate_drift_rate(device_before_sync, real_timestamp);

    // 设置系统时间
    struct timeval tv;
    tv.tv_sec = real_timestamp;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    // 保存新的校准点
    s_last_sync_real = real_timestamp;
    s_last_sync_device = real_timestamp;  // 同步后设备时间等于真实时间
    s_calibrated = true;

    Serial.printf("TimeCalibration: Time synced to %ld\n", real_timestamp);

    // 打印当前时间用于验证
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    Serial.printf("TimeCalibration: Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

time_t TimeCalibration::get_calibrated_time() {
    // 获取当前系统时间
    time_t current_time = time(NULL);

    // 如果没有校准数据或漂移率为0，直接返回系统时间
    if (!s_calibrated || s_drift_rate == 0.0f) {
        return current_time;
    }

    // 计算自上次同步以来经过的时间（设备时间）
    time_t elapsed = current_time - s_last_sync_device;

    // 如果刚同步不久（5分钟内），直接返回系统时间（误差很小）
    if (elapsed < 300) {
        return current_time;
    }

    // 根据漂移率估算当前的累积漂移
    int32_t estimated_drift = (int32_t)(elapsed * s_drift_rate);

    // 校正时间：减去估算的漂移
    time_t calibrated_time = current_time - estimated_drift;

    // 调试输出（可选，避免过多日志）
    // Serial.printf("TimeCalibration: Raw=%ld, Drift=%ld, Calibrated=%ld\n",
    //               current_time, estimated_drift, calibrated_time);

    return calibrated_time;
}
