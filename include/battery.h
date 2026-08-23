#pragma once

#include <stdio.h>
#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "lua_hardware_api.h"

#define ADC_CHANNEL ADC_CHANNEL_0   // GPIO0

int check_bat();

int check_battery_init();

int battery_get_mv();
