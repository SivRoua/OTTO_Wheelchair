#ifndef MOTION_H
#define MOTION_H

/*
 * motion — 电机控制组件
 * ================================================================
 * prepare → set → send 模式：
 *   先 motion_prepare() 准备帧缓冲区，
 *   再逐个调 motion_left/right/stepper 设置方向，
 *   最后 motion_send() 构建 6 字节帧通过 I2C 发给 sub-board (0x42)。
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_F 'F'
#define MOTION_B 'B'
#define MOTION_S 'S'

/* 初始化 I2C master，配好 sub-board 从机地址 0x42 */
esp_err_t motion_init(int port, int sda_pin, int scl_pin);

/* 重置待发帧缓冲区（不发 I2C），三电机默认 Stop */
void motion_prepare(void);

/* 左轮：'F'=正转, 'B'=反转, 'S'=停止 */
void motion_left(char dir);

/* 右轮：'F'=正转, 'B'=反转, 'S'=停止 */
void motion_right(char dir);

/* 步进电机：'F'=正转, 'B'=反转, 'S'=停止 */
void motion_stepper(char dir);

/* 构建 6 字节帧并 I2C 发送到 sub-board */
esp_err_t motion_send(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_H */
