/*
 * LoRa 透传模块驱动核心（Phase 1-6）- 时序修复版
 * ================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "LoRa.h"
#include "LoRa_internal.h"
#include "lora_port.h"

extern const lora_port_ops_t lora_port_ops;

/* ================================================================
 * 内部上下文
 * ================================================================ */
struct lora_ctx {
    lora_port_ops_t     ops;
    uart_bus_handle_t   bus;
    lora_config_t       config;
    int8_t              last_rssi;      /* Phase 6: 缓存 RSSI */
};

/* ================================================================
 * 内部辅助 — AUX 等待
 * ================================================================ */
static bool wait_aux_idle(lora_ctx_t *ctx, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (ctx->ops.gpio_read_aux() && elapsed < timeout_ms) {
        ctx->ops.delay_ms(LORA_AUX_POLL_MS);
        elapsed += LORA_AUX_POLL_MS;
    }
    return !ctx->ops.gpio_read_aux();
}

/* ================================================================
 * 内部辅助 — 等待 AUX 变高（数据到达 / 操作启动）
 * ================================================================ */
// static bool wait_aux_busy(lora_ctx_t *ctx, uint32_t timeout_ms)
// {
//     uint32_t elapsed = 0;
//     while (!ctx->ops.gpio_read_aux() && elapsed < timeout_ms) {
//         ctx->ops.delay_ms(LORA_AUX_POLL_MS);
//         elapsed += LORA_AUX_POLL_MS;
//     }
//     return ctx->ops.gpio_read_aux();
// }

/* ================================================================
 * 内部辅助 — 读取 AT 回复
 * ================================================================ */
static int read_at_response(lora_ctx_t *ctx, char *resp_buf,
                             int buf_size, uint32_t timeout_ms)
{
    int pos = 0;
    int remaining = buf_size - 1;
    uint32_t elapsed = 0;

    while (elapsed < timeout_ms) {
        uint8_t tmp[64];
        int n = uart_bus_read(ctx->bus, tmp,
                    (remaining < 64) ? remaining : 64);
        if (n > 0) {
            memcpy(resp_buf + pos, tmp, n);
            pos += n;
            remaining -= n;
            resp_buf[pos] = '\0';
            /* 读到数据后 AUX 变低 = 模块回复完成 */
            if (!ctx->ops.gpio_read_aux()) break;
            if (remaining <= 0) break;
        }
        ctx->ops.delay_ms(LORA_AT_POLL_MS);
        elapsed += LORA_AT_POLL_MS;
    }
    return pos;
}

/* ================================================================
 * 内部辅助 — 进入 AT 模式
 * ================================================================ */
static esp_err_t lora_enter_at_mode(lora_ctx_t *ctx)
{
    char resp[LORA_AT_RESP_BUF_SIZE];
    ctx->ops.delay_ms(1000);
    uart_bus_flush(ctx->bus);
    uart_bus_write(ctx->bus,
        (const uint8_t *)LORA_AT_ENTER_CMD,
        strlen(LORA_AT_ENTER_CMD));
    ctx->ops.delay_ms(500);
    memset(resp, 0, sizeof(resp));
    read_at_response(ctx, resp, sizeof(resp), LORA_AT_ENTRY_TIMEOUT_MS);
    return (strstr(resp, LORA_AT_RESP_ENTRY) != NULL) ? ESP_OK : ESP_FAIL;
}

/* ================================================================
 * 内部辅助 — 退出 AT 模式
 * ================================================================ */
static esp_err_t lora_exit_at_mode(lora_ctx_t *ctx)
{
    uart_bus_write(ctx->bus,
        (const uint8_t *)LORA_AT_EXIT_CMD,
        strlen(LORA_AT_EXIT_CMD));
    ctx->ops.delay_ms(200);
    if (!wait_aux_idle(ctx, LORA_AT_EXIT_TIMEOUT_MS))
        return ESP_ERR_TIMEOUT;
    uart_bus_flush(ctx->bus);
    return ESP_OK;
}

/* ================================================================
 * 内部辅助 — 在 AT 模式下发一条指令并检查 OK
 * ================================================================ */
static esp_err_t lora_send_cmd(lora_ctx_t *ctx, const char *cmd,
                                char *resp_buf, int resp_buf_size)
{
    uart_bus_flush(ctx->bus);
    uart_bus_write(ctx->bus, (const uint8_t *)cmd, strlen(cmd));
    uart_bus_write(ctx->bus,
        (const uint8_t *)LORA_AT_CRLF, strlen(LORA_AT_CRLF));
    ctx->ops.delay_ms(100);
    memset(resp_buf, 0, resp_buf_size);
    int len = read_at_response(ctx, resp_buf, resp_buf_size,
                                LORA_AT_CMD_TIMEOUT_MS);
    if (len <= 0) return ESP_ERR_TIMEOUT;
    if (strstr(resp_buf, LORA_AT_RESP_OK) != NULL) return ESP_OK;
    if (strstr(resp_buf, LORA_AT_RESP_ERROR) != NULL) return ESP_FAIL;
    return ESP_FAIL;
}

/* ================================================================
 * 内部辅助 — 执行一条完整 AT 指令（进入+发送+退出）
 * ================================================================ */
static esp_err_t lora_exec_cmd(lora_ctx_t *ctx, const char *cmd)
{
    esp_err_t ret;
    char resp[LORA_AT_RESP_BUF_SIZE];
    ret = lora_enter_at_mode(ctx);
    if (ret != ESP_OK) return ret;
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    esp_err_t exit_ret = lora_exit_at_mode(ctx);
    if (ret != ESP_OK) return ret;
    return exit_ret;
}

/* ================================================================
 * Phase 1：核心骨架
 * ================================================================ */

lora_ctx_t* lora_create(const lora_config_t *cfg, uart_bus_handle_t bus)
{
    if (!cfg) return NULL;
    lora_ctx_t *ctx = (lora_ctx_t *)calloc(1, sizeof(lora_ctx_t));
    if (!ctx) return NULL;
    ctx->ops = lora_port_ops;
    ctx->bus = bus;
    ctx->config = *cfg;
    ctx->last_rssi = 0;
    ctx->ops.init(cfg->aux_pin);
    if (!wait_aux_idle(ctx, LORA_AUX_TIMEOUT_MS)) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void lora_deinit(lora_ctx_t *handle)
{
    if (!handle) return;
    free(handle);
}

bool lora_is_idle(lora_ctx_t *handle)
{
    if (!handle) return false;
    return !handle->ops.gpio_read_aux();
}

/* ================================================================
 * Phase 2：AT 指令交互
 * ================================================================ */

esp_err_t lora_ping(lora_ctx_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return lora_exec_cmd(handle, "AT");
}

/* ================================================================
 * Phase 3：系统查询
 * ================================================================ */

esp_err_t lora_get_config(lora_ctx_t *handle, lora_config_params_t *params)
{
    if (!handle || !params) return ESP_ERR_INVALID_ARG;
    char resp[LORA_AT_RESP_BUF_SIZE * 2];
    esp_err_t ret;

    ret = lora_enter_at_mode(handle);
    if (ret != ESP_OK) return ret;

    uart_bus_flush(handle->bus);
    uart_bus_write(handle->bus, (const uint8_t *)"AT+HELP", 7);
    uart_bus_write(handle->bus, (const uint8_t *)"\r\n", 2);
    handle->ops.delay_ms(50);

    int total = 0;
    uint32_t elapsed = 0;
    memset(resp, 0, sizeof(resp));
    while (elapsed < 3000 && total < sizeof(resp) - 1) {
        int n = uart_bus_read(handle->bus, (uint8_t *)resp + total,
                              sizeof(resp) - total - 1);
        if (n > 0) {
            total += n;
        }
        if (total > 0 && !handle->ops.gpio_read_aux()) {
            break;
        }
        handle->ops.delay_ms(10);
        elapsed += 10;
    }
    resp[total] = '\0';

    lora_exit_at_mode(handle);

    memset(params, 0, sizeof(*params));

    char *line = strtok(resp, "\r\n");
    while (line != NULL) {
        if (strstr(line, "===") || strstr(line, "LoRa Parameter") || strlen(line) == 0) {
            line = strtok(NULL, "\r\n");
            continue;
        }

        int tmp_int;

        if (sscanf(line, "MODE:%d", &tmp_int) == 1) {
            params->mode = (uint8_t)tmp_int;
        }
        else if (sscanf(line, "LEVEL:%d", &tmp_int) == 1) {
            params->level = (uint8_t)tmp_int;
        }
        else if (sscanf(line, "SLEEP:%d", &tmp_int) == 1) {
            params->sleep_mode = (uint8_t)tmp_int;
        }
        else if (strstr(line, "Frequency:")) {
            char *p = strstr(line, ">> ");
            if (p) {
                p += 3;
                params->channel = (uint8_t)strtol(p, NULL, 16);
            }
        }
        else if (strstr(line, "MAC:")) {
            unsigned int hi, lo;
            if (sscanf(line, "MAC:%x,%x", &hi, &lo) == 2) {
                params->addr = (uint16_t)((hi << 8) | lo);
            }
        }
        else if (sscanf(line, "Power:%d", &tmp_int) == 1) {
            params->power = (int8_t)tmp_int;
        }

        line = strtok(NULL, "\r\n");
    }

    return ESP_OK;
}

int lora_get_noise(lora_ctx_t *handle)
{
    if (!handle) return 0;

    esp_err_t ret = lora_enter_at_mode(handle);
    if (ret != ESP_OK) return 0;

    uart_bus_flush(handle->bus);
    uart_bus_write(handle->bus, (const uint8_t *)"AT+ERSSI\r\n", 10);
    handle->ops.delay_ms(50);

    char resp[32];
    int total = 0;
    uint32_t elapsed = 0;
    memset(resp, 0, sizeof(resp));
    while (elapsed < 3000 && total < sizeof(resp) - 1) {
        int n = uart_bus_read(handle->bus, (uint8_t *)resp + total,
                              sizeof(resp) - total - 1);
        if (n > 0) {
            total += n;
        }
        if (total > 0 && !handle->ops.gpio_read_aux()) {
            break;
        }
        handle->ops.delay_ms(10);
        elapsed += 10;
    }

    lora_exit_at_mode(handle);

    int noise = 0;
    const char *p = resp;
    while (*p) {
        if (*p == '-' || (*p >= '0' && *p <= '9')) {
            noise = atoi(p);
            break;
        }
        p++;
    }
    return noise;
}

/* ================================================================
 * Phase 4：参数配置
 * ================================================================ */

/*
 * 修复点（问题 4）：
 * 将每条配置指令由裸写入改为调用 lora_send_cmd。
 * 该方法自动轮询检查回复是否含有 "+OK"，防范指令堆叠和执行丢失。
 */
static esp_err_t lora_send_config_cmds(lora_ctx_t *ctx,
                                        const lora_config_params_t *params)
{
    char cmd[64];
    char resp[LORA_AT_RESP_BUF_SIZE];
    esp_err_t ret;

    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_LEVEL, params->level);
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    snprintf(cmd, sizeof(cmd), "%s%02X", AT_CMD_CHANNEL, params->channel);
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_POWER, params->power);
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_MODE, params->mode);
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_SLEEP, params->sleep_mode);
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_PACKET, params->packet);
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_DRSSI, params->drssi ? 1 : 0);
    ret = lora_send_cmd(ctx, cmd, resp, sizeof(resp));
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t lora_apply_config(lora_ctx_t *handle,
                             const lora_config_params_t *params)
{
    if (!handle || !params) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = lora_enter_at_mode(handle);
    if (ret != ESP_OK) return ret;

    ret = lora_send_config_cmds(handle, params);
    if (ret != ESP_OK) {
        lora_exit_at_mode(handle);
        return ret;
    }

    char resp[LORA_AT_RESP_BUF_SIZE];
    ret = lora_send_cmd(handle, AT_CMD_RESET, resp, sizeof(resp));

    lora_exit_at_mode(handle);

    wait_aux_idle(handle, LORA_AUX_TIMEOUT_MS);

    return ret;
}

esp_err_t lora_set_power(lora_ctx_t *handle, int8_t dbm)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (dbm < 0 || dbm > 22) return ESP_ERR_INVALID_ARG;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_POWER, (int)dbm);
    return lora_exec_cmd(handle, cmd);
}

esp_err_t lora_set_channel(lora_ctx_t *handle, uint8_t ch)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (ch > 63) return ESP_ERR_INVALID_ARG;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s%02X", AT_CMD_CHANNEL, ch);
    return lora_exec_cmd(handle, cmd);
}

esp_err_t lora_set_level(lora_ctx_t *handle, uint8_t level)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (level > 7) return ESP_ERR_INVALID_ARG;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s%d", AT_CMD_LEVEL, (int)level);
    return lora_exec_cmd(handle, cmd);
}

esp_err_t lora_reset(lora_ctx_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = lora_exec_cmd(handle, AT_CMD_RESET);
    wait_aux_idle(handle, LORA_AUX_TIMEOUT_MS);
    return ret;
}

esp_err_t lora_factory_reset(lora_ctx_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = lora_exec_cmd(handle, AT_CMD_DEFAULT);
    if (ret == ESP_OK) {
        ret = lora_reset(handle);
    }
    return ret;
}

/* ================================================================
 * Phase 5：数据透明收发
 * ================================================================ */

esp_err_t lora_send(lora_ctx_t *handle, const uint8_t *data, uint8_t len)
{
    if (!handle || !data) return ESP_ERR_INVALID_ARG;
    if (len == 0 || len > LORA_MAX_PACKET_LEN) return ESP_ERR_INVALID_ARG;

    if (!wait_aux_idle(handle, LORA_TX_TIMEOUT_MS))
        return ESP_ERR_TIMEOUT;

    uart_bus_write(handle->bus, data, len);

    /* 
     * 修复点（问题 1）：
     * 根据当前包长度，按典型最恶劣工况（9600bps 物理波特率下，1 字节传输需约 1.04ms）估算传输时序，
     * 额外引入 15ms 基准安全冗余，替代死延时，从而给模块足够的反应时间去接收并拉高 AUX 信号。
     */
    uint32_t tx_delay = 15 + ((uint32_t)len * 12) / 10;
    handle->ops.delay_ms(tx_delay);

    if (!wait_aux_idle(handle, LORA_TX_TIMEOUT_MS))
        return ESP_ERR_TIMEOUT;

    return ESP_OK;
}

/*
 * 修复点（问题 2 与 问题 3）：
 * 建立守护式的循环接收逻辑，整合并保障一个包的完整性，防止数据发生不规则切碎和截断。
 */
int lora_recv(lora_ctx_t *handle, uint8_t *buf, uint8_t max_len)
{
    if (!handle || !buf || max_len == 0) return 0;

    int total = 0;
    uint32_t elapsed = 0;
    const uint32_t timeout_ms = 150; // 设定合理接收包最大帧内超时

    /* 
     * 只要当前接收未填满用户缓冲区、且未超时，
     * 在模块仍在输出（AUX 高）或能继续读出数据的情况下，维持读取拼装完整包
     */
    while (total < max_len && elapsed < timeout_ms) {
        int n = uart_bus_read(handle->bus, buf + total, max_len - total);
        if (n > 0) {
            total += n;
            elapsed = 0; // 重置无数据超时
        } else {
            /* 
             * 当前没有读到新数据。
             * 此时如果模块指示已经恢复空闲（AUX 变低），表示物理吐包流程结束。
             */
            if (!handle->ops.gpio_read_aux()) {
                break;
            }
            handle->ops.delay_ms(2);
            elapsed += 2;
        }
    }

    /* 
     * 此时已经拿到了相对完整的一包，其真正有效长度为 total。
     * 在包收集确认后，方可对真正的包尾提取 RSSI 字节。
     */
    if (handle->config.drssi && total > 0) {
        handle->last_rssi = (int8_t)buf[total - 1];
        total--; // 裁剪剔除 RSSI，不干扰用户载荷
    }

    return total;
}

/* ================================================================
 * Phase 6：定点传输 + RSSI
 * ================================================================ */

esp_err_t lora_send_to(lora_ctx_t *handle, uint16_t addr, uint8_t ch,
                        const uint8_t *data, uint8_t len)
{
    if (!handle || !data) return ESP_ERR_INVALID_ARG;
    if (len == 0 || len > LORA_MAX_PACKET_LEN - FIXED_HEADER_LEN)
        return ESP_ERR_INVALID_ARG;

    if (!wait_aux_idle(handle, LORA_TX_TIMEOUT_MS))
        return ESP_ERR_TIMEOUT;

    uint8_t header[FIXED_HEADER_LEN];
    header[0] = (uint8_t)(addr >> 8);    /* 地址高字节 */
    header[1] = (uint8_t)(addr & 0xFF);  /* 地址低字节 */
    header[2] = ch;                       /* 目标信道 */

    uart_bus_write(handle->bus, header, FIXED_HEADER_LEN);
    uart_bus_write(handle->bus, data, len);

    /* 修复点（问题 1）：在定点模式下，总发送字节数为 Payload + 3 字节的首部，应用相同的保护时序 */
    uint32_t total_len = len + FIXED_HEADER_LEN;
    uint32_t tx_delay = 15 + (total_len * 12) / 10;
    handle->ops.delay_ms(tx_delay);

    if (!wait_aux_idle(handle, LORA_TX_TIMEOUT_MS))
        return ESP_ERR_TIMEOUT;

    return ESP_OK;
}

int8_t lora_last_rssi(lora_ctx_t *handle)
{
    if (!handle) return 0;
    return handle->last_rssi;
}