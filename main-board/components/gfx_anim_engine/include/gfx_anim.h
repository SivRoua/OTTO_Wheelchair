#ifndef GFX_ANIM_H
#define GFX_ANIM_H

#include <stdint.h>
#include <stdbool.h>
#include "gfx_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 精灵结构体 ---------- */
typedef struct {
    int16_t x, y;               /* 当前位置（左上角） */
    int16_t prev_x, prev_y;     /* 上一帧位置（用于擦除） */
    uint8_t w, h;               /* 精灵尺寸（像素） */
    const uint8_t **bitmaps;    /* 动画帧位图数组（每帧指向一列行式单色数据） */
    uint8_t num_frames;         /* 总帧数 */
    uint8_t current_frame;      /* 当前显示的帧索引 */
    bool visible;               /* 是否绘制 */
    int16_t dx, dy;             /* 每帧移动增量（速度） */
    uint8_t frame_ticks;        /* 每隔多少引擎 tick 切换一帧（0 = 不自动切帧） */
    uint8_t _tick_counter;      /* 内部计数器，勿手动修改 */
    bool    frame_loop;         /* 播完最后一帧后是否循环回第 0 帧 */
} gfx_anim_sprite_t;

/* ---------- 动画引擎句柄（不透明） ---------- */
typedef struct gfx_anim_engine gfx_anim_engine_t;

/* ---------- 引擎生命周期 ---------- */

/**
 * @brief 创建动画引擎
 * @param ui         已初始化的 gfx_ui 上下文
 * @param max_sprites 最大精灵数量（数组预分配）
 * @param fps        目标帧率（如 10 表示每秒 10 帧）
 * @return 引擎句柄，失败返回 NULL
 */
gfx_anim_engine_t *gfx_anim_engine_create(gfx_ui_ctx_t *ui, uint8_t max_sprites, uint8_t fps);

/**
 * @brief 销毁动画引擎，释放所有资源
 */
void gfx_anim_engine_destroy(gfx_anim_engine_t *engine);

/* ---------- 精灵管理 ---------- */

/**
 * @brief 添加一个精灵（拷贝传入的配置）
 * @param engine 引擎句柄
 * @param sprite 精灵初始值（位图数组、帧数等必须已设置好）
 * @return 成功返回精灵在内部数组中的索引（>=0），失败返回 -1
 */
int8_t gfx_anim_sprite_add(gfx_anim_engine_t *engine, const gfx_anim_sprite_t *sprite);

/**
 * @brief 移除索引处的精灵（后续索引顺移）
 */
void gfx_anim_sprite_remove(gfx_anim_engine_t *engine, uint8_t index);

/**
 * @brief 获取精灵数量
 */
uint8_t gfx_anim_sprite_count(gfx_anim_engine_t *engine);

/**
 * @brief 获取指定精灵的指针（可直接修改其字段）
 * @return NULL 若索引无效
 */
gfx_anim_sprite_t *gfx_anim_sprite_get(gfx_anim_engine_t *engine, uint8_t index);

/* ---------- 帧循环控制 ---------- */

/**
 * @brief 启动动画（创建并启动定时器）
 */
void gfx_anim_engine_start(gfx_anim_engine_t *engine);

/**
 * @brief 停止动画（删除定时器，但保留精灵状态）
 */
void gfx_anim_engine_stop(gfx_anim_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* GFX_ANIM_H */