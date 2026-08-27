#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 统一休眠策略：任何逻辑执行完后调用，立即进入深度休眠；
// 唤醒源由调用方保证（按键唤醒先处理按键，BLE 模式不调用）
void enter_deep_sleep(void);

#ifdef __cplusplus
}
#endif

