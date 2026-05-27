#include "gfx_anim.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* ---------- 动画引擎内部结构 ---------- */
struct gfx_anim_engine {
    gfx_ui_ctx_t *ui;
    gfx_anim_sprite_t *sprites; /* 精灵数组 */
    uint8_t max_sprites;
    uint8_t sprite_count;
    TimerHandle_t timer;        /* FreeRTOS 软件定时器 */
    uint32_t period_ms;         /* 帧周期（毫秒） */
};

/* ---------- 内部辅助：帧更新回调 ---------- */
static void anim_timer_callback(TimerHandle_t xTimer) {
    gfx_anim_engine_t *engine = (gfx_anim_engine_t *)pvTimerGetTimerID(xTimer);
    if (!engine || !engine->ui) return;

    gfx_ui_lock(engine->ui);

    /* 遍历精灵，擦除旧位置 */
    for (uint8_t i = 0; i < engine->sprite_count; i++) {
        gfx_anim_sprite_t *s = &engine->sprites[i];
        if (!s->visible) continue;
        /* 擦除上一帧占据的矩形（填充黑色） */
        gfx_ui_clear_rect(engine->ui, s->prev_x, s->prev_y, s->w, s->h);
    }

    /* 更新精灵状态并绘制新位置 */
    for (uint8_t i = 0; i < engine->sprite_count; i++) {
        gfx_anim_sprite_t *s = &engine->sprites[i];
        if (!s->visible) continue;

        /* 保存旧位置（用于下一帧擦除） */
        s->prev_x = s->x;
        s->prev_y = s->y;

        /* 自动移动 */
        s->x += s->dx;
        s->y += s->dy;

        /* 绘制当前帧 */
        if (s->bitmaps && s->current_frame < s->num_frames) {
            const uint8_t *bitmap = s->bitmaps[s->current_frame];
            gfx_ui_blit(engine->ui, s->x, s->y, s->w, s->h, bitmap, GFX_COLOR_WHITE, false);
        }

        /* 自动切帧 */
        if (s->frame_ticks > 0 && s->num_frames > 1) {
            s->_tick_counter++;
            if (s->_tick_counter >= s->frame_ticks) {
                s->_tick_counter = 0;
                if (s->current_frame + 1 < s->num_frames) {
                    s->current_frame++;
                } else if (s->frame_loop) {
                    s->current_frame = 0;
                }
            }
        }
    }

    /* 刷新到屏幕 */
    gfx_ui_flush(engine->ui);
    gfx_ui_unlock(engine->ui);
}

/* ================================================================
 * 公开 API 实现
 * ================================================================ */

gfx_anim_engine_t *gfx_anim_engine_create(gfx_ui_ctx_t *ui, uint8_t max_sprites, uint8_t fps) {
    if (!ui || max_sprites == 0 || fps == 0) return NULL;

    gfx_anim_engine_t *engine = (gfx_anim_engine_t *)calloc(1, sizeof(gfx_anim_engine_t));
    if (!engine) return NULL;

    engine->ui = ui;
    engine->max_sprites = max_sprites;
    engine->sprite_count = 0;
    engine->period_ms = 1000 / fps;

    engine->sprites = (gfx_anim_sprite_t *)calloc(max_sprites, sizeof(gfx_anim_sprite_t));
    if (!engine->sprites) {
        free(engine);
        return NULL;
    }

    /* 创建定时器，但不启动（由 start 控制） */
    engine->timer = xTimerCreate(
        "animTimer",
        pdMS_TO_TICKS(engine->period_ms),
        pdTRUE,                     /* 自动重载 */
        (void *)engine,             /* 定时器 ID */
        anim_timer_callback
    );
    if (!engine->timer) {
        free(engine->sprites);
        free(engine);
        return NULL;
    }

    return engine;
}

void gfx_anim_engine_destroy(gfx_anim_engine_t *engine) {
    if (!engine) return;
    if (engine->timer) {
        xTimerDelete(engine->timer, portMAX_DELAY);
    }
    free(engine->sprites);
    free(engine);
}

int8_t gfx_anim_sprite_add(gfx_anim_engine_t *engine, const gfx_anim_sprite_t *sprite) {
    if (!engine || !sprite || engine->sprite_count >= engine->max_sprites) return -1;
    /* 拷贝精灵数据 */
    engine->sprites[engine->sprite_count] = *sprite;
    /* 初始化 prev 为当前位置，避免第一帧错误擦除 */
    engine->sprites[engine->sprite_count].prev_x = sprite->x;
    engine->sprites[engine->sprite_count].prev_y = sprite->y;
    return engine->sprite_count++;
}

void gfx_anim_sprite_remove(gfx_anim_engine_t *engine, uint8_t index) {
    if (!engine || index >= engine->sprite_count) return;
    /* 将后续元素前移 */
    if (index < engine->sprite_count - 1) {
        memmove(&engine->sprites[index], &engine->sprites[index + 1],
                (engine->sprite_count - 1 - index) * sizeof(gfx_anim_sprite_t));
    }
    engine->sprite_count--;
}

uint8_t gfx_anim_sprite_count(gfx_anim_engine_t *engine) {
    return engine ? engine->sprite_count : 0;
}

gfx_anim_sprite_t *gfx_anim_sprite_get(gfx_anim_engine_t *engine, uint8_t index) {
    if (!engine || index >= engine->sprite_count) return NULL;
    return &engine->sprites[index];
}

void gfx_anim_engine_start(gfx_anim_engine_t *engine) {
    if (engine && engine->timer) {
        xTimerStart(engine->timer, 0);
    }
}

void gfx_anim_engine_stop(gfx_anim_engine_t *engine) {
    if (engine && engine->timer) {
        xTimerStop(engine->timer, 0);
    }
}