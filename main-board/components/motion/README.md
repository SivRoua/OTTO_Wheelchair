# 电机控制模块说明

## 一、通信协议

主板通过 I2C 向副板发送 6 字节定长帧控制三个电机。

| 项目 | 值 |
| --- | --- |
| 总线 | I2C0 |
| 电平 | 3.3V TTL |
| 频率 | 100 kHz |
| 从机地址 | `0x42` |
| 引脚 | SCL=47, SDA=38 |

---

## 二、帧格式

```
[S][T][flag][checksum][E][D]   （6 字节定长）
```

| 字节 | 含义 | 值 |
| --- | --- | --- |
| 0 | 帧头 | `'S'` (0x53) |
| 1 | 类型 | `'T'` (0x54) |
| 2 | 电机控制 | 见下文 flag 编码 |
| 3 | 校验和 | `'S' ^ 'T' ^ flag` |
| 4 | 帧尾 1 | `'E'` (0x45) |
| 5 | 帧尾 2 | `'D'` (0x44) |

副板验证：`frame[0]=='S' && frame[1]=='T' && frame[4]=='E' && frame[5]=='D' && S^T^flag^cs==0`，任一条件不满足则丢弃帧并全停电机。

### flag 字节编码

```
flag bit:  [7] [6]  [5] [4]  [3] [2]  [1] [0]
           步进电机   左轮     右轮     未用(0)
```

| 位模式 | 方向 |
| --- | --- |
| `10` | 正转 |
| `01` | 反转 |
| `00` | 停止 |
| `11` | 停止 |

### 实例

前进（左轮正转 + 右轮正转）：

```
flag = (00 << 6) | (10 << 4) | (10 << 2) = 0b01011000 = 0x58
checksum = 0x53 ^ 0x54 ^ 0x58 = 0x5F
帧: 53 54 58 5F 45 44
```

原地右转（左轮正转 + 右轮反转）：

```
flag = (00 << 6) | (10 << 4) | (01 << 2) = 0b01010100 = 0x54
checksum = 0x53 ^ 0x54 ^ 0x54 = 0x53
帧: 53 54 54 53 45 44
```

---

## 三、目录结构

```
components/motion/
├── CMakeLists.txt
├── Kconfig          ← I2C 引脚菜单配置
├── motion.c         ← 核心实现
├── include/
│   └── motion.h     ← 公开 API
└── README.md
```

---

## 四、API 说明

prepare → set → send 模式：先 `prepare` 清空缓冲区，再逐个设置电机方向，最后 `send` 构建帧并 I2C 发出。

### 函数一览

| 函数 | 说明 | 返回值 |
| --- | --- | --- |
| `motion_init(port, sda, scl)` | 初始化 I2C master，注册从机 0x42 | `esp_err_t` |
| `motion_prepare()` | 清空待发帧缓冲区（flag=0，全 Stop），不发 I2C | void |
| `motion_left(dir)` | 左轮方向 | void |
| `motion_right(dir)` | 右轮方向 | void |
| `motion_stepper(dir)` | 步进电机方向 | void |
| `motion_send()` | 构建 6 字节帧 → I2C 发送 | `esp_err_t` |

### 方向参数

```c
#define MOTION_F 'F'   // Forward
#define MOTION_B 'B'   // Backward
#define MOTION_S 'S'   // Stop
```

---

## 五、典型用法

```c
#include "motion.h"

void app_main(void)
{
    motion_init(0, 38, 47);

    // 前进 2 秒 → 停
    motion_prepare();
    motion_left(MOTION_F);
    motion_right(MOTION_F);
    motion_send();
    vTaskDelay(pdMS_TO_TICKS(2000));
    motion_prepare();
    motion_send();

    // 原地左转（左轮反转 + 右轮正转）
    motion_prepare();
    motion_left(MOTION_B);
    motion_right(MOTION_F);
    motion_send();

    // 步进电机正转 + 左右全速前进
    motion_prepare();
    motion_stepper(MOTION_F);
    motion_left(MOTION_F);
    motion_right(MOTION_F);
    motion_send();
}
```

---

## 六、副板电机对应

| 电机 | 驱动芯片 | 副板引脚 | flag 位 |
| --- | --- | --- | --- |
| 步进电机 | ULN2003 | PA0–PA3 | `[7:6]` |
| 左轮直流 | TB6612 | PB14(AIN1), PB15(AIN2) | `[5:4]` |
| 右轮直流 | TB6612 | PB13(BIN1), PB12(BIN2) | `[3:2]` |

副板 `next_command()` 收到帧后统一解码并一次性应用到三个电机，无效帧则全停。
