# 蜂鸣器模块说明

## 一、硬件特性

| 项目 | 值 |
| --- | --- |
| 类型 | 有源蜂鸣器 |
| 触发方式 | 低电平触发 |
| 接口 | 单 GPIO 输出 |

有源蜂鸣器内置振荡电路，GPIO 拉低即响，拉高即停，无需 PWM。

---

## 二、目录结构

```
components/buzzer/
├── CMakeLists.txt
├── README.md
├── include/
│   └── buzzer.h    ← 公开 API
└── buzzer.c        ← 实现
```

---

## 三、API 说明

| 函数 | 说明 |
| --- | --- |
| `buzzer_init(pin)` | 初始化 GPIO，默认高电平（静音） |
| `buzzer_on()` | 拉低 GPIO，开始响 |
| `buzzer_off()` | 拉高 GPIO，停止响 |
| `buzzer_beep(duration_ms)` | 异步响指定毫秒后自动停止，立即返回 |

---

## 四、典型用法

```c
#include "buzzer.h"

// 初始化，指定 GPIO 引脚
buzzer_init(13);

// 响 500ms，不阻塞主任务
buzzer_beep(500);

// 手动控制
buzzer_on();
vTaskDelay(pdMS_TO_TICKS(200));
buzzer_off();
```

---

## 五、实现说明

`buzzer_beep` 内部创建一个临时 FreeRTOS task，task 执行 `buzzer_on` → `vTaskDelay` → `buzzer_off` → `vTaskDelete(NULL)` 后自动销毁，不占用调用方的执行流。

`duration_ms` 通过 `(void*)(uintptr_t)` 转型传入 task 参数，避免堆分配。
