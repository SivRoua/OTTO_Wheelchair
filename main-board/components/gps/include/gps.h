#ifndef GPS_H
#define GPS_H

/*
 * GPS 串口交互模块公开 API
 * ================================================================
 * 负责通过 UART 读取 GPS 模块输出的 NMEA 语句，
 * 解析出经纬度、时间、定位状态等基本字段。
 *
 * 依赖：uart_bus
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
    uart_bus_config_t uart;
} gps_config_t;

/* ---- 2. 解析结果（仅来自 $GNGGA） ---- */
typedef struct {
    /* 时间（UTC） */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;

    /* 纬度 */
    double   latitude;      /* 单位：度（十进制），南纬为负 */
    char     lat_dir;       /* 'N' 或 'S' */

    /* 经度 */
    double   longitude;     /* 单位：度（十进制），西经为负 */
    char     lon_dir;       /* 'E' 或 'W' */

    /* 定位质量：0=无效, 1=GPS, 2=DGPS */
    uint8_t  fix_quality;
    uint8_t  satellites;    /* 使用卫星数 */
    float    altitude;      /* 海拔（米） */
} gps_data_t;

/* ---- 3. 不透明句柄 ---- */
typedef struct gps_ctx gps_ctx_t;

/* ---- 初始化 / 销毁 ---- */
gps_ctx_t* gps_create(const gps_config_t *cfg, uart_bus_handle_t bus);
void       gps_deinit(gps_ctx_t *handle);

/* ---- 数据读取 ---- */

/*
 * 从串口读取并解析一帧 NMEA 语句。
 * 非阻塞：如果当前没有完整语句，返回 ESP_ERR_NOT_FOUND。
 * 成功时将解析结果写入 out，返回 ESP_OK。
 */
esp_err_t gps_read(gps_ctx_t *handle, gps_data_t *out);

/*
 * 阻塞等待一帧有效定位数据，超时返回 ESP_ERR_TIMEOUT。
 * timeout_ms = 0 表示永久等待。
 */
esp_err_t gps_read_blocking(gps_ctx_t *handle, gps_data_t *out,
                             uint32_t timeout_ms);

/* fix_quality > 0 且卫星数 > 0 即视为已定位 */
bool gps_is_located(const gps_data_t *data);

/*
 * 返回最近一次成功解析的原始 $GNGGA 语句字符串。
 * 返回值指向 ctx 内部缓冲区，调用方只读，无需释放。
 * 若尚未收到过有效 GGA 帧，返回 NULL。
 */
const char* gps_get_gga_sentence(const gps_ctx_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */
