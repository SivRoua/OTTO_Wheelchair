#ifndef GFX_MENU_H
#define GFX_MENU_H

#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include "gfx_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * 输入事件
 * ---------------------------------------------------------------- */
typedef enum {
    GFX_INPUT_UP = 0,
    GFX_INPUT_DOWN,
    GFX_INPUT_ENTER,
    GFX_INPUT_BACK,
} gfx_input_t;

/* ----------------------------------------------------------------
 * 菜单项（供内置列表页面助手使用）
 * ---------------------------------------------------------------- */
typedef struct {
    const char *label;
    void (*action)(void);
} gfx_menu_item_t;

/* ----------------------------------------------------------------
 * 前向声明
 * ---------------------------------------------------------------- */
typedef struct gfx_menu_engine gfx_menu_engine_t;

/* ----------------------------------------------------------------
 * 页面描述符 — 架构核心
 * 所有回调均可为 NULL（表示无操作）。
 * ---------------------------------------------------------------- */
typedef struct {
    /* 页面变为活跃时调用一次（push 进入或 pop 返回） */
    void (*on_enter)(gfx_menu_engine_t *menu, void *user_data);

    /* 由 gfx_menu_render() 调用，负责绘制页面内容 */
    void (*on_render)(gfx_menu_engine_t *menu, void *user_data);

    /* 由 gfx_menu_input() 调用，处理按键事件 */
    void (*on_input)(gfx_menu_engine_t *menu, gfx_input_t event, void *user_data);

    /* 页面即将离开时调用一次（push 离开或 pop 弹出） */
    void (*on_exit)(gfx_menu_engine_t *menu, void *user_data);

    /* 由 gfx_menu_tick() 每次调用，用于定时逻辑（自动跳转、闪烁等） */
    void (*on_tick)(gfx_menu_engine_t *menu, void *user_data);

    /* 传递给所有回调的用户数据指针，必须指向静态或堆内存 */
    void *user_data;
} gfx_page_desc_t;

/* ----------------------------------------------------------------
 * 页面注册表与导航栈容量
 * ---------------------------------------------------------------- */
#define GFX_PAGE_REGISTRY_MAX  16
#define GFX_NAV_STACK_DEPTH     8
#define GFX_PAGE_ID_NONE     0xFF   /* 空槽哨兵值 */

/* ----------------------------------------------------------------
 * 引擎生命周期
 * ---------------------------------------------------------------- */
gfx_menu_engine_t *gfx_menu_create(gfx_ui_ctx_t *ui);
void               gfx_menu_destroy(gfx_menu_engine_t *menu);

/* ----------------------------------------------------------------
 * 页面注册
 * page_id: 0 ~ GFX_PAGE_REGISTRY_MAX-1
 * 描述符按值拷贝，user_data 指针由调用方管理生命周期。
 * ---------------------------------------------------------------- */
bool gfx_menu_register_page(gfx_menu_engine_t *menu,
                             uint8_t page_id,
                             const gfx_page_desc_t *desc);

/* ----------------------------------------------------------------
 * 导航 — 基于栈
 *
 * push: 压入新页面，触发前进过渡动画（2帧左→右擦除）。
 *       nav_depth==0 时为初始压栈，跳过过渡和 on_exit。
 * pop:  弹出当前页面，触发后退过渡动画（2帧右→左擦除）。
 *       nav_depth<=1 时返回 false（不能弹出根页面）。
 *
 * 注意：push/pop 内部调用 vTaskDelay，只能从任务上下文调用。
 * ---------------------------------------------------------------- */
bool    gfx_menu_push(gfx_menu_engine_t *menu, uint8_t page_id);
bool    gfx_menu_pop(gfx_menu_engine_t *menu);
uint8_t gfx_menu_current_page(const gfx_menu_engine_t *menu);
uint8_t gfx_menu_stack_depth(const gfx_menu_engine_t *menu);

/* ----------------------------------------------------------------
 * 输入分发 — 路由到当前页面的 on_input 回调
 * ---------------------------------------------------------------- */
void gfx_menu_input(gfx_menu_engine_t *menu, gfx_input_t event);

/* ----------------------------------------------------------------
 * 渲染与时钟
 * ---------------------------------------------------------------- */
/* 在任务循环中周期性调用（建议 ~50ms）：
 *   1. 调用当前页面的 on_tick（如有）
 *   2. 若 dirty 标志置位，调用 gfx_menu_render */
void gfx_menu_tick(gfx_menu_engine_t *menu);

/* 强制立即重绘当前页面 */
void gfx_menu_render(gfx_menu_engine_t *menu);

/* 获取引擎持有的 UI 上下文（供页面回调使用） */
gfx_ui_ctx_t *gfx_menu_get_ui(gfx_menu_engine_t *menu);

/* 标记当前页面需要重绘（在 on_input/on_tick 内调用） */
void gfx_menu_set_dirty(gfx_menu_engine_t *menu);

/* ----------------------------------------------------------------
 * 内置页面助手 — 开机画面
 * ---------------------------------------------------------------- */
typedef struct {
    const uint8_t *logo;
    uint16_t logo_w, logo_h;
    const char *title;
    const char *subtitle;
    uint32_t duration_ms;    /* 自动跳转等待时间，0 = 不自动跳转 */
    uint8_t  next_page_id;   /* duration_ms > 0 时跳转的目标页面 */
    TickType_t _boot_start;  /* 内部使用，on_enter 写入，勿手动修改 */
} gfx_boot_cfg_t;

/* 用 cfg 填充 desc 的所有回调字段，cfg 生命周期须长于引擎 */
void gfx_page_boot_init(gfx_page_desc_t *desc, gfx_boot_cfg_t *cfg);

/* ----------------------------------------------------------------
 * 内置页面助手 — 列表菜单
 * ---------------------------------------------------------------- */
#define GFX_LIST_MAX_ITEMS     16
#define GFX_LIST_VISIBLE_ROWS   5

typedef struct {
    const char *title;
    const gfx_menu_item_t *items;
    uint8_t item_count;
    uint8_t selected;    /* 当前高亮项，由 on_input 维护 */
    uint8_t scroll_top;  /* 滚动偏移，由 on_input 维护 */
    /* BACK 键回调：NULL = 默认调用 gfx_menu_pop */
    void (*on_back)(gfx_menu_engine_t *menu, void *user_data);
} gfx_list_cfg_t;

/* 用 cfg 填充 desc 的所有回调字段，cfg 生命周期须长于引擎 */
void gfx_page_list_init(gfx_page_desc_t *desc, gfx_list_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* GFX_MENU_H */
