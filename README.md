# OTTO_Wheelchair

Omnidirectional Tracking Telemonitoring Orthosis — 全向追踪与远程监护矫形轮椅。

2026 年上半年传感器原理课设，从 PCB 到固件到服务端的全栈实践。

## 仓库结构

```
OTTO_Wheelchair/
├── hardware/
│   ├── PCB/          # 嘉立创EDA 工程文件
│   └── model/        # 3D 模型文件（待同步）
├── main-board/       # 主板固件，ESP32-S3（Tinzen 编写，待同步）
├── sub-board/        # 副板固件，STM32F103C8（Rust + stm32f1xx-hal）
└── server/           # 上位机服务 hearth（Rust + axum）
```

## 系统架构

```
轮椅端
  ESP32-S3 [主板，待同步]
    ├─ ATGM336H GPS → NMEA 解析
    └─ DX-LR22-433T22D LoRa TX (433MHz)

  STM32F103C8 [副板]
    ├─ I2C1 从机 (0x42) 接收主板指令
    ├─ TB6612  → 左右直流电机 (PB12-15)
    └─ ULN2003 → 步进电机 (PA0-3)

接收端 (Wyse 5070)
  DX-LR22-433T22D LoRa 模块 + CH340 TTL 适配器
    └─ /dev/ttyUSB0 → hearth

服务端 (Wyse 5070)
  hearth (Rust)
    ├─ 串口自动探测 ttyUSB*/ttyACM*
    ├─ $GPRMC/$GNRMC 解析 + 校验和验证
    ├─ Haversine 累计里程 + 速度计算
    ├─ SQLite 轨迹持久化
    ├─ SSE /events 实时推送
    ├─ GET /api/wheelchair  当前状态 JSON
    ├─ GET /api/trail       历史轨迹点
    └─ HTTP :3000 → Leaflet 地图前端
```

## 副板 (sub-board)

**芯片**：STM32F103C8（Blue Pill），`thumbv7m-none-eabi`，64K Flash / 20K RAM

**通信协议**：I2C1 从机，地址 `0x42`，接收 6 字节定长帧：

```
[S][T][flags][flags][E][D]
```

- `flags` 高 2 位 = 步进电机方向，次 2 位 = 左电机，再次 2 位 = 右电机
- 方向编码：`10` = 正转，`01` = 反转，其余 = 停止
- 校验：`'S' ^ 'T' ^ flags[0] ^ flags[1] == 0`

**引脚分配**：

| 引脚 | 功能 |
|------|------|
| PA0–PA3 | ULN2003 IN1–IN4（步进电机） |
| PB12–PB15 | TB6612 BIN2/BIN1/AIN2/AIN1（左右直流电机） |
| PB6/PB7 | I2C1 SCL/SDA（接主板） |
| PC13 | 板载 LED（启动闪烁 + 心跳） |

**烧录**：

```bash
cd sub-board
cargo build --release
# probe-rs 自动烧录（已配置 runner）
cargo run --release
```

## 服务端 (server)

**环境变量**：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SERIAL_PORT` | `/dev/ttyUSB0` | 串口回退路径（优先自动探测） |
| `BAUD` | `9600` | 波特率 |
| `PORT` | `3000` | HTTP 监听端口 |
| `DB_PATH` | `hearth.db` | SQLite 数据库路径 |
| `STATIC_DIR` | `/usr/local/share/hearth/static` | 前端静态文件目录 |
| `RUST_LOG` | — | 日志级别，如 `info` |

**本地运行**：

```bash
cd server
cargo build --release
RUST_LOG=info DB_PATH=hearth.db PORT=3000 STATIC_DIR=static ./target/release/hearth
```

**热更新部署到 Wyse 5070**（服务运行中无法直接 scp，用 ssh+cat 绕过文件锁）：

```bash
ssh root@<wyse-ip> 'killall hearth; sleep 1'
ssh root@<wyse-ip> 'cat > /tmp/h && mv /tmp/h /usr/local/bin/hearth && chmod +x /usr/local/bin/hearth' \
  < server/target/release/hearth
```

## 调试工具

```bash
# 通过 LoRa TTL 模块发送 $GPRMC，在固定点附近随机游走（模拟 GPS）
python3 server/lora/send_gprmc.py
```
