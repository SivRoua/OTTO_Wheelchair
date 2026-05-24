#ifndef LORA_H
#define LORA_H

/*
 * LoRa 透传模块公开 API
 * ================================================================
 * Phase 1 — 核心骨架          ✅
 * Phase 2 — AT 指令交互       ✅
 * Phase 3 — 系统查询          ✅
 * Phase 4 — 参数配置          ✅
 * Phase 5 — 数据透明收发      ✅
 * Phase 6 — 定点收发 + RSSI   ✅
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "uart_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 1. 基础配置 ---- */
typedef struct {
    int aux_pin;
    uart_bus_config_t uart;
    bool drssi;                    /* 可选：初始是否启用数据末尾 RSSI 上报 */
} lora_config_t;

/* ---- 2. 射频参数结构体 ---- */
typedef struct {
    uint8_t  level;         /* 0~7 空中速率档位 */
    uint8_t  channel;       /* 0~63 工作信道 */
    uint8_t  power;         /* 0~22 dBm */
    uint8_t  mode;          /* 0=透明, 1=定点, 2=广播 */
    uint8_t  sleep_mode;    /* 0=休眠, 1=唤醒, 2=高时效 */
    uint8_t  packet;        /* 0=32B, 1=64B, 2=128B, 3=230B */
    bool     drssi;         /* RSSI 上报 */
    uint16_t addr;          /* 设备地址 */
} lora_config_params_t;

/* ---- 3. 不透明句柄 ---- */
typedef struct lora_ctx lora_ctx_t;

/* ---- Phase 1：核心骨架 ---- */
lora_ctx_t* lora_create(const lora_config_t *cfg, uart_bus_handle_t bus);
void       lora_deinit(lora_ctx_t *handle);
bool       lora_is_idle(lora_ctx_t *handle);

/* ---- Phase 2：AT 指令交互 ---- */
esp_err_t  lora_ping(lora_ctx_t *handle);

/* ---- Phase 3：系统查询 ---- */

/* 读取全部射频参数 */
esp_err_t lora_get_config(lora_ctx_t *handle, lora_config_params_t *params);

/* 查询当前信道噪声 */
int lora_get_noise(lora_ctx_t *handle);

/* ---- Phase 4：参数配置 ---- */

/* 批量写入并重启 */
esp_err_t lora_apply_config(lora_ctx_t *handle,
                             const lora_config_params_t *params);

/* 单参数设置 */
esp_err_t lora_set_power(lora_ctx_t *handle, int8_t dbm);
esp_err_t lora_set_channel(lora_ctx_t *handle, uint8_t ch);
esp_err_t lora_set_level(lora_ctx_t *handle, uint8_t level);

/* 软件重启 / 恢复出厂 */
esp_err_t lora_reset(lora_ctx_t *handle);
esp_err_t lora_factory_reset(lora_ctx_t *handle);

/* ---- Phase 5：数据透明收发 ---- */

/* 发送（透明模式） */
esp_err_t lora_send(lora_ctx_t *handle, const uint8_t *data, uint8_t len);

/* 接收（非阻塞） */
int lora_recv(lora_ctx_t *handle, uint8_t *buf, uint8_t max_len);

/* ---- Phase 6：定点传输 + RSSI ---- */

/* 定点发送到指定设备 */
esp_err_t lora_send_to(lora_ctx_t *handle, uint16_t addr, uint8_t ch,
                        const uint8_t *data, uint8_t len);

/* 最近一次接收的 RSSI */
int8_t lora_last_rssi(lora_ctx_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* LORA_H */