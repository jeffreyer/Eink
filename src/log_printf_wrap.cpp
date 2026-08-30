// 兼容 Arduino core 3.3.8：pioarduino 链接脚本仍对 log_printf 启用
// -Wl,--wrap=log_printf，但新 core 把 log_printf 定义移入 esp32-hal-uart.c
// 且不再提供 __wrap_log_printf，导致链接报 undefined reference。
// 这里补充包装实现，转发到 core 的 log_printfv，避免重复格式化逻辑。
#include <stdarg.h>

extern "C" int log_printfv(const char *format, va_list arg);

extern "C" int __wrap_log_printf(const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  int len = log_printfv(format, arg);
  va_end(arg);
  return len;
}
