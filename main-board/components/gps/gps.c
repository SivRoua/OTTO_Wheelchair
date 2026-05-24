/*
 * GPS 串口交互模块核心实现
 * ================================================================
 * 只解析 $GNGGA / $GPGGA，其余语句全部丢弃。
 *
 * 读取策略：
 *   - 逐字节拼装行缓冲区，以 '\n' 为帧结束标志
 *   - 遇到非 GGA 开头的行直接跳过，不做任何解析
 *   - 校验 NMEA checksum（'*' 后两位十六进制）
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps.h"

#define GPS_LINE_BUF_SIZE   128
#define GPS_RX_CHUNK        32
#define GPS_POLL_MS         5

struct gps_ctx {
    uart_bus_handle_t   bus;
    gps_config_t        config;
    char                line_buf[GPS_LINE_BUF_SIZE];
    int                 line_pos;
    gps_data_t          cache;
    bool                cache_valid;
    char                gga_sentence[GPS_LINE_BUF_SIZE]; /* 最近一次原始 GGA 行 */
};

/* ================================================================
 * NMEA checksum 校验
 * '$' 和 '*' 之间所有字符的异或值，与 '*' 后两位十六进制比对
 * ================================================================ */
static bool nmea_checksum_ok(const char *sentence)
{
    const char *p = sentence;
    if (*p != '$') return false;
    p++;

    uint8_t calc = 0;
    while (*p && *p != '*') {
        calc ^= (uint8_t)*p;
        p++;
    }
    if (*p != '*') return false;

    unsigned int recv;
    if (sscanf(p + 1, "%2X", &recv) != 1) return false;
    return calc == (uint8_t)recv;
}

/* ================================================================
 * 度分格式转十进制度
 * NMEA: DDDMM.MMMM → 度 + 分/60
 * ================================================================ */
static double dm_to_decimal(double dm)
{
    int deg = (int)(dm / 100);
    double min = dm - deg * 100.0;
    return deg + min / 60.0;
}

/* ================================================================
 * 解析 $GNGGA / $GPGGA
 * 字段（逗号分隔，0 为语句类型）：
 *   1  UTC 时间   2  纬度   3  纬度方向
 *   4  经度       5  经度方向
 *   6  定位质量   7  卫星数   9  海拔
 * ================================================================ */
static bool parse_gga(const char *sentence, gps_data_t *out)
{
    char buf[GPS_LINE_BUF_SIZE];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *fields[16];
    int n = 0;
    char *tok = strtok(buf, ",");
    while (tok && n < 16) {
        fields[n++] = tok;
        tok = strtok(NULL, ",*");
    }
    if (n < 10) return false;

    double hhmmss = atof(fields[1]);
    int hms = (int)hhmmss;
    out->hour   = (uint8_t)(hms / 10000);
    out->minute = (uint8_t)((hms / 100) % 100);
    out->second = (uint8_t)(hms % 100);

    if (strlen(fields[2]) > 0) {
        out->latitude = dm_to_decimal(atof(fields[2]));
        out->lat_dir  = fields[3][0];
        if (out->lat_dir == 'S') out->latitude = -out->latitude;
    }

    if (strlen(fields[4]) > 0) {
        out->longitude = dm_to_decimal(atof(fields[4]));
        out->lon_dir   = fields[5][0];
        if (out->lon_dir == 'W') out->longitude = -out->longitude;
    }

    out->fix_quality = (uint8_t)atoi(fields[6]);
    out->satellites  = (uint8_t)atoi(fields[7]);
    out->altitude    = (float)atof(fields[9]);

    return true;
}

/* ================================================================
 * 尝试解析行缓冲区中的一条语句
 * 非 GGA 开头直接返回 false，不做任何处理
 * ================================================================ */
static bool try_parse_line(gps_ctx_t *ctx, const char *line)
{
    if (strncmp(line, "$GNGGA", 6) != 0 &&
        strncmp(line, "$GPGGA", 6) != 0) {
        return false;
    }

    if (!nmea_checksum_ok(line)) return false;

    gps_data_t tmp = ctx->cache;
    if (!parse_gga(line, &tmp)) return false;

    /* 只在定位有效时更新缓存和原始语句，保留上次成功定位的数据 */
    if (gps_is_located(&tmp)) {
        ctx->cache = tmp;
        ctx->cache_valid = true;
        strncpy(ctx->gga_sentence, line, GPS_LINE_BUF_SIZE - 1);
        ctx->gga_sentence[GPS_LINE_BUF_SIZE - 1] = '\0';
    }
    return true;
}

/* ================================================================
 * 从串口读字节，拼装行缓冲区
 * 返回 true 表示拼出了一条完整行（以 '\n' 结尾）
 * ================================================================ */
static bool feed_uart(gps_ctx_t *ctx)
{
    uint8_t chunk[GPS_RX_CHUNK];
    int n = uart_bus_read(ctx->bus, chunk, sizeof(chunk));
    if (n <= 0) return false;

    bool got_line = false;
    for (int i = 0; i < n; i++) {
        char c = (char)chunk[i];

        if (c == '$') {
            ctx->line_pos = 0;
        }

        if (ctx->line_pos < GPS_LINE_BUF_SIZE - 1) {
            ctx->line_buf[ctx->line_pos++] = c;
            ctx->line_buf[ctx->line_pos]   = '\0';
        }

        if (c == '\n') {
            got_line = true;
        }
    }
    return got_line;
}

/* ================================================================
 * 初始化 / 销毁
 * ================================================================ */

gps_ctx_t* gps_create(const gps_config_t *cfg, uart_bus_handle_t bus)
{
    if (!cfg) return NULL;
    gps_ctx_t *ctx = (gps_ctx_t *)calloc(1, sizeof(gps_ctx_t));
    if (!ctx) return NULL;
    ctx->bus         = bus;
    ctx->config      = *cfg;
    ctx->line_pos    = 0;
    ctx->cache_valid = false;
    uart_bus_flush(bus);
    return ctx;
}

void gps_deinit(gps_ctx_t *handle)
{
    if (!handle) return;
    free(handle);
}

/* ================================================================
 * 数据读取
 * ================================================================ */

esp_err_t gps_read(gps_ctx_t *handle, gps_data_t *out)
{
    if (!handle || !out) return ESP_ERR_INVALID_ARG;

    if (!feed_uart(handle))          return ESP_ERR_NOT_FOUND;
    if (!try_parse_line(handle, handle->line_buf)) return ESP_ERR_NOT_FOUND;

    *out = handle->cache;
    return ESP_OK;
}

esp_err_t gps_read_blocking(gps_ctx_t *handle, gps_data_t *out,
                             uint32_t timeout_ms)
{
    if (!handle || !out) return ESP_ERR_INVALID_ARG;

    uint32_t elapsed = 0;
    while (timeout_ms == 0 || elapsed < timeout_ms) {
        if (gps_read(handle, out) == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(GPS_POLL_MS));
        elapsed += GPS_POLL_MS;
    }
    return ESP_ERR_TIMEOUT;
}

/* ================================================================
 * gps_is_located
 * fix_quality > 0 表示 GGA 报告有效定位（1=GPS, 2=DGPS 等）。
 * satellites > 0 作为额外保护，防止 fix_quality 非零但卫星数为 0 的异常帧。
 * ================================================================ */
bool gps_is_located(const gps_data_t *data)
{
    if (!data) return false;
    return data->fix_quality > 0 && data->satellites > 0;
}

const char* gps_get_gga_sentence(const gps_ctx_t *handle)
{
    if (!handle || !handle->cache_valid) return NULL;
    return handle->gga_sentence;
}
