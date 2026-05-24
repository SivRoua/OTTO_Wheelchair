/*
 * ================================================================
 * uart_bus — 公共 UART 抽象层（ESP-IDF 端口实现）
 * ================================================================
 *
 * 本文件实现了 uart_bus.h 中声明的 5 个 API 函数。
 * 所有与 ESP-IDF 的具体依赖都集中在这里，
 * 上层模块（LoRa、GPS）通过 uart_bus_handle_t 句柄间接操作串口，
 * 不需要直接接触 ESP-IDF 的 UART API。
 *
 * 代码风格说明：
 *   - 缩进：4 空格
 *   - 大括号位置：K&R 风格（与函数/控制语句同行）
 *   - 注释：每段逻辑前有说明，难懂的 C 语法有额外解释
 */

/*
 * stdint.h / stdbool.h : 标准 C99 整数与布尔类型
 *   uint8_t / uint32_t / bool 等跨平台类型定义在此。
 *   使用这些类型可保证不同编译器和 MCU 上位宽一致。
 *
 * string.h : memcpy、memset 等内存操作函数
 *   本模块中用于 uart_write_bytes 发送数据。
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/*
 * esp_log.h : ESP-IDF 日志输出
 *   ESP_LOGI / ESP_LOGE 等宏定义在此。
 *   - 在 menuconfig 中可以独立控制每个 TAG 的日志等级
 *   - 生产固件将日志等级调高即可关闭调试输出
 */
#include "esp_log.h"

/*
 * ESP-IDF UART 驱动头文件
 *   uart_config_t 结构体
 *   uart_param_config()     — 配置 UART 参数（波特率、数据位等）
 *   uart_set_pin()          — 绑定 TX/RX/RTS/CTS 引脚
 *   uart_driver_install()   — 安装 UART 驱动，分配缓冲区
 *   uart_write_bytes()      — 发送数据
 *   uart_read_bytes()       — 接收数据
 *   uart_flush_input()      — 清空接收缓冲区
 *   uart_driver_delete()    — 卸载驱动
 *
 * 注意 uart_port_t 类型：
 *   ESP-IDF 用 uart_port_t 枚举表示 UART 编号
 *   （UART_NUM_0 = 0, UART_NUM_1 = 1, UART_NUM_2 = 2），
 *   它本质上就是 int。我们传入的 uart_bus_handle_t 也是 int，
 *   所以在调用 UART API 时用 (uart_port_t)bus 做显式类型转换。
 *   形式：将 int "转换" 为 uart_port_t 枚举
 *   含义：告诉编译器这里确实是合法的 UART 编号
 *   风险：如果 bus 是 -1 或 3（非法编号），行为未定义
 *         所以需要由调用方保证传入合法的句柄
 */
#include "driver/uart.h"

#include "uart_bus.h"

/* ================================================================
 * 模块私有宏定义
 * ================================================================
 *
 * #define 宏在编译预处理阶段做纯文本替换。
 * 这里用宏而不是 const 变量的原因是：
 *   宏的值在编译时确定，可以用于静态数组大小声明。
 *   如果改成 const int，在 C89/C99 中不能做
 *   "uint8_t buf[RX_BUF_SIZE]" 这样的栈上数组声明，
 *   因为 const int 在 C 中不是编译期常量。
 *   （C99 支持 VLA 可变长度数组，但嵌入式开发通常禁用，
 *    因为 VLA 可能导致栈溢出且无法被静态检测。）
 */

/* 接收缓冲区大小（字节） */
#define RX_BUF_SIZE     1024

/* 发送缓冲区大小（字节），0 表示使用硬件 FIFO 直写 */
#define TX_BUF_SIZE     0

/*
 * 无效句柄标记值。
 * 因为 uart_bus_handle_t 是 int 类型，
 * 合法的句柄应是 UART_NUM_0(0)、UART_NUM_1(1)、UART_NUM_2(2)，
 * 所以 -1 可以被用作"无效"标记。
 * 选择 -1 而不是 0 的原因：
 *   0 在枚举 UART_NUM_0 中是合法值（控制台串口），
 *   而 -1 不在任何合法 UART 编号范围内。
 */
#define UART_BUS_INVALID_HANDLE     (-1)

/*
 * ESP-IDF 日志标签。
 * 每个组件通常定义一个 TAG，配合 ESP_LOGI / ESP_LOGE 使用。
 * 在终端日志输出中 TAG 会显示在行首，便于区分日志来源。
 * 因为 TAG 只在本文件内使用，用 static 限制它的作用域。
 * static 的作用：
 *   在文件作用域中，static 意味着"内部链接"（internal linkage），
 *   即该变量只在本编译单元内可见，其他 .c 文件中的同名变量不会冲突。
 *   （这与函数内的 static 不同——函数内 static 表示"静态存储期"。）
 */
static const char *TAG = "uart_bus";

/* ================================================================
 * 公开 API 实现
 * ================================================================
 *
 * 以下每个函数前都有详细说明，格式参照 Doxygen 风格。
 * 虽然 uart_bus.h 中已有函数声明，但实现文件中的注释
 * 更偏向说明"怎么做"而非"做什么"。
 */

/*
 * uart_bus_init — 初始化一个 UART 并返回句柄
 *
 * 这个函数做了三件事，按顺序分别是：
 *   1. 配置 UART 通信参数（波特率、数据位、停止位、校验、流控）
 *   2. 绑定 TX/RX 引脚到 UART 控制器
 *   3. 安装驱动，分配收发环形缓冲区
 *
 * 三个步骤有顺序依赖：必须先配置参数然后绑定引脚，
 * 装好驱动之后才能读写操作。如果中间任一步失败，函数返回 -1。
 *
 * 这个函数不做的事情：
 *   不检查目标 UART 是否已被其他模块占用。
 *   如果同一个 UART_NUM 被重复初始化，
 *   uart_driver_install 会返回 ESP_ERR_INVALID_STATE，
 *   本函数会检测到并返回失败。
 *
 * @param cfg  UART 配置（UART 编号、引脚、波特率），不可为 NULL
 * @return     成功返回 uart_bus_handle_t（即 UART 编号）
 *             失败返回 UART_BUS_INVALID_HANDLE (-1)
 *
 * C 语法说明：
 *   const uart_bus_config_t *cfg
 *     const 限定指针指向的内容不可修改。
 *     这样做有两个目的：
 *     (1) 保证函数不会意外修改传入的配置参数
 *     (2) 允许调用者传入 const 结构体（更安全的编程风格）
 */
uart_bus_handle_t uart_bus_init(const uart_bus_config_t *cfg)
{
    /*
     * 参数有效性检查。
     * 如果 cfg 是 NULL，说明调用者传入了空指针。
     * 此时如果继续访问 cfg->xxx 会导致 CPU 异常
     * （在 ARM Cortex-M 上触发 MemManage Fault 或 HardFault）。
     * 所以先做防御性判断。
     *
     * 这里的写法：
     *   if (!cfg) 等价于 if (cfg == NULL)
     *   但 C 语言中空指针在布尔上下文中求值为 false，
     *   所以 !NULL == true。
     *   （C 标准 $6.3.2.3/3：任何类型指针被 0 赋值或比较时，
     *    在源码中写作 NULL，在运行期成为 0 地址。）
     */
    if (!cfg) {
        ESP_LOGE(TAG, "uart_bus_init: cfg is NULL");
        return UART_BUS_INVALID_HANDLE;
    }

    esp_err_t ret;

    /* ---- 步骤 1：配置 UART 参数 ---- */

    /*
     * uart_config_t 是 ESP-IDF 定义的 UART 配置结构体。
     * C99 的指定初始化器语法（Designated Initializer）：
     *   .成员名 = 值，
     * 这样即使结构体成员顺序改变，初始化的对应关系也不会错。
     * 相比顺序初始化（uart_config_t { 9600, ... }），
     * 指定初始化器更安全、可读性更好。
     *
     * 这里配置为 8N1（8 数据位，无校验，1 停止位），
     * 无硬件流控。这是串口透传模块最通用的配置。
     *
     * .source_clk = UART_SCLK_DEFAULT
     *   ESP-IDF v5.x 要求显式指定 UART 的时钟源。
     *   UART_SCLK_DEFAULT 让驱动选择默认时钟源。
     */
    uart_config_t uart_cfg = {
        .baud_rate  = cfg->baud_rate,   /* 波特率，如 9600、115200 */
        .data_bits  = UART_DATA_8_BITS, /* 8 位数据位 */
        .parity     = UART_PARITY_DISABLE, /* 无校验 */
        .stop_bits  = UART_STOP_BITS_1, /* 1 位停止位 */
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE, /* 无硬件流控 */
        .source_clk = UART_SCLK_DEFAULT, /* 时钟源自动选择 */
    };

    /*
     * uart_param_config：将结构体配置写入 UART 控制器寄存器。
     * 第一个参数 (uart_port_t)cfg->uart_num 是 UART 编号的强制类型转换。
     * 因为 cfg->uart_num 类型是 int，ESP-IDF 函数期望 uart_port_t。
     * 虽然 enum 和 int 在 C 中互为兼容类型（在函数参数处可隐式转换），
     * 显式转换能让代码读者清楚这里发生了类型转换。
     *
     * ESP_ERROR_CHECK 宏用于生产代码时：
     *   如果 ret != ESP_OK，它会在错误信息中打印文件和行号，然后 abort()。
     * 但本模块使用检查返回值 + 返回错误标记的方式，
     * 避免模块初始化失败直接导致系统复位。
     * 调用者可以检查返回值做出合适的相应（如重试、告警等）。
     */
    ret = uart_param_config((uart_port_t)cfg->uart_num, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed on UART%d: %d",
                 cfg->uart_num, ret);
        return UART_BUS_INVALID_HANDLE;
    }

    /* ---- 步骤 2：绑定 TX/RX 引脚 ---- */

    /*
     * uart_set_pin：将 UART 控制器的 TX 和 RX 信号路由到指定的 GPIO 引脚。
     * 参数含义：
     *   第 1 个参数 (uart_port_t)cfg->uart_num — UART 编号
     *   第 2 个参数 cfg->txd_pin — TX 引脚（ESP32 的 TX → 模块的 RX）
     *   第 3 个参数 cfg->rxd_pin — RX 引脚（ESP32 的 RX ← 模块的 TX）
     *   第 4 个参数 UART_PIN_NO_CHANGE — RTS 引脚不改变（-1）
     *   第 5 个参数 UART_PIN_NO_CHANGE — CTS 引脚不改变（-1）
     * UART_PIN_NO_CHANGE 定义为 -1，表示"不需要修改这个引脚"。
     * 因为串口透传模块通常不需要流控，所以 RTS/CTS 不接。
     *
     * 注意：ESP32 的大部分 GPIO 都可以作为 UART 引脚，
     * 但某些 GPIO（如 6~11 用于 Flash）不能随意使用。
     */
    ret = uart_set_pin(
        (uart_port_t)cfg->uart_num,       /* UART 编号 */
        cfg->txd_pin,                      /* TX 引脚 */
        cfg->rxd_pin,                      /* RX 引脚 */
        UART_PIN_NO_CHANGE,                /* RTS 不变 */
        UART_PIN_NO_CHANGE                 /* CTS 不变 */
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed on UART%d: %d",
                 cfg->uart_num, ret);
        return UART_BUS_INVALID_HANDLE;
    }

    /* ---- 步骤 3：安装驱动 — 分配收发缓冲区 ---- */

    /*
     * uart_driver_install：分配 UART 驱动内部使用的资源。
     * 参数含义：
     *   第 1 个参数 — UART 编号
     *   第 2 个参数 RX_BUF_SIZE — 接收环形缓冲区大小（字节）
     *     这里设为 1024，足够容纳 LoRa 最大 256 字节 + GPS 若干行 NMEA
     *   第 3 个参数 TX_BUF_SIZE — 发送缓冲区大小（字节）
     *     这里设为 0，表示不使用软件发送缓冲。
     *     tx_buf_size = 0 时，uart_write_bytes 直接将数据写入硬件 FIFO，
     *     不经过额外的 RAM 拷贝。对于小数据包来说效率更高。
     *   第 4 个参数 queue_size — 事件队列大小
     *     0 表示不使用 UART 事件队列（本模块使用非阻塞轮询模式读取）
     *   第 5 个参数 queue — 事件队列句柄的输出指针，传递 NULL 因为不需要
     *   第 6 个参数 intr_alloc_flags — 中断分配标志
     *     0 表示使用默认分配（ESP_INTR_FLAG_LOWMED）。
     *
     * 关于 TX_BUF_SIZE = 0 的深入解释：
     *   如果 TX_BUF_SIZE > 0，uart_write_bytes 会先把数据拷贝到
     *   软件发送缓冲区，然后分批送入硬件 FIFO。
     *   好处是：上层函数几乎总是立即返回（不阻塞）。
     *   坏处是：多了一次内存拷贝，且多占了一块 RAM。
     *   对于 LoRa/ESP32 这种交互式的低数据量场景，
     *   使用 FIFO 直写（TX_BUF_SIZE = 0）更省内存，也更直接。
     *   数据量小的情况下硬等到 FIFO 空也基本不花时间。
     */
    ret = uart_driver_install(
        (uart_port_t)cfg->uart_num,       /* UART 编号 */
        RX_BUF_SIZE,                       /* 接收缓冲区 1024 字节 */
        TX_BUF_SIZE,                       /* 发送缓冲区 0（使用硬件 FIFO） */
        0,                                 /* 不创建事件队列 */
        NULL,                              /* 不获取队列句柄 */
        0                                  /* 默认中断标志 */
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed on UART%d: %d, "
                 "可能已被其他模块占用", cfg->uart_num, ret);
        return UART_BUS_INVALID_HANDLE;
    }

    ESP_LOGI(TAG, "UART%d initialized: TX=%d RX=%d %lu 8N1",
             cfg->uart_num, cfg->txd_pin, cfg->rxd_pin,
             (unsigned long)cfg->baud_rate);

    /*
     * 返回句柄。
     * 这里直接用 cfg->uart_num 作为句柄值。
     * 因为 int 是值类型（不是指针），返回的是 UART 编号的副本，
     * 调用者拿到的句柄和 cfg->uart_num 数值相同。
     *
     * 对比 lcd12864 返回 void*：
     *   lcd12864_create 返回的是 calloc 分配的堆内存地址。
     *   uart_bus_init 返回的是 int 类型的 UART 编号。
     *   两者都是"不透明句柄"的变体——
     *   句柄是供调用者保存、传回给后续函数的令牌，
     *   调用者不需要理解句柄的内部含义。
     */
    return cfg->uart_num;
}

/*
 * uart_bus_write — 通过 UART 发送数据
 *
 * 将 data 指向的 len 个字节通过串口发送出去。
 * 内部调用 uart_write_bytes()。
 *
 * 关于"阻塞"的说明：
 *   uart_write_bytes 是阻塞调用，但阻塞时间非常短——
 *   它只等数据全部写入硬件 FIFO 队列，不等它们在电线上逐个发完。
 *   对于一个 9600 波特率的串口，发送一字节约需 1ms。
 *   如果发送 60 字节的数据包且 TX FIFO 已满，
 *   阻塞时间最多几十微秒（取决于 FIFO 空间），
 *   但对于 task 调度来说可以忽略。
 *
 * @param bus   uart_bus 句柄（即 UART_NUM_1 或 UART_NUM_2）
 * @param data  待发送数据的缓冲区指针
 * @param len   待发送数据的字节数
 *
 * C 语法说明：
 *   const uint8_t *data
 *     声明 data 指向 uint8_t 类型的值，且通过 data 指针
 *     不能修改该值。这通常表示 data 缓冲区是由调用者管理，
 *     写函数只读取内容不修改。
 *
 *   为什么不用 uint8_t *data？
 *     如果写函数参数类型是非 const 的，
 *     调用者传入一个 const 数组时编译器会报警告。
 *     参数写 const 可以让函数接受 const 和非 const 的缓冲区。
 */
void uart_bus_write(uart_bus_handle_t bus, const uint8_t *data, uint32_t len)
{
    /*
     * 防御性检查：
     *   如果 data 是 NULL，uart_write_bytes 的行为未定义。
     *   len == 0 则没有发送的必要。
     *   注意这里不检查 bus 是否合法（如 bus == -1），
     *   因为如果句柄本身就是无效的，那说明上层代码
     *   在初始化失败后仍然尝试发送数据——这是调用者的 bug，
     *   驱动层不应该默默吞咽错误。
     *   在调试阶段可以让它在 uart_write_bytes 内部崩溃
     *   以便尽早发现。
     */
    if (!data || len == 0) {
        return;
    }

    /*
     * uart_write_bytes：
     *   第 1 个参数 (uart_port_t)bus：UART 编号
     *   第 2 个参数 (const char *)data：
     *     uart_write_bytes 的签名期待 const char*，
     *     但我们的数据缓冲区是 uint8_t*。
     *     在 C 语言中，char 和 uint8_t 虽然都是 1 字节，
     *     但在类型系统里是不同的类型。
     *     这里用显式强制转换：
     *       (const char *)data
     *     告诉编译器"我知道类型不匹配，请按我的意思来"。
     *     这是嵌入式编程中常见的做法——因为 UART 发送
     *     原始字节时，byte 和 char 没有语义区别。
     *   第 3 个参数 len：发送长度
     *
     *   关于 (const char *) 转换的安全说明：
     *     从 uint8_t* 转换到 char* 是安全的（两者都是指向
     *     单字节数据的指针），转换后的指针指向完全相同的内存地址。
     *     这里 const 保持不变，所以数据不会被修改。
     */
    uart_write_bytes((uart_port_t)bus, (const char *)data, len);
}

/*
 * uart_bus_read — 从 UART 接收数据（非阻塞）
 *
 * 尝试从 UART 接收缓冲区读取最多 max_len 个字节，
 * 存储到 buf 中。
 *
 * 这个函数是非阻塞的——如果缓冲区中没有数据，立即返回 0。
 * 它不会等待数据到达（超时设为 0）。
 * 调用者应该在自己的任务循环中定期调用此函数来收集数据。
 *
 * @param bus     uart_bus 句柄
 * @param buf     存放接收数据的缓冲区，由调用者提供
 * @param max_len buf 的最大容量（字节）
 * @return        实际读取的字节数（0 ~ max_len）
 *
 * 返回值设计说明：
 *   返回 int 而非 uint32_t 是因为 ESP-IDF 的 uart_read_bytes
 *   返回 int（-1 表示出错）。我们也沿用这个约定。
 *   但在这里，0 表示没有数据或操作顺利完成，
 *   -1 理论上不会出现（因为超时 0 不会产生错误），
 *   但保持返回类型一致以便调用者可以按 int 处理。
 */
int uart_bus_read(uart_bus_handle_t bus, uint8_t *buf, uint32_t max_len)
{
    if (!buf || max_len == 0) {
        return 0;
    }

    /*
     * uart_read_bytes 参数说明：
     *   第 1 个参数：UART 编号
     *   第 2 个参数：接收缓冲区
     *   第 3 个参数：期望读取的最大字节数
     *   第 4 个参数：等待超时时间（单位：RTOS Tick）
     *     0 表示"不等待"——有数据就返回数据，没数据就返回 0。
     *     与 uart_write_bytes 的"阻塞直到写入 FIFO"不同，
     *     这里的 0 超时让 read 变成纯非阻塞调用。
     *     这是有意为之——因为不同模块的接收模式不同：
     *       - LoRa：可以在专用任务中循环轮询读取
     *       - GPS：可以在专用任务中循环轮询读取
     *     如果是阻塞式读取（等待直到收到指定长度的数据），
     *     调用者就不得不使用任务通知或队列等同步机制，
     *     这会把 uart_bus 和 FreeRTOS 的同步原语绑定更紧。
     *     所以这里选择最通用的非阻塞模式。
     */
    return uart_read_bytes((uart_port_t)bus, buf, max_len, 0);
}

/*
 * uart_bus_flush — 清空接收缓冲区
 *
 * 丢弃 UART 驱动接收环形缓冲区中所有尚未读取的数据。
 *
 * 典型使用场景：
 *   切换 LoRa 模块的工作模式后，模块可能会发送一些
 *   残留的响应或状态字节到串口。如果不丢弃这些数据，
 *   下一次读串口时可能会读到旧的无用数据，导致解析混淆。
 *   所以切换模式后应调用 uart_bus_flush。
 *
 * @param bus uart_bus 句柄
 *
 * 实现说明：
 *   uart_flush_input 会丢弃所有已接收但未被读取的数据。
 *   它只清空接收方向，不影响发送方向。
 */
void uart_bus_flush(uart_bus_handle_t bus)
{
    /*
     * uart_flush_input 返回 esp_err_t，
     * 但在 flush 操作的语境下，失败的后果不太严重——
     * 最多就是清空失败，旧数据还在缓冲区里而已。
     * 所以这里忽略返回值。
     *
     * 但有一个例外：如果 bus 是非法 UART 编号（如 3 或 -1），
     * uart_flush_input 可能引发断言。
     * 所以调用者应确保传入的句柄是有效的。
     */
    uart_flush_input((uart_port_t)bus);
}

/*
 * uart_bus_deinit — 注销 UART，释放所有资源
 *
 * 逆操作 uart_bus_init。
 * 调用 uart_driver_delete 释放驱动内部资源
 * （队列、环形缓冲区、中断处理函数等）。
 *
 * 调用此函数后，该 UART 就不可再用了。
 * 若要再次使用，需要重新调用 uart_bus_init。
 *
 * @param bus uart_bus 句柄（即 UART 编号）
 *
 * 安全说明：
 *   如果其他模块或任务仍然持有这个 UART 的句柄并仍在读写，
 *   调用 uart_bus_deinit 会导致未定义行为。
 *   上层需要在调用 deinit 前确保所有使用者已停止。
 *
 *   与 lcd12864_deinit 的对比：
 *     lcd12864_deinit 要释放 frame buffer（堆内存）、
 *     互斥锁、SPI 设备句柄等多个资源。
 *     uart_bus_deinit 只释放一个 uart_driver_install
 *     分配的资源，所以实现更简单。
 */
void uart_bus_deinit(uart_bus_handle_t bus)
{
    /*
     * uart_driver_delete 参数说明：
     *   它接受 uart_port_t 枚举，表示要释放的 UART 编号。
     *   释放后，该 UART 不再可用，引脚恢复到 GPIO 模式
     *   （注意：恢复的 GPIO 模式是输入模式，
     *    如果你之前配置成输出，需要重新配回输出。
     *    但在本模块的典型用法中很少需要这么做。）
     *
     * 关于 uart_driver_delete 的阻塞行为：
     *   它会等待当前正在进行的传输完成。
     *   如果串口正在发送最后一个字节，它会在那几毫秒内阻塞。
     */
    uart_driver_delete((uart_port_t)bus);

    ESP_LOGI(TAG, "UART%d deinitialized", bus);
}
