#pragma once

#include "gfx_menu.h"
#include "gps.h"
#include "LoRa.h"
#include <stdbool.h>
#include <stdint.h>

/* ----------------------------------------------------------------
 * Page IDs
 * ---------------------------------------------------------------- */
#define PAGE_BOOT      0
#define PAGE_MAIN      1
#define PAGE_MENU      2
#define PAGE_BACKREST  3
#define PAGE_PRESSURE  4
#define PAGE_LORA_CFG  5

/* ----------------------------------------------------------------
 * 全局共享句柄 — 定义在 main.c，各页面文件通过此头访问
 * ---------------------------------------------------------------- */
extern gfx_menu_engine_t *s_menu;
extern lora_ctx_t        *s_lora;
extern gps_ctx_t         *s_gps;
extern bool               s_lora_ok;

/* ----------------------------------------------------------------
 * 本地时间 — GPS Sync 后由 main.c 维护，Status Bar 只读
 * hour=0xFF 表示尚未同步，显示 "--:--"
 * ---------------------------------------------------------------- */
extern uint8_t s_time_hour;
extern uint8_t s_time_min;
extern bool    s_gps_located;

