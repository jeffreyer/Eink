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
void ble_config_toggle(void);
void ble_config_render_mode(void);
void ble_config_unbind(void);

#ifdef __cplusplus
}
#endif
