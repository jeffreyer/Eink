#include "battery.h"
#include <stdio.h>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lua_hardware_api.h"

bool calibrated=false;

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
    float voltage = adc_mv *
                     2.0;  // 分压系数为2，假设使用了1:1的分压电阻网络

    Serial.printf("Battery voltage: %.2f mV\n", voltage);
    return voltage;
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