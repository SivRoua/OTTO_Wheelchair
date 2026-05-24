# LoRa 模块（LLCC68 透传）说明书

## 一、模块基本特性

| 项目         | 值                               |
| ---------- | ------------------------------- |
| 芯片         | LLCC68                          |
| 接口         | UART（TTL 电平）                    |
| 引脚         | VCC, GND, TXD, RXD, M0, M1, AUX |
| 默认串口       | 9600 bps / 8 数据位 / 无校验 / 1 停止位  |
| 默认速率等级     | LEVEL 2（约 2148 bps 空中速率）        |
| 默认信道       | 00（433.15 MHz）                  |
| 默认设备地址     | ffff（两个字节）                      |
| 默认密钥       | 12345                           |
| 默认传输模式     | 透明传输（MODE0）                     |
| 默认工作模式     | 高时效模式（SLEEP2）                   |
| 默认发射功率     | 22 dBm                          |
| 默认分包长度     | 230 字节（PACKET3）                 |
| 默认 RSSI 上报 | 关闭（DRSSI0）                      |

---

## 二、引脚功能

### 2.1 M0 / M1 —— 工作模式选择

当 `AT+SWITCH=1` 时，M0/M1 引脚组合控制模式：

| M0  | M1  | 工作模式   | 说明             |
| --- | --- | ------ | -------------- |
| 0   | 0   | 高时效模式  | 一直接收，串口有数据即发   |
| 1   | 0   | 空中唤醒模式 | 周期性 CAD 检测，低功耗 |
| 0   | 1   | AT 模式  | 可接收 AT 指令      |
| 1   | 1   | 休眠模式   | 射频和 MCU 均休眠    |

当 `AT+SWITCH=0`（默认值），工作模式由 `AT+SLEEP` 指令控制，M0/M1 无效。

### 2.2 AUX —— 模块状态指示（输出）

| AUX 电平 | 含义                       |
| ------ | ------------------------ |
| 高      | 数据发送中 / 数据接收中 / 工作模式切换中  |
| 低      | 数据发送完成 / 数据接收完成 / 模式切换完成 |

> **注意：** 模块检测到接收数据时，AUX 会提前 2-3ms 输出高电平，提示 MCU 准备接收。

---

## 三、工作模式（`AT+SLEEP`）

| 值   | 模式     | 说明                                         |
| --- | ------ | ------------------------------------------ |
| 0   | 休眠模式   | MCU + 射频休眠。唤醒方式：串口发 `AT+WAKEUP` 或 M0/M1 切换 |
| 1   | 空中唤醒模式 | 周期 CAD 检测（默认 4s 周期），低功耗。收发双方必须都处于此模式       |
| 2   | 高时效模式  | 始终接收，实时响应，功耗较高（默认）                         |

> 设置后需 `AT+RESET` 重启生效。

---

## 四、AT 命令详解（驱动开发必需）

### 4.1 命令格式

- **进入/退出 AT 命令模式：** 发送 `+++\r\n`（带换行）
- 上电默认处于**传输模式**
- 成功进入返回：`Entry AT`
- 退出命令模式：再次发送 `+++\r\n`，返回 `Exit AT` 并自动复位
- AT 命令结构：`AT+CMD<CR><LF>`
- 响应格式：成功返回 `OK`，失败返回 `ERROR=<code>`
- 查询指令：`AT+CMD?` 返回 `+CMD=<value>`
- 设置指令：`AT+CMD=<value>`

> **注意：** 部分模块的数据手册写的是「`+++` 无换行」，但实测本模块需要带 `\r\n` 才能被识别为 AT 模式切换指令。如果不带换行，`+++` 会被当作普通数据无线发射出去。

### 4.2 常用 AT 命令表

| 命令           | 功能            | 参数说明                                        | 默认值      | 生效方式 |
| ------------ | ------------- | ------------------------------------------- | -------- | ---- |
| `+++`        | 进入/退出 AT 模式   | `\r\n` 必带                                   | –        | 立即   |
| `AT`         | 测试指令          | 无                                           | –        | 立即   |
| `AT+RESET`   | 软件重启          | 无                                           | –        | 立即   |
| `AT+DEFAULT` | 恢复出厂设置        | 无                                           | –        | 重启后  |
| `AT+BAUD`    | 串口波特率         | 3=9600, 4=19200, 5=38400, 6=57600, 7=115200 | 3 (9600) | 重启后  |
| `AT+PARI`    | 串口校验位         | 0=无校验, 1=偶校验, 2=奇校验                         | 0 (无校验)  | 重启后  |
| `AT+LEVEL`   | 空中速率档位        | 0~7（见档位表）                                   | 2        | 重启后  |
| `AT+MODE`    | 传输模式          | 0=透明, 1=定点, 2=广播                            | 0 (透明)   | 重启后  |
| `AT+SLEEP`   | 工作模式          | 0=休眠, 1=空中唤醒, 2=高时效                         | 2        | 重启后  |
| `AT+SWITCH`  | 硬件引脚控制开关      | 0=关闭（用AT指令）, 1=打开（用M0/M1）                   | 0        | 重启后  |
| `AT+CHANNEL` | 工作信道（十六进制）    | 00~63（对应 433.15 ~ 532.15 MHz）               | 00       | 重启后  |
| `AT+MAC`     | 设备地址（十六进制）    | 两个字节，例如 `0a,01`                             | ff,ff    | 重启后  |
| `AT+OPENKEY` | 密钥开关          | 0=关闭, 1=开启                                  | 1 (开启)   | 重启后  |
| `AT+KEY`     | 设置密钥（不可查询）    | 0~65535                                     | 12345    | 重启后  |
| `AT+PACKET`  | 分包长度          | 0=32B, 1=64B, 2=128B, 3=230B                | 3 (230B) | 重启后  |
| `AT+DRSSI`   | 数据包 RSSI 上报   | 0=关闭, 1=开启（数据末尾追加 1 字节 RSSI）                | 0        | 重启后  |
| `AT+POWE`    | 发射功率（dBm）     | 0~22                                        | 22       | 重启后  |
| `AT+LBT`     | LBT 状态        | 0=关闭, 1=打开                                  | 0        | 重启后  |
| `AT+LRSI`    | LBT 监听阈值（dBm） | -255~0                                      | -100     | 重启后  |
| `AT+ERSSI`   | 查询当前信道噪声（dBm） | 只读                                          | –        | 立即   |
| `AT+HELP`    | 查询完整配置信息      | 返回所有参数                                      | –        | 立即   |

### 4.3 速率档位表（`AT+LEVEL`）

| LEVEL | SF  | BW (kHz) | CR  | 空中速率 (bit/s) | 参考距离 (km) |
| ----- | --- | -------- | --- | ------------ | --------- |
| 0     | 11  | 125      | 4/8 | 336          | 8.0       |
| 1     | 11  | 250      | 4/5 | 1075         | –         |
| 2     | 11  | 500      | 4/5 | 2148（默认）     | –         |
| 3     | 8   | 250      | 4/6 | 6250         | –         |
| 4     | 8   | 500      | 4/6 | 10417        | –         |
| 5     | 7   | 500      | 4/6 | 18229        | –         |
| 6     | 7   | 500      | 4/5 | 37500        | –         |
| 7     | 7   | 500      | 4/5 | 62500        | –         |

> 收发双方 LEVEL 必须相同。

---

## 五、数据传输模式

### 5.1 透明传输（MODE0）

串口收到什么，无线就发什么。接收端直接输出原始数据。数据格式无额外头部。

### 5.2 定点传输（MODE1）

数据格式：`[目标地址 2 字节] + [目标信道 1 字节] + [数据]`

例：目标地址 `0x0001`，目标信道 `0x01`，数据 `aabbcc`
→ 发送 HEX：`00 01 01 61 62 63 63`

### 5.3 广播传输（MODE2）

数据格式：`[目标信道 1 字节] + [数据]`

例：目标信道 `0x01`，数据 `aabbcc`
→ 发送 HEX：`01 61 62 63 63`

> 定点/广播模式需收发双方 LEVEL 相同，且数据必须以 HEX 格式发送。

---

## 六、模块状态与操作时序

### 6.1 模式切换要点

- 通过 M0/M1 切换模式（`AT+SWITCH=1`）时，必须等待 AUX 从高变低。
- 从其他模式切换到休眠模式时，模块会处理完未完成的数据后才进入休眠。
- 从休眠唤醒时，需等待 AUX 变低后再操作。

### 6.2 收发注意事项

- LoRa 是半双工，同一时刻只能一方发送。
- 低空中速率下，大数据量传输可能堆积丢失，建议分包发送。
- 模块串口接收缓冲区有限，发送前建议检测 AUX 电平（低电平表示空闲可发）。

---

## 七、实现原理（驱动架构说明）

### 7.1 目录结构

```
components/LoRa/
├── CMakeLists.txt           ← 构建注册，REQUIRES uart_bus + esp_driver_gpio
├── Kconfig                  ← 引脚和参数菜单配置
├── README.md                ← 本文件
├── include/
│   └── LoRa.h               ← 公开 API 头文件
├── LoRa.c                   ← 驱动核心实现
├── LoRa_internal.h          ← 内部宏定义（AT 指令常量、超时参数）
└── port/
    ├── lora_port.h          ← 端口抽象层（AUX + 延时）
    └── esp_idf_gpio.c       ← ESP-IDF GPIO 端口实现
```

### 7.2 架构分层

驱动分为三层，与 lcd12864 模块的架构对称：

**1. 公开 API 层（`include/LoRa.h`）**

对外暴露 16 个函数，按功能分 6 个 Phase：

| Phase   | 功能          | API                                                                                                             |
| ------- | ----------- | --------------------------------------------------------------------------------------------------------------- |
| Phase 1 | 核心骨架        | `lora_create`, `lora_deinit`, `lora_is_idle`                                                                    |
| Phase 2 | AT 指令交互     | `lora_ping`                                                                                                     |
| Phase 3 | 系统查询        | `lora_get_config`, `lora_get_noise`                                                                             |
| Phase 4 | 参数配置        | `lora_apply_config`, `lora_set_power`, `lora_set_channel`, `lora_set_level`, `lora_reset`, `lora_factory_reset` |
| Phase 5 | 数据透明收发      | `lora_send`, `lora_recv`                                                                                        |
| Phase 6 | 定点传输 + RSSI | `lora_send_to`, `lora_last_rssi`                                                                                |

**2. 端口抽象层（`port/lora_port.h` + `esp_idf_gpio.c`）**

只包含三个函数指针：

| 函数                | 功能                        |
| ----------------- | ------------------------- |
| `init(aux_pin)`   | 初始化 AUX 引脚为 GPIO 输入       |
| `gpio_read_aux()` | 读取 AUX 电平，true=忙，false=空闲 |
| `delay_ms(ms)`    | 毫秒级阻塞延时                   |

对比 lcd12864 的端口层（SPI 读写 + GPIO 控制），LoRa 的端口层非常薄——UART 的收发全部委托给公共的 `uart_bus` 模块。

**3. 内部宏定义层（`LoRa_internal.h`）**

存放 AT 指令字符串常量（如 `"+++\r\n"`、`"AT+LEVEL="`）和超时参数、缓冲区大小等编译期常量。集中在一个头文件里便于修改和维护。

### 7.3 对外 API 一览

公开头文件 `include/LoRa.h` 对外暴露 16 个函数，按功能分组如下：

**Phase 1 — 核心骨架**

| 函数                      | 功能                       | 参数                                                             | 返回值                     |
| ----------------------- | ------------------------ | -------------------------------------------------------------- | ----------------------- |
| `lora_create(cfg, bus)` | 创建模块实例，初始化 AUX 引脚，等待模块就绪 | `lora_config_t *cfg`：AUX引脚、UART配置；`uart_bus_handle_t bus`：串口句柄 | `lora_ctx_t*`，失败返回 NULL |
| `lora_deinit(handle)`   | 销毁实例，释放上下文内存             | `lora_ctx_t *handle`                                           | void                    |
| `lora_is_idle(handle)`  | 查询模块是否空闲（AUX 低电平）        | `lora_ctx_t *handle`                                           | bool：true=空闲，false=忙    |

**Phase 2 — AT 指令交互**

| 函数                  | 功能             | 参数                   | 返回值                                       |
| ------------------- | -------------- | -------------------- | ----------------------------------------- |
| `lora_ping(handle)` | AT 测试指令，验证通信链路 | `lora_ctx_t *handle` | `ESP_OK` / `ESP_FAIL` / `ESP_ERR_TIMEOUT` |

**Phase 3 — 系统查询**

| 函数                                | 功能            | 参数                                  | 返回值             |
| --------------------------------- | ------------- | ----------------------------------- | --------------- |
| `lora_get_config(handle, params)` | 读取模块全部射频参数    | `lora_config_params_t *params`：输出参数 | `ESP_OK` 或错误码   |
| `lora_get_noise(handle)`          | 查询当前信道噪声(dBm) | `lora_ctx_t *handle`                | int：噪声值（0 表示失败） |

**Phase 4 — 参数配置**

| 函数                                  | 功能              | 参数                                    | 返回值           |
| ----------------------------------- | --------------- | ------------------------------------- | ------------- |
| `lora_apply_config(handle, params)` | 批量设置射频参数 + 自动重启 | `lora_config_params_t *params`        | `ESP_OK` 或错误码 |
| `lora_set_power(handle, dbm)`       | 设置发射功率 (0~22)   | `lora_ctx_t *handle`, `int8_t dbm`    | `ESP_OK` 或错误码 |
| `lora_set_channel(handle, ch)`      | 设置工作信道 (0~63)   | `lora_ctx_t *handle`, `uint8_t ch`    | `ESP_OK` 或错误码 |
| `lora_set_level(handle, level)`     | 设置速率档位 (0~7)    | `lora_ctx_t *handle`, `uint8_t level` | `ESP_OK` 或错误码 |
| `lora_reset(handle)`                | 软件重启模块          | `lora_ctx_t *handle`                  | `ESP_OK` 或错误码 |
| `lora_factory_reset(handle)`        | 恢复出厂设置并重启       | `lora_ctx_t *handle`                  | `ESP_OK` 或错误码 |

**Phase 5 — 数据透明收发**

| 函数                                | 功能         | 参数                                         | 返回值                |
| --------------------------------- | ---------- | ------------------------------------------ | ------------------ |
| `lora_send(handle, data, len)`    | 发送数据（透明传输） | `const uint8_t *data`, `uint8_t len`（≤230） | `ESP_OK` 或错误码      |
| `lora_recv(handle, buf, max_len)` | 接收数据（非阻塞）  | `uint8_t *buf`, `uint8_t max_len`          | int：实际读取的字节数，0=无数据 |

**Phase 6 — 定点传输 + RSSI**

| 函数                                          | 功能             | 参数                                     | 返回值                  |
| ------------------------------------------- | -------------- | -------------------------------------- | -------------------- |
| `lora_send_to(handle, addr, ch, data, len)` | 定点发送（MODE1 格式） | `uint16_t addr`：目标地址；`uint8_t ch`：目标信道 | `ESP_OK` 或错误码        |
| `lora_last_rssi(handle)`                    | 获取最近一次接收的 RSSI | `lora_ctx_t *handle`                   | `int8_t`：RSSI 值(dBm) |

**配置结构体：**

```c
// lora_config_t — 创建模块时的基础配置
typedef struct {
    int aux_pin;                // AUX 引脚号
    uart_bus_config_t uart;     // UART 配置（引脚、波特率）
    bool drssi;                 // 是否启用数据末尾 RSSI 上报
} lora_config_t;

// lora_config_params_t — 射频参数（用于 get/set）
typedef struct {
    uint8_t  level;             // 0~7 空中速率档位
    uint8_t  channel;           // 0~63 工作信道
    uint8_t  power;             // 0~22 dBm
    uint8_t  mode;              // 0=透明, 1=定点, 2=广播
    uint8_t  sleep_mode;        // 0=休眠, 1=唤醒, 2=高时效
    uint8_t  packet;            // 0=32B, 1=64B, 2=128B, 3=230B
    bool     drssi;             // RSSI 上报
    uint16_t addr;              // 设备地址
} lora_config_params_t;
```

所有 API 的函数签名、参数含义、返回值类型均以 `include/LoRa.h` 为准，上表为快速参考。

---

### 7.4 模块依赖关系

```
main
  ├── lcd12864    ← SPI LCD，独立
  ├── uart_bus    ← 公共 UART 抽象层
  └── LoRa        ← 依赖 uart_bus（数据收发）+ esp_driver_gpio（AUX）
```

LoRa 不直接操作 UART 寄存器，所有串口读写通过 `uart_bus_handle_t` 句柄完成。句柄由外围传入，模块只管收发，不负责 UART 的安装和配置。

### 7.5 AT 引擎工作原理

```
进入 AT 模式:
  uart_bus_flush()           ← 丢弃残留数据
  uart_bus_write("+++\r\n") ← 发送 AT 切换指令
  delay_ms(500)              ← 等模块处理
  read_at_response()         ← 读取 "Entry AT"
  strstr(resp, "Entry AT")   ← 检查是否进入成功

发送 AT 指令:
  uart_bus_flush()
  uart_bus_write("AT+XXX\r\n")
  delay_ms(100)
  read_at_response()         ← 读取 "OK" 或 "ERROR"
  strstr(resp, "OK")         ← 判断执行结果

退出 AT 模式:
  uart_bus_write("+++\r\n")
  delay_ms(200)
  wait_aux_idle()            ← 等待模块复位完成
  uart_bus_flush()           ← 丢弃复位后乱码
```

### 7.6 数据收发流程

**发送（透明模式）：**

```
wait_aux_idle()                 ← 等模块空闲（上次发送完成）
uart_bus_write(data, len)       ← 写入串口，模块自动无线转发
wait_aux_busy(100ms)            ← 等 AUX 变高（发送开始）
wait_aux_idle(5s)               ← 等 AUX 变低（发送完成）
```

**接收（透明模式）：**

```
if (gpio_read_aux())            ← AUX 高 = 有数据到达
    uart_bus_read(buf, max)     ← 读取串口数据
    if (AT+DRSSI==1)            ← 如果 RSSI 上报开启
        末尾字节缓存为 RSSI
```

### 7.7 关键的调试发现

在驱动开发过程中有一个重要的经验：**不要完全相信数据手册中的串口协议描述。** 本模块的手册声明 `+++` 不带换行，但实际测试发现必须带 `\r\n` 才能被识别为 AT 模式切换指令。如果不带换行，`+++` 会被当作普通数据无线发送出去——这个现象可以通过另一块模块空中接收来验证。

重要补充：read_at_response 的缺陷
这次解析失败还暴露了驱动中 read_at_response 的问题：它过早地因 AUX 变低而退出，导致只读到回复的片段。在 lora_get_config 中，你可以继续使用上面写死的“固定超时读取”方式，或者修改 read_at_response 的逻辑：改为仅在收到数据后且 AUX 变低才退出，同时增加一个“最大等待时间”防止卡死。但目前的临时方案已经可以工作。