# OTTO_Wheelchair

Omnidirectional Tracking Telemonitoring Orthosis — 全向追踪与远程监护矫形轮椅。

2026 年上半年传感器原理课设，从 PCB 到固件到服务端的全栈实践。

## 仓库结构

```
OTTO_Wheelchair/
├── hardware/
│   ├── PCB/          # KiCad 工程文件（待同步）
│   └── model/        # 3D 模型文件（待同步）
├── main-board/       # 主板固件，ESP32-S3（Tinzen 编写，待同步）
├── sub-board/        # 副板固件，STM32F103（Rust + stm32f1xx-hal）
└── server/           # 上位机服务，Rust + axum（hearth）
```

## 系统架构

```
轮椅端
  ESP32-S3 [主板]
    ├─ ATGM336H GPS → NMEA 解析
    └─ LoRa TX (DX-LR22-433T22D, 433MHz)

  STM32F103 [副板]
    └─ 电机驱动 (TB6612 直流 + ULN2003 步进)

接收端
  DX-LR22-433T22D LoRa 模块 + CH340 TTL 适配器
    └─ /dev/ttyUSB0 → Wyse 5070

服务端 (Wyse 5070)
  hearth (Rust)
    ├─ 串口读取 $GPRMC → 解析 → SQLite
    ├─ SSE 实时推送
    └─ HTTP :3000 → Leaflet 地图
```

## 快速开始

### 服务端

```bash
cd server
cargo build --release
RUST_LOG=info DB_PATH=hearth.db PORT=3000 ./target/release/hearth
```

### 副板固件

```bash
cd sub-board
cargo build --release
# 烧录需要 probe-rs 或 OpenOCD
```

### 模拟 GPS 数据（调试用）

```bash
# 通过 LoRa TTL 模块发送 $GPRMC，在固定点附近随机游走
python3 server/lora/send_gprmc.py
```
