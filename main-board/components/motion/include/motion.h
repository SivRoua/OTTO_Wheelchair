#ifndef MOTION_H
#define MOTION_H

/*
 * motion — 电机控制组件
 * ================================================================
 * prepare → set → send 模式：
 *   先 motion_prepare() 准备 MotorCommand（三电机默认 Stop），
 *   再逐个调 motion_left/right/stepper 设置方向，
 *   最后 motion_send() 将 MotorCommand 编码为 6 字节帧并通过 I2C 发出。
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_F 'F'
#define MOTION_B 'B'
#define MOTION_S 'S'

typedef enum {
    DIR_STOP     = 0,
    DIR_FORWARD  = 1,
    DIR_BACKWARD = 2,
} dir_t;

typedef struct {
    dir_t left;
    dir_t right;
    dir_t stepper;
} motor_command_t;

/* 初始化 I2C master，配好 sub-board 从机地址 0x42 */
esp_err_t motion_init(int port, int sda_pin, int scl_pin);

/* 重置 MotorCommand，三电机默认 Stop */
void motion_prepare(void);

/* 设置左轮：'F'=正转, 'B'=反转, 'S'=停止 */
void motion_left(char dir);

/* 设置右轮：'F'=正转, 'B'=反转, 'S'=停止 */
void motion_right(char dir);

/* 设置步进电机：'F'=正转, 'B'=反转, 'S'=停止 */
void motion_stepper(char dir);

/* 将 MotorCommand 编码为 6 字节帧并 I2C 发送到 sub-board */
esp_err_t motion_send(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_H */
