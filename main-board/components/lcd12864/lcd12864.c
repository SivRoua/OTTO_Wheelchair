/*
 * ================================================================
 * LCD12864 (UC1701X 或兼容) 驱动核心
 * ================================================================
 * 本文件实现了所有与硬件无关的显示逻辑，包括初始化序列、
 * 文本显示控制等。所有与具体平台硬件交互的操作均通过
 * 端口层提供的函数指针完成，因此本文件可跨平台复用。
 *
 * C 语言知识点：
 * 1. 不透明指针：在 .h 中声明类型但不定以完整结构体，在 .c 中才定义。
 * 2. 函数指针：通过结构体中的函数指针实现类似“虚函数”的多态。
 * 3. extern：引用外部定义的全局常量（端口操作表实例）。
 */

#include <stdlib.h>      /* 提供 calloc, free */
#include <string.h>      /* 提供 memset 操作帧缓冲区 */
#include "lcd12864.h"
#include "lcd12864_port.h"  /* 获取端口操作表和端口配置类型 */
#include "font_8x16.h"      /* 8x16 ASCII 字库，95 个可打印字符的点阵数据 */

/*
 * FreeRTOS 头文件：
 * FreeRTOS.h 提供基础类型和宏（如 portMAX_DELAY），
 * semphr.h 提供互斥锁（SemaphoreHandle_t）相关 API。
 * 这两者用于保护帧缓冲区和 SPI 通信不被多任务并发破坏。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * ================================================================
 * 内部上下文结构体（真正的屏幕实例）
 * ================================================================
 * 该结构体对用户不可见，所有状态都封装在此。
 * 成员：
 *   ops         - 平台操作函数集，用于调用底层硬件函数
 *   cursor_x    - 当前光标列坐标（像素单位，0~127）
 *   cursor_y    - 当前光标行（页单位，0~7）
 *   is_on       - 显示开关状态（true：开，false：关）
 *   framebuffer - 帧缓冲区指针，指向 1024 字节堆内存
 *                 布局：128 列 × 8 页，每页 128 字节，
 *                 每字节对应一列中 8 个垂直像素点
 *   mutex       - FreeRTOS 互斥锁句柄，保护帧缓冲和 SPI
 */
struct lcd12864_ctx{
    lcd12864_port_ops_t ops;  /* 操作表，包含函数指针 */
    uint8_t cursor_x;
    uint8_t cursor_y;
    bool is_on;
    uint8_t *framebuffer;     /* 帧缓冲区：128 × 64 / 8 = 1024 字节 */
    SemaphoreHandle_t mutex;  /* 互斥锁，用于多任务保护 */
};

/*
 * ================================================================
 * 引用端口层提供的全局操作表实例
 * ================================================================
 * 变量 lcd12864_port_ops 在 port/esp_idf_spi.c 中定义，
 * 并已填充所有函数指针。此处用 extern 声明，表示该变量
 * 在其他文件中存在，链接时会找到它。
 */
extern const lcd12864_port_ops_t lcd12864_port_ops;

/*
 * ================================================================
 * 内部辅助函数：将公开配置安全转换为端口层配置
 * ================================================================
 * 两个结构体字段相同，此处采用逐字段赋值的方式，
 * 而非直接指针强转，以避免 C 语言中结构体指针类型转换的
 * 未定义行为（strict aliasing 违规）。
 */
static lcd12864_port_config_t cfg_to_port(const lcd12864_config_t *cfg) {
    /*
     * 在栈上创建端口层配置实例，逐字段复制。
     * 这样做虽然比指针强转多了一次内存拷贝，但保证了
     * 类型安全，且拷贝开销（几十字节）相对于初始化流程
     * 的 SPI/GPIO 配置来说完全可以忽略。
     */
    lcd12864_port_config_t port_cfg;
    port_cfg.sclk   = cfg->sclk;
    port_cfg.sda    = cfg->sda;
    port_cfg.rs     = cfg->rs;
    port_cfg.cs     = cfg->cs;
    port_cfg.reset  = cfg->reset;
    port_cfg.freq_hz = cfg->freq_hz;
    return port_cfg;
}

/*
 * ================================================================
 * 公开 API 实现
 * ================================================================ */

/**
 * @brief 创建屏幕实例，完成硬件和软件初始化
 *
 * 流程：
 * 1. 分配上下文内存（使用 calloc 自动清零）
 * 2. 获取并复制平台操作表
 * 3. 调用端口层初始化硬件（GPIO、SPI）
 * 4. 执行硬件复位时序
 * 5. 调用软件初始化序列
 * 6. 若任何步骤失败，释放已分配资源并返回 NULL
 */
void* lcd12864_create(const lcd12864_config_t *cfg) {
    /* 分配上下文，calloc 会自动将所有成员置零（包括 framebuffer 和 mutex） */
    lcd12864_ctx_t *ctx = calloc(1, sizeof(lcd12864_ctx_t));
    if (!ctx) {
        return NULL;
    }

    /* 获取平台操作表，后续所有硬件调用都通过 ctx->ops */
    ctx->ops = lcd12864_port_ops;

    /* 初始化硬件（使用安全转换后的端口配置） */
    lcd12864_port_config_t port_cfg = cfg_to_port(cfg);
    ctx->ops.init(&port_cfg);

    /* 硬件复位：先拉低复位引脚 100ms，再拉高 100ms */
    ctx->ops.reset(false);
    ctx->ops.delay_ms(100);
    ctx->ops.reset(true);
    ctx->ops.delay_ms(100);

    /*
     * 分配帧缓冲区：128 列 × 64 行 / 8 位每字节 = 1024 字节。
     * 该缓冲区存储完整的屏幕内容，所有绘图操作先写缓冲区，
     * 最后由 flush() 整屏发送到硬件。
     */
    ctx->framebuffer = malloc(1024);
    if (!ctx->framebuffer) {
        ctx->ops.deinit();
        free(ctx);
        return NULL;
    }

    /*
     * 创建 FreeRTOS 互斥锁。
     * xSemaphoreCreateMutex() 返回的互斥锁支持优先级继承，
     * 可防止优先级翻转问题。在多任务环境中保护帧缓冲和 SPI
     * 通信的原子性。
     */
    ctx->mutex = xSemaphoreCreateMutex();
    if (!ctx->mutex) {
        free(ctx->framebuffer);
        ctx->ops.deinit();
        free(ctx);
        return NULL;
    }

    /* 执行软件初始化命令序列 */
    if (lcd12864_init(ctx) != ESP_OK) {
        vSemaphoreDelete(ctx->mutex);
        free(ctx->framebuffer);
        ctx->ops.deinit();
        free(ctx);
        return NULL;
    }

    /* 初始化完成后清屏并刷新，使屏幕显示空白 */
    lcd12864_clear(ctx);
    lcd12864_flush(ctx);

    return ctx;
}

/**
 * @brief 屏幕软件初始化序列
 *
 * 发送一系列命令来配置屏幕的工作模式。
 * 具体命令和延时需根据屏幕控制器数据手册编写。
 * 下方以 UC1701X 控制器为例，给出一个最小初始化流程。
 * 实际项目请替换为你的屏幕 IC 要求的命令。
 */
esp_err_t lcd12864_init(lcd12864_ctx_t *handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;

    /* ---------- 复位后延时，等待内部稳定 ---------- */
    handle->ops.delay_ms(10);

    /* ---------- 系统复位命令（可选） ---------- */
    handle->ops.write_cmd(0xE2); /* 软件复位 */
    handle->ops.delay_ms(10);

    /* ---------- 电源控制 ---------- */
    handle->ops.write_cmd(0x2F); /* 升压器、电压调节器、电压跟随器均打开 */
    handle->ops.delay_ms(10);

    /* ---------- 显示开启 ---------- */
    handle->ops.write_cmd(0xAF); /* 显示开 */
    handle->ops.delay_ms(10);

    /* ---------- 显示方向设置 ---------- */
    /*
     * 当前模块默认需要同时修正左右镜像和上下镜像：
     * - SEG remap: 0xA1 反转列输出方向，修正左右镜像，默认值为 0xA0
     * - COM scan direction: 0xC8 反转扫描方向，修正上下镜像，默认值为 0xC0
     * 若你的屏幕安装方向不同，可按硬件实际情况改回 0xA0 / 0xC0。
     */
    handle->ops.write_cmd(0xA0); /* SEG remap = reverse */
    handle->ops.write_cmd(0xC8); /* COM scan direction = reverse */

    /* 更新状态 */
    handle->is_on = true;
    handle->cursor_x = 0;
    handle->cursor_y = 0;

    return ESP_OK;
}

/**
 * @brief 反初始化屏幕
 */
void lcd12864_deinit(lcd12864_ctx_t *handle) {
    if (!handle) return;

    /* 关闭显示（可选） */
    handle->ops.write_cmd(0xAE); /* 显示关 */
    handle->ops.delay_ms(10);

    /* 释放硬件资源 */
    handle->ops.deinit();

    /* 释放 FreeRTOS 互斥锁 */
    if (handle->mutex) {
        vSemaphoreDelete(handle->mutex);
    }

    /* 释放帧缓冲区 */
    if (handle->framebuffer) {
        free(handle->framebuffer);
    }

    /* 释放上下文内存 */
    free(handle);
}

/**
 * @brief 清屏，并将光标归零
 */
void lcd12864_clear(lcd12864_ctx_t *handle) {
    if (!handle || !handle->framebuffer) return;

    /* 将帧缓冲区全部置零，即熄灭所有像素 */
    memset(handle->framebuffer, 0x00, 1024);

    /* 光标返回左上角 */
    handle->cursor_x = 0;
    handle->cursor_y = 0;
}

/**
 * @brief 打开显示
 */
void lcd12864_display_on(lcd12864_ctx_t *handle) {
    if (!handle) return;
    handle->ops.write_cmd(0xAF); /* 显示开命令（UC1701X） */
    handle->is_on = true;
}

/**
 * @brief 关闭显示
 */
void lcd12864_display_off(lcd12864_ctx_t *handle) {
    if (!handle) return;
    handle->ops.write_cmd(0xAE); /* 显示关命令 */
    handle->is_on = false;
}

/**
 * @brief 设置光标位置
 *
 * 对于 UC1701X 这类图形点阵屏，通常需要分别设置页地址（行）
 * 和列地址。此处假设采用常见的页寻址模式：
 *   - 设置页地址 (0xB0 | y)  y 为页号（0~7）
 *   - 设置列地址高半字节 (0x10 | (x>>4))
 *   - 设置列地址低半字节 (0x00 | (x & 0x0F))
 * 用户可根据实际 IC 调整。
 */
void lcd12864_set_cursor(lcd12864_ctx_t *handle, uint8_t x, uint8_t y) {
    if (!handle) return;

    /*
     * 在帧缓冲模式下，本函数仅更新内部光标坐标（像素列 x，页号 y），
     * 不再向硬件发送地址设置命令。地址设置由 flush() 在刷屏时统一管理。
     * 光标坐标供 write_char / write_string 确定字符写入帧缓冲的位置。
     */
    handle->cursor_x = x;
    handle->cursor_y = y;
}

/**
 * @brief 在当前光标位置写入一个字节
 *
 * 对于图形模式，写入的字节直接对应竖向 8 个像素（页）。
 * 写入后光标通常自动右移一列。
 */
void lcd12864_write_char(lcd12864_ctx_t *handle, char ch) {
    if (!handle || !handle->framebuffer) return;

    /*
     * font8x16 数组下标映射：
     * ASCII 0x20（空格）对应 font8x16[0]，
     * ASCII 0x21（!）  对应 font8x16[1]，
     * ...
     * ASCII 0x7E（~）  对应 font8x16[94]。
     * 非可打印字符统一显示为空格。
     */
    uint8_t idx;
    if ((uint8_t)ch >= 0x20 && (uint8_t)ch <= 0x7E) {
        idx = (uint8_t)ch - 0x20;
    } else {
        idx = 0;  /* 控制字符显示为空格 */
    }
    const uint8_t *bitmap = font8x16[idx];

    /* 边界保护：限制光标不超出可绘制范围 */
    if (handle->cursor_x > 120) handle->cursor_x = 120; /* 需留 8 像素宽 */
    if (handle->cursor_y > 6)  handle->cursor_y = 6;    /* 需留下一页（y+1） */

    /*
     * 将一个 8x16 字符写入帧缓冲区。
     *
     * 帧缓冲区寻址方式：
     *   offset = page * 128 + column
     *   其中 page 为页号（0~7），column 为像素列（0~127）
     *
     * 字库格式 vs 硬件格式：
     *   字库 font8x16 中每个字符 16 字节，每字节编码一行中 8 个水平像素
     *   （bit7=最左像素，bit0=最右像素）。
     *   LCD 页模式下每字节编码一列中 8 个垂直像素
     *   （bit0=页内最上像素，bit7=页内最下像素）。
     *   因此不能直接拷贝 bitmap[n] → framebuffer[列偏移 + n]，
     *   需要对 8×8 矩阵做位转置（bit transpose）。
     *
     * 转置逻辑（以上半部分为例）：
     *   输入 bitmap[0..7] 中 bitmap[row] 的 bit(7-col) 表示像素 (row, col) 的点亮状态。
     *   输出 col_data[0..7] 中 col_data[col] 的 bit(row) 指向同一点亮状态。
     */
    uint8_t col_byte;
    uint16_t top_base = (uint16_t)handle->cursor_y * 128 + handle->cursor_x;

    /* 上半部分：对 bitmap[0..7] 做 8×8 位转置 */
    for (int col = 0; col < 8; col++) {
        col_byte = 0;
        for (int row = 0; row < 8; row++) {
            /*
             * 原 bitmap[row] 的 bit(7-col) 对应水平方向第 col 列。
             * 转置后该点映射到 col_byte 的 bit(row)，即垂直方向第 row 行。
             */
            if (bitmap[row] & (1 << (7 - col))) {
                col_byte |= (1 << row);
            }
        }
        handle->framebuffer[top_base + col] = col_byte;
    }

    /* 下半部分：对 bitmap[8..15] 做相同的转置，写入下一页 */
    uint16_t bot_base = (uint16_t)(handle->cursor_y + 1) * 128 + handle->cursor_x;
    for (int col = 0; col < 8; col++) {
        col_byte = 0;
        for (int row = 0; row < 8; row++) {
            if (bitmap[row + 8] & (1 << (7 - col))) {
                col_byte |= (1 << row);
            }
        }
        handle->framebuffer[bot_base + col] = col_byte;
    }

    /*
     * 光标右移一个字符宽度（8 像素）。
     * 若超出屏幕右边界（128 列），则回卷到下一行，
     * 行距为 2 页（16 像素，等于字符高度）。
     */
    handle->cursor_x += 8;
    if (handle->cursor_x >= 128) {
        handle->cursor_x = 0;
        handle->cursor_y += 2;  /* 下移两页，等于一个字符高度 */
        if (handle->cursor_y >= 8) {
            handle->cursor_y = 0;
        }
    }
}

/**
 * @brief 在当前光标位置写入字符串
 */
void lcd12864_write_string(lcd12864_ctx_t *handle, const char *str) {
    if (!handle || !str) return;

    while (*str) {
        lcd12864_write_char(handle, *str);
        str++;
    }
}

/*
 * ================================================================
 * 以下为基于帧缓冲的新增 API
 * ================================================================ */

/**
 * @brief 将帧缓冲区内容整屏刷新到屏幕硬件
 *
 * 遍历 8 个页，对每一页：
 * 1. 发送页地址命令（0xB0 | page）
 * 2. 发送列地址命令（列 0）
 * 3. 通过 write_data_bulk 批量发送该页 128 字节数据
 *
 * 这种逐页刷新的方式兼容 UC1701X 等不支持跨页自增的控制器。
 * 若控制器支持列地址自动递增，也可合并为单次 1024 字节传输。
 */
void lcd12864_flush(lcd12864_ctx_t *handle) {
    if (!handle || !handle->framebuffer) return;

    /* 逐页发送帧缓冲区内容到屏幕 */
    for (uint8_t page = 0; page < 8; page++) {
        /*
         * UC1701X 页地址命令格式：0xB0 | page (page = 0~7)
         * 该命令将显示 RAM 的读写指针移动到指定页。
         */
        handle->ops.write_cmd(0xB0 | page);

        /*
         * 列地址设置分为两步：
         * 高位：0x10 | (column >> 4)  — 取列地址的高 4 位
         * 低位：0x00 | (column & 0x0F) — 取列地址的低 4 位
         * 此处列地址为 0，因此发送 0x10 和 0x00。
         */
        handle->ops.write_cmd(0x10);  /* 列地址高位 = 0 */
        handle->ops.write_cmd(0x00);  /* 列地址低位 = 0 */

        /*
         * 批量发送该页的全部 128 字节数据。
         * &framebuffer[page * 128] 指向该页起始位置。
         * 使用批量传输而非逐字节发送，可大幅减少 SPI 事务开销。
         */
        handle->ops.write_data_bulk(
            &handle->framebuffer[page * 128],
            128
        );
    }
}

/**
 * @brief 在指定像素位置画一个点
 *
 * 帧缓冲区字节布局：
 *   每字节 = 一列中 8 个垂直像素（从上到下排列）
 *   字节地址 = page * 128 + column
 *   其中 page = y / 8，column = x
 *   像素在该字节中的位位置 = y % 8（bit 0 为最上方）
 *
 * 例如像素 (10, 15)：
 *   page = 15 / 8 = 1，byte = 1 * 128 + 10 = 138
 *   bit = 15 % 8 = 7
 *   framebuffer[138] 的 bit 7 即表示该像素
 */
void lcd12864_draw_pixel(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, bool color) {
    if (!handle || !handle->framebuffer) return;

    /* 边界检查：超出 128×64 的坐标直接忽略 */
    if (x >= 128 || y >= 64) return;

    /*
     * 计算该像素在帧缓冲区中的位置：
     * y >> 3 等价于 y / 8，得到页号（0~7）
     * y & 0x07 等价于 y % 8，得到在字节中的位偏移（0~7）
     * 乘以 128 是因为每页 128 字节（128 列）
     */
    uint16_t offset = ((uint16_t)(y >> 3) * 128) + x;
    uint8_t bit = y & 0x07;  /* 取 y 的低 3 位 */

    if (color) {
        /* 点亮：将对应位置 1 */
        handle->framebuffer[offset] |= (1 << bit);
    } else {
        /* 熄灭：将对应位清 0 */
        handle->framebuffer[offset] &= ~(1 << bit);
    }
}

/**
 * @brief 填充一个矩形区域
 *
 * 通过遍历矩形内所有像素并调用 draw_pixel 实现。
 * 在帧缓冲模式下，所有像素修改都在内存中完成，
 * 最后需调用 flush() 一次性刷到屏幕。
 *
 * 对于全屏填充等大范围操作，如果要进一步优化性能，
 * 可以考虑按字节对齐进行 memset 优化，但当前实现
 * 对于 UI 菜单场景的高亮条、进度条等小矩形已足够。
 */
void lcd12864_fill_rect(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool color) {
    if (!handle || !handle->framebuffer) return;

    /* 裁剪矩形边界，避免写到帧缓冲区范围之外 */
    if (x >= 128 || y >= 64) return;
    if (x + w > 128) w = 128 - x;
    if (y + h > 64)  h = 64 - y;

    /* 逐行逐列绘制像素 */
    for (uint8_t row = 0; row < h; row++) {
        for (uint8_t col = 0; col < w; col++) {
            lcd12864_draw_pixel(handle, x + col, y + row, color);
        }
    }
}

/**
 * @brief 在指定像素坐标绘制一个 8x16 ASCII 字符
 *
 * 与 write_char 不同，putc 使用绝对像素坐标而非光标位置，
 * 且不修改光标状态。适合 UI 层在固定位置显示静态文本。
 *
 * x: 左上角像素列坐标（0~127），需保证 x + 7 <= 127
 * y: 左上角页号（0~6），需保证 y + 1 <= 7（字符高度占 2 页）
 * c: 要显示的 ASCII 字符（0x20~0x7E），非可打印字符显示为空格
 */
void lcd12864_putc(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, char c) {
    if (!handle || !handle->framebuffer) return;

    /* 边界保护 */
    if (x >= 128 || y >= 7) return;       /* y 不能为 7，因需 y+1 放下半部分 */
    if (x + 8 > 128) return;              /* 字符占 8 列，需留足空间 */

    /* 字符到字库索引的映射，与 write_char 一致 */
    uint8_t idx;
    if ((uint8_t)c >= 0x20 && (uint8_t)c <= 0x7E) {
        idx = (uint8_t)c - 0x20;
    } else {
        idx = 0;
    }
    const uint8_t *bitmap = font8x16[idx];

    /*
     * 与 write_char 相同，需要对字库的水平行排列做 8×8 位转置，
     * 转换为 LCD 页模式要求的垂直列排列。
     * 详见 write_char 中的注释。
     */
    uint8_t col_byte;

    /* 上半部分：页 y，列 x ~ x+7 */
    uint16_t top_base = (uint16_t)y * 128 + x;
    for (int col = 0; col < 8; col++) {
        col_byte = 0;
        for (int row = 0; row < 8; row++) {
            if (bitmap[row] & (1 << (7 - col))) {
                col_byte |= (1 << row);
            }
        }
        handle->framebuffer[top_base + col] = col_byte;
    }

    /* 下半部分：页 y+1，列 x ~ x+7 */
    uint16_t bot_base = (uint16_t)(y + 1) * 128 + x;
    for (int col = 0; col < 8; col++) {
        col_byte = 0;
        for (int row = 0; row < 8; row++) {
            if (bitmap[row + 8] & (1 << (7 - col))) {
                col_byte |= (1 << row);
            }
        }
        handle->framebuffer[bot_base + col] = col_byte;
    }
}

/**
 * @brief 在指定位置绘制字符串
 *
 * 从 (x, y) 开始依次调用 putc 绘制每个字符，
 * 字符间距为 8 像素。超出右边界后自动换行：
 * x 归零，y 加 2（因字符高度 16 像素 / 每页 8 像素 = 2 页）。
 * 若超出底部边界则停止绘制。
 */
void lcd12864_puts(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, const char *str) {
    if (!handle || !str) return;

    while (*str) {
        lcd12864_putc(handle, x, y, *str);

        /* 光标右移一个字符宽度 */
        x += 8;

        /* 若剩余空间不足 8 像素，换到下一行 */
        if (x + 8 > 128) {
            x = 0;
            y += 2;       /* 下移两页（16 像素） */
            if (y >= 8) {
                break;    /* 超出屏幕底部，停止绘制 */
            }
        }

        str++;
    }
}

/**
 * @brief 获取互斥锁
 *
 * 用于保护帧缓冲区和 SPI 通信的原子性。
 * 调用 xSemaphoreTake 时会阻塞当前任务直到获得锁，
 * portMAX_DELAY 表示无限等待。
 * 典型用法：
 *   lcd12864_lock(lcd);
 *   lcd12864_clear(lcd);
 *   lcd12864_puts(lcd, 0, 0, "Hello");
 *   lcd12864_flush(lcd);
 *   lcd12864_unlock(lcd);
 */
void lcd12864_lock(lcd12864_ctx_t *handle) {
    if (!handle || !handle->mutex) return;
    /*
     * xSemaphoreTake：获取互斥锁。
     * 若锁已被其他任务持有，当前任务进入阻塞态，
     * 直到锁被释放或超时（这里使用 portMAX_DELAY，永不超时）。
     * 返回 pdTRUE 表示成功获取锁。
     */
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
}

/**
 * @brief 释放互斥锁
 */
void lcd12864_unlock(lcd12864_ctx_t *handle) {
    if (!handle || !handle->mutex) return;
    /*
     * xSemaphoreGive：释放互斥锁。
     * 如果有其他任务正在等待此锁，FreeRTOS 会
     * 在调度点将锁交给最高优先级的等待任务。
     */
    xSemaphoreGive(handle->mutex);
}