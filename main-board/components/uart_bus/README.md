# uart_bus 模块设计说明

## 这个模块是做什么的

`uart_bus` 是一个轻量级的 UART 抽象层，或者说，是一套把 ESP32 的 UART 外设包装成"即插即用"手柄的工具。

在一个同时使用了 LoRa 透传模块和 GPS 接收模块的项目里，两个设备各占一个串口。如果没有统一层管理，每个模块都要各自调用 `uart_driver_install`、各自配置引脚、各自处理中断。`uart_bus` 把这一切公共的部分抽出来，让上层模块只需要说"给我一个串口"——初始化、发送、接收、清理全部走同一套代码。

## 为什么要加这一层

这个问题要从项目里串口设备的数量说起。

### 场景：这个项目有几个串口设备

```
ESP32
├── UART0 ── 控制台（可以用来看 log，不建议挪作他用）
├── UART1 ── LoRa 透传模块（LLCC68 封装，UART 通信）
└── UART2 ── GPS 接收模块（NMEA 输出，UART 通信）
```

如果没有 `uart_bus`，LoRa 模块和 GPS 模块各写各的 UART 代码，最终效果是这样的：

```
components/LoRa/port/esp_idf_uart.c   ← uart_driver_install(UART_NUM_1, ...)
components/gps/port/esp_idf_uart.c    ← uart_driver_install(UART_NUM_2, ...)  ← 和上面 80% 相同
```

两份 UART 初始化代码几乎一模一样，唯一的区别是 UART 编号和引脚号。如果将来换平台（比如换到 STM32），或者统一修改波特率，就得在两个文件里各改一遍。

加了 `uart_bus` 之后，代码变成：

```
components/uart_bus/uart_bus.c         ← uart_driver_install 只写一次
components/LoRa/LoRa.c                 ← 通过 uart_bus 句柄收发
components/gps/gps.c                   ← 通过 uart_bus 句柄收发（同一份代码，不同句柄）
```

**uart_bus 要解决的关键问题不是功能冲突——因为不同 UART 编号的硬件本来就是完全独立的——而是代码重复。** 三个串口设备每个都写一遍 UART 初始化和中断处理，是没必要的。

### 那为什么不直接复用 ESP-IDF 的 UART API？

ESP-IDF 的 UART API 已经提供了 `uart_write_bytes` 和 `uart_read_bytes` 这样的高层接口。直接在每个模块里调用它们，技术上完全可行。

问题出在**初始化和配置的复杂度**上。调用 `uart_driver_install` 需要填一个 UART 配置结构体（波特率、数据位、停止位、校验、流控），注册中断处理函数，分配发送和接收缓冲区。这段代码在每个串口模块里重复出现时，差异只有引脚号和 UART 编号这几个数值。`uart_bus` 把这段重复的配置流程收归一处，上层模块只需要决定"我要用哪个串口"和"引脚接在哪"。

另外还有一个重要的考虑：**UART 中断服务只能安装一次**。如果 LoRa 和 GPS 各自注册中断处理函数，可能会互相覆盖。`uart_bus` 把中断管理集中起来，避免这种竞争——而且目前的设计甚至不依赖用户注册的中断处理，完全使用 ESP-IDF 内置的驱动框架。

---

## 模块长什么样

```
components/uart_bus/
├── CMakeLists.txt           # 构建注册，依赖 esp_driver_uart
├── include/
│   └── uart_bus.h           # 公开头文件：函数声明和类型定义
└── uart_bus.c               # 实现文件：初始化、读写、注销
```

结构极其简单，就三个文件。为什么这么薄？因为 uart_bus 只负责"发字节"和"收字节"这两件事，不做协议解析，不做缓冲区管理以外的任何业务逻辑。具体的协议解析——比如 LoRa 模块的配置命令帧、GPS 的 NMEA 句子解析——由上层模块自己完成。

---

## 对外接口

### 类型定义

```c
typedef int uart_bus_handle_t;
```

句柄直接使用 ESP-IDL 的 UART 编号（`UART_NUM_1`、`UART_NUM_2` 等）。这样设计有几个考虑：

- 不需要额外的内存分配来维护句柄表
- 底层 UART API 原本就用编号区分设备，句柄和编号合一省去一层映射
- 用户一眼能看明白句柄对应哪个物理串口

```c
typedef struct {
    int uart_num;          /* UART 编号 */
    int txd_pin;           /* TX 引脚 */
    int rxd_pin;           /* RX 引脚 */
    uint32_t baud_rate;    /* 波特率 */
} uart_bus_config_t;
```

配置结构体只包含四个字段。没有包含数据位、停止位、校验位——因为这些在串口透传模块中几乎总是 8N1（8 数据位、无校验、1 停止位）。如果后续项目中出现需要非 8N1 配置的设备，可以扩展这个结构体，但当前设计追求最小化。

### 函数

| 函数                                 | 功能               | 备注                                                                |
| ---------------------------------- | ---------------- | ----------------------------------------------------------------- |
| `uart_bus_init(cfg)`               | 初始化一个 UART 并返回句柄 | 内部完成 `uart_param_config` → `uart_set_pin` → `uart_driver_install` |
| `uart_bus_write(bus, data, len)`   | 发送数据             | 调用 `uart_write_bytes`，阻塞直到所有字节写入硬件 FIFO                           |
| `uart_bus_read(bus, buf, max_len)` | 非阻塞读取            | 调用 `uart_read_bytes` 加超时 0，返回值 = 实际读取的字节数                         |
| `uart_bus_flush(bus)`              | 清空 RX 缓冲区        | 调用 `uart_flush_input`                                             |
| `uart_bus_deinit(bus)`             | 注销 UART          | 调用 `uart_driver_delete`                                           |

**设计选择**：为什么不提供带中断的异步接收回调？

因为不同模块的接收行为差别太大了：

- LoRa 模块：数据是**突发性**的，每次收到完整的一包就交给上层处理，但对时序要求不高
- GPS 模块：数据是**持续性流**，每秒吐一串 NMEA 句子，需要逐字节收集后按行切割

如果 uart_bus 提供统一的中断回调接口，这个回调应该长成什么样会成为争议。所以 uart_bus 只提供**同步轮询读写**（write 阻塞、read 非阻塞），由上层自己决定接收策略——LoRa 可以在任务循环中轮询 `uart_bus_read`，GPS 可以启用自己的接收任务。这种设计让 uart_bus 保持通用性，不需要预判上层的接收模式。

---

## 实现原理（以 ESP-IDF 为例）

### 初始化过程

```c
uart_bus_handle_t uart_bus_init(const uart_bus_config_t *cfg) {
    /* 1. 配置 UART 参数（波特率、8N1、无流控） */
    uart_config_t uart_cfg = {
        .baud_rate  = cfg->baud_rate,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 2. 应用配置 + 绑定引脚 */
    uart_param_config(cfg->uart_num, &uart_cfg);
    uart_set_pin(cfg->uart_num, cfg->txd_pin, cfg->rxd_pin,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    /* 3. 安装驱动（分配 FIFO 缓冲区） */
    uart_driver_install(cfg->uart_num, RX_BUF_SIZE, TX_BUF_SIZE, 0, NULL, 0);

    return cfg->uart_num;
}
```

三步一气呵成。这里有个细节：发送缓冲区大小和接收缓冲区大小都在 `.c` 文件内以宏定义给出（比如 `RX_BUF_SIZE 1024`），不在配置结构体中暴露——因为对 LoRa 和 GPS 来说，1024 字节的缓冲区足够容纳一次最大数据量。如果将来有模块需要更大缓冲区，再改为可配置参数。

### 写操作

```c
void uart_bus_write(uart_bus_handle_t bus, const uint8_t *data, uint32_t len) {
    uart_write_bytes((uart_port_t)bus, data, len);
}
```

`uart_write_bytes` 是阻塞的，但在 ESP-IDF 中"阻塞"的意思是：数据被写入硬件 FIFO（而不是等待所有字节在电线上传输完毕）后立即返回。所以实际阻塞时间极短（微秒级），除非 UART 的 FIFO 满了。对于 LoRa 一次最多几十字节的载荷来说不会出现 FIFO 满的情况。

### 读操作

```c
int uart_bus_read(uart_bus_handle_t bus, uint8_t *buf, uint32_t max_len) {
    return uart_read_bytes((uart_port_t)bus, buf, max_len, 0);  // 0 超时 = 非阻塞
}
```

超时设为 0，立即返回。没有数据可读时返回 0。上层模块在自己的任务循环中定期调用 `uart_bus_read` 来获取接收到的数据。

### 缓冲区清空

```c
void uart_bus_flush(uart_bus_handle_t bus) {
    uart_flush_input((uart_port_t)bus);
}
```

清空内部 RX 缓冲区。在切换 LoRa 模块工作模式时特别重要——切换模式后，模块可能吐出之前残留的响应数据，需要丢弃以避免混淆。

---

## 这个模块不做什么

明确一下 uart_bus 的边界：

| 不做                | 原因                                   |
| ----------------- | ------------------------------------ |
| 不做协议解析            | LoRa 的配置命令帧、GPS 的 NMEA 解析由各模块自己完成    |
| 不管理多个任务对同一串口的并发访问 | 一个串口本来就不应该被两个任务同时驱动。如果需要由上层加锁        |
| 不做环形缓冲区之外的缓冲区管理   | ESP-IDF 的 UART 驱动内部已经维护了 FIFO 和环形缓冲区 |
| 不做 DMA / 中断接收处理   | 保持接口简单，上层可以选择轮询方式读取                  |
| 不提供 RX 超时或数据到达回调  | 不同模块需要的接收模式不同，uart_bus 不做假设          |

---

## 主要用例

### 在 LoRa 模块中

```c
/* main.c — 初始化 */
uart_bus_config_t lora_uart = {
    .uart_num  = UART_NUM_1,
    .txd_pin   = 17,
    .rxd_pin   = 16,
    .baud_rate = 9600,
};
lora_bus = uart_bus_init(&lora_uart);

lora_config_t lora_cfg = {
    .m0_pin = 13, .m1_pin = 14, .aux_pin = 15,
};
lora_ctx_t *lora = lora_create(&lora_cfg, lora_bus);  // 传入 uart_bus 句柄
```

LoRa 模块在收发数据时，内部调用 `uart_bus_write(lora_bus, data, len)` 和 `uart_bus_read(lora_bus, buf, max_len)`，不直接接触 ESP-IDF 的 UART API。

### 在 GPS 模块中

```c
/* main.c */
uart_bus_config_t gps_uart = {
    .uart_num  = UART_NUM_2,
    .txd_pin   = 4,
    .rxd_pin   = 5,
    .baud_rate = 9600,
};
gps_bus = uart_bus_init(&gps_uart);

gps_ctx_t *gps = gps_create(gps_bus);  // 传入 uart_bus 句柄
```

GPS 模块在自己的接收任务中循环调用 `uart_bus_read(gps_bus, buf, 256)` 收集 NMEA 原始数据，然后逐行解析经纬度。

---

## 与其他模块的兼容性

### 串口编号不能冲突

ESP32 有 3 个 UART 控制器：

| UART 编号    | 默认引脚（TX/RX） | 典型用途                         |
| ---------- | ----------- | ---------------------------- |
| UART_NUM_0 | GPIO 1 / 3  | 控制台 log 输出 + 固件下载，**建议留给系统** |
| UART_NUM_1 | 由用户自定       | LoRa 模块                      |
| UART_NUM_2 | 由用户自定       | GPS 模块                       |

LoRa 和 GPS 不能使用同一个 UART_NUM。使用同一个 UART_NUM 时，第二次调用 `uart_bus_init` 会因为 `uart_driver_install` 返回 `ESP_ERR_INVALID_STATE` 而失败。

### 与 lcd12864 模块的兼容性

lcd12864 使用 SPI（占用 SPI2 主机、5 个 GPIO），与 uart_bus 管理的串口**完全没有资源冲突**。两个模块的硬件资源完全独立。

### 与系统控制台（CONSOLE）的兼容性

UART0 默认被 `esp_console` 或 `esp_log` 占用，用于输出日志和接收命令。`uart_bus` 不应接管 UART0。如果项目需要外设使用 UART0，必须先在 menuconfig 中关闭 CONSOLE 重定向。

---

## 硬件资源占用

每个 uart_bus 实例占用：

| 资源              | 占用                                         |
| --------------- | ------------------------------------------ |
| **UART 控制器**    | 1 个（由 `uart_driver_install` 分配）            |
| **GPIO**        | 2 个（TXD + RXD），由上层模块指定                     |
| **堆内存（RX 缓冲区）** | 1024 字节（接收环形缓冲，由 `uart_driver_install` 分配） |
| **堆内存（TX 缓冲区）** | 0 字节（`tx_buf_size = 0`，数据直接写入硬件 FIFO）      |
| **中断**          | UART 中断由 ESP-IDF 驱动内部管理，不需要额外 GPIO 中断      |

---

## 在 RTOS（FreeRTOS）中的行为

`uart_bus_write` 调用的 `uart_write_bytes` 在内部会使用 FreeRTOS 队列管理发送。如果发送缓冲区满，它会阻塞当前任务。但对于小数据包（如 LoRa 每次 5~60 字节），基本不会遇到阻塞。

`uart_bus_read` 使用 0 超时，完全非阻塞。调用它的任务不会被挂起，可以安全地在高频率循环中轮询。

`uart_bus_init` 中 `uart_driver_install` 分配的内存来自 FreeRTOS 堆，**不是**任务栈。所以任务栈大小不会成为瓶颈。

`uart_bus_deinit` 调用 `uart_driver_delete`，会释放内部创建的队列和中断句柄。如果其他任务正在这个串口上等待，行为未定义——上层需要在 deinit 前确保目标串口已停止使用。

---

## 关于"不透明句柄"的说明

对比 lcd12864 使用 `void*` 作为不透明句柄，uart_bus 直接用 `int`。这不是疏忽，而是有意识的选择：

- `uart_bus_handle_t` 实际上就是 `UART_NUM_1`、`UART_NUM_2` 这些整数常量
- 没有必要封装一个指针类型，因为句柄不需要指向任何内部状态——状态全在 ESP-IDF 的 UART 驱动内部
- 整型句柄更轻量，可拷贝，可比较，可以出现在 switch-case 中

如果未来实现需要维护 per-bus 的状态（如统计信息、低功耗管理），再改为 `void*` 不迟。这个决定遵循"只在需要抽象的时候才抽象"的原则，不提前引入不必要的间接层。
