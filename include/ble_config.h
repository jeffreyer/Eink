#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern bool s_ble_enabled;

void ble_config_init(void);
void ble_config_stop(void);
void ble_config_update(void);
void ble_config_publish_status(void);
bool ble_config_is_enabled(void);
// 蓝牙会话结束（连接过并已断开/退出）且无待处理命令 → 应立即休眠
bool ble_config_should_sleep_after_disconnect(void);
void ble_config_toggle(void);
void ble_config_unbind(void);

#ifdef __cplusplus
}
#endif
