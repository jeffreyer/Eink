#include "battery.h"
#include <stdio.h>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lua_hardware_api.h"

bool calibrated=false;
static int s_battery_mv = 0;
static int s_battery_percent = 0;
static bool s_battery_read = false;

uint32_t readADCVoltage()
{
    uint32_t total = 0;

    // 多次采样平均
    for(int i = 0; i < 32; i++)
    {
        total += analogReadMilliVolts(ADC_CHANNEL);
        delay(2);
    }

    return total / 32;
}

int check_bat(){
    // ADC脚实际电压
    uint32_t adc_mv = readADCVoltage();

    // 根据分压还原输入电压
    float voltage = adc_mv * 2.0f;  // 分压系数为2，假设使用了1:1的分压电阻网络

    s_battery_mv = (int)voltage;
    s_battery_read = true;

    // 线性映射 3300mV~4200mV → 0~100%（1S 锂电）
    int pct = (s_battery_mv - 3300) * 100 / 900;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_battery_percent = pct;
    is_low_bat = (s_battery_mv < 3400);

    Serial.printf("Battery voltage: %d mV (%d%%)\n", s_battery_mv, pct);
    return s_battery_mv;
}

int battery_get_mv(void) {
    if (!s_battery_read) {
        check_bat();
    }
    return s_battery_mv;
}

int check_battery_init() {
     // ADC配置
    analogReadResolution(12);


    // ESP32-C3 推荐11dB衰减
    // 支持更高输入范围
    analogSetPinAttenuation(
        ADC_CHANNEL,
        ADC_11db
    );


    return 0;
}
