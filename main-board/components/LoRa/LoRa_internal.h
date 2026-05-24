#ifndef LORA_INTERNAL_H
#define LORA_INTERNAL_H

/*
 * LoRa 模块内部宏定义
 * ================================================================
 * 存放驱动核心使用的常量定义。
 * 参考 README.md 第四章节 "AT 命令详解"。
 */

/* ================================================================
 * 1. AUX 等待参数（Phase 1）
 * ================================================================ */
#define LORA_AUX_TIMEOUT_MS         2000
#define LORA_AUX_POLL_MS           1

/* ================================================================
 * 2. AT 指令控制串（Phase 2）
 * ================================================================ */
#define LORA_AT_ENTER_CMD           "+++\r\n"
#define LORA_AT_EXIT_CMD            "+++\r\n"
#define LORA_AT_CRLF                "\r\n"

#define LORA_AT_RESP_ENTRY          "Entry AT"
#define LORA_AT_RESP_EXIT           "Exit AT"
#define LORA_AT_RESP_OK             "OK"
#define LORA_AT_RESP_ERROR          "ERROR"

/* ================================================================
 * 3. AT 超时参数
 * ================================================================ */
#define LORA_AT_ENTRY_TIMEOUT_MS    1000
#define LORA_AT_CMD_TIMEOUT_MS      2000
#define LORA_AT_EXIT_TIMEOUT_MS     2000
#define LORA_AT_RESP_BUF_SIZE       256
#define LORA_AT_POLL_MS            10

/* ================================================================
 * 4. Phase 3-4：查询 / 配置指令
 * ================================================================
 *
 * 参考 README.md 4.2 节：
 *   查询指令：AT+CMD? 返回 +CMD=<value>
 *   设置指令：AT+CMD=<value>
 */

#define AT_CMD_HELP                 "AT+HELP"
#define AT_CMD_ERSSI                "AT+ERSSI"

#define AT_CMD_LEVEL                "AT+LEVEL="
#define AT_CMD_CHANNEL              "AT+CHANNEL="
#define AT_CMD_POWER                "AT+POWE="
#define AT_CMD_MODE                 "AT+MODE="
#define AT_CMD_SLEEP                "AT+SLEEP="
#define AT_CMD_PACKET               "AT+PACKET="
#define AT_CMD_DRSSI                "AT+DRSSI="
#define AT_CMD_OPENKEY              "AT+OPENKEY="
#define AT_CMD_KEY                  "AT+KEY="
#define AT_CMD_MAC                  "AT+MAC="
#define AT_CMD_BAUD                 "AT+BAUD="

#define AT_QUERY_SUFFIX             "?"

#define AT_CMD_RESET                "AT+RESET"
#define AT_CMD_DEFAULT              "AT+DEFAULT"

#define AT_CMD_SEND                 "AT+SEND="
#define AT_CMD_RECV                 "AT+RECV"

#define FIXED_HEADER_LEN            3

/* ================================================================
 * 5. 数据收发参数（Phase 5-6）
 * ================================================================ */
#define LORA_MAX_PACKET_LEN         230
#define LORA_TX_TIMEOUT_MS          1000
#define LORA_RX_POLL_MS            10

#endif /* LORA_INTERNAL_H */
