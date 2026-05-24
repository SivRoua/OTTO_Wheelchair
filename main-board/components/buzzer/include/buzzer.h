#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void buzzer_init(int pin);
void buzzer_on(void);
void buzzer_off(void);

/* 异步叫 duration_ms 毫秒后自动停止，立即返回 */
void buzzer_beep(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */
