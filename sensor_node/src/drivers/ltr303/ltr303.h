#ifndef LTR303_H_
#define LTR303_H_

#include <stdint.h>
#include <zephyr/device.h>

#include "ltr303_reg.h"

typedef struct {
    const struct device *i2c_bus;
    uint8_t i2c_addr;
} ltr303_t;

typedef struct {
    uint16_t ch1_ir_raw;
    uint16_t ch0_visible_ir_raw;
} ltr303_raw_data_t;

int ltr303_init(ltr303_t *dev,
                const struct device *i2c_bus,
                uint8_t i2c_addr);

int ltr303_read_raw(ltr303_t *dev,
                    ltr303_raw_data_t *raw);

#endif /* LTR303_H_ */