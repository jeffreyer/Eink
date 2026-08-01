#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IDLE_TIMEOUT_DEFAULT 30
#define BRIGHTNESS_FADE_STEP 1

extern uint32_t s_idle_timeout_ms;

typedef void (*sleep_manager_key_cb_t)(void);

// 初始化睡眠管理器
int sleep_manager_init();

// 启动睡眠管理器（需要在主循环中定期调用）
int sleep_manager_start(void);

// 处理睡眠管理逻辑（在主循环中调用）
void sleep_manager_update();

// 重置空闲计时器（用于退出蓝牙模式等场景）
void sleep_manager_reset_idle_timer();

void enter_deep_sleep(void);

#ifdef __cplusplus
}
#endif

