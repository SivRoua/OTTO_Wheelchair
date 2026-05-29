#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 LD2402 IO 引脚，传 -1 则使用 Kconfig 默认引脚 (CONFIG_LD2402_IO_PIN) */
esp_err_t ld2402_io_init(int io_pin);

/* 读取当前检测状态，true = 有人，false = 无人 */
bool ld2402_io_is_person_detected(void);

#ifdef __cplusplus
}
#endif
