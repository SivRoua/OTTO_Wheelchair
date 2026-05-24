# GPS 模块说明

## 一、模块基本特性

| 项目 | 值 |
| --- | --- |
| 接口 | UART（TTL 电平） |
| 默认串口 | 9600 bps / 8 数据位 / 无校验 / 1 停止位 |
| 输出协议 | NMEA 0183 |
| 解析语句 | 仅 $GNGGA / $GPGGA |
| 定位状态判断 | fix_quality > 0 且 satellites > 0 |

---

## 二、NMEA 语句说明

GPS 模块每秒输出一帧，包含多条语句。本驱动只处理第一行 `$GNGGA`，其余全部丢弃。

### $GNGGA 字段说明

```
$GNGGA,HHMMSS.ss,LLLL.LL,a,YYYYY.YY,a,q,nn,x.x,x.x,M,x.x,M,,*hh
         1         2       3  4        5  6  7   8    9
```

| 字段 | 含义 |
| --- | --- |
| 1 | UTC 时间（HHMMSS.ss） |
| 2 | 纬度（DDMM.MMMM） |
| 3 | 纬度方向（N/S） |
| 4 | 经度（DDDMM.MMMM） |
| 5 | 经度方向（E/W） |
| 6 | 定位质量（0=无效, 1=GPS, 2=DGPS） |
| 7 | 使用卫星数 |
| 8 | HDOP |
| 9 | 海拔（米） |

### 定位成功示例

```
$GNGGA,111827.000,3345.04155,N,11312.12009,E,1,08,7.2,144.1,M,-20.4,M,,*66
```

fix_quality=1，satellites=8，定位有效。

### 定位失败示例

```
$GNGGA,110838.000,,,,,0,00,25.5,,,,,,*79
```

fix_quality=0，satellites=0，定位无效。驱动不更新缓存，保留上次成功定位的数据。

---

## 三、目录结构

```
components/gps/
├── CMakeLists.txt
├── README.md
├── Kconfig
├── include/
│   └── gps.h          ← 公开 API
├── gps.c              ← 驱动核心实现
└── port/
    ├── gps_port.h
    └── esp_idf_gpio.c
```

---

## 四、API 说明

### 初始化

```c
uart_bus_config_t uart_cfg = {
    .uart_num  = UART_BUS_NUM_1,
    .txd_pin   = 17,
    .rxd_pin   = 18,
    .baud_rate = 9600,
};
uart_bus_handle_t bus = uart_bus_init(&uart_cfg);

gps_config_t gps_cfg = { .uart = uart_cfg };
gps_ctx_t *gps = gps_create(&gps_cfg, bus);
```

### 函数一览

| 函数 | 说明 | 返回值 |
| --- | --- | --- |
| `gps_create(cfg, bus)` | 创建实例，flush 串口缓冲区 | `gps_ctx_t*`，失败返回 NULL |
| `gps_deinit(handle)` | 销毁实例，释放内存 | void |
| `gps_read(handle, out)` | 非阻塞读取并解析一帧 GGA | `ESP_OK` / `ESP_ERR_NOT_FOUND` |
| `gps_read_blocking(handle, out, timeout_ms)` | 阻塞等待有效定位帧，0=永久等待 | `ESP_OK` / `ESP_ERR_TIMEOUT` |
| `gps_is_located(data)` | 判断当前是否已定位 | bool |
| `gps_get_gga_sentence(handle)` | 返回最近一次成功定位的原始 $GNGGA 字符串 | `const char*`，未定位返回 NULL |

### gps_data_t 结构体

```c
typedef struct {
    uint8_t  hour;        // UTC 时
    uint8_t  minute;      // UTC 分
    uint8_t  second;      // UTC 秒
    double   latitude;    // 纬度（十进制度，南纬为负）
    char     lat_dir;     // 'N' 或 'S'
    double   longitude;   // 经度（十进制度，西经为负）
    char     lon_dir;     // 'E' 或 'W'
    uint8_t  fix_quality; // 0=无效, 1=GPS, 2=DGPS
    uint8_t  satellites;  // 使用卫星数
    float    altitude;    // 海拔（米）
} gps_data_t;
```

---

## 五、典型用法

### 阻塞等待定位后发送 GGA 给 LoRa

```c
gps_data_t data;
if (gps_read_blocking(gps, &data, 5000) == ESP_OK) {
    const char *sentence = gps_get_gga_sentence(gps);
    if (sentence) {
        lora_send(lora, (const uint8_t *)sentence, strlen(sentence));
    }
}
```

### 轮询读取

```c
gps_data_t data;
while (1) {
    if (gps_read(gps, &data) == ESP_OK) {
        if (gps_is_located(&data)) {
            printf("lat=%.6f lon=%.6f alt=%.1f sats=%d\n",
                   data.latitude, data.longitude,
                   data.altitude, data.satellites);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

---

## 六、实现说明

### 数据缓存策略

`gps_get_gga_sentence` 返回的字符串指向 ctx 内部缓冲区，只在定位有效时更新。定位失败帧不覆盖缓存，保证 LoRa 始终发送最后一次有效坐标。

### 行拼装逻辑

- 逐字节读取串口，遇到 `$` 重置行缓冲区
- 遇到 `\n` 触发解析
- 非 `$GNGGA` / `$GPGGA` 开头的行在第一步就被丢弃，不做 checksum 计算

### NMEA Checksum

`$` 和 `*` 之间所有字符的异或值，与 `*` 后两位十六进制比对。校验失败的帧直接丢弃。
