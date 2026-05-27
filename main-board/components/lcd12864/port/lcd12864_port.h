#ifndef LCD12864_PORT_H
#define LCD12864_PORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 端口层配置：由核心层转换自 lcd12864_config_t，供端口实现使用。
 */
typedef struct {
    int sclk;
    int sda;
    int rs;
    int cs;
    int reset;
    uint32_t freq_hz;
} lcd12864_port_config_t;

/*
 * 端口层操作表：平台需要提供的硬件访问函数指针。
 */
typedef struct {
    void (*init)(const lcd12864_port_config_t *cfg);
    void (*reset)(bool level);
    void (*delay_ms)(uint32_t ms);
    void (*write_cmd)(uint8_t cmd);
    void (*write_data)(uint8_t data);
    void (*write_data_bulk)(const uint8_t *data, uint32_t len);
    void (*deinit)(void);
} lcd12864_port_ops_t;

extern const lcd12864_port_ops_t lcd12864_port_ops;

#ifdef __cplusplus
}
#endif

#endif /* LCD12864_PORT_H */