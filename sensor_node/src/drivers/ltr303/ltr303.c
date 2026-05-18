#include "ltr303.h"

#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

static int write_reg(ltr303_t *dev, uint8_t reg, uint8_t value)
{
    if (dev == NULL || dev->i2c_bus == NULL) {
        return -EINVAL;
    }

    return i2c_reg_write_byte(dev->i2c_bus,
                              dev->i2c_addr,
                              reg,
                              value);
}

static int read_reg(ltr303_t *dev,
                    uint8_t reg,
                    uint8_t *value)
{
    if (dev == NULL || dev->i2c_bus == NULL || value == NULL) {
        return -EINVAL;
    }

    return i2c_reg_read_byte(dev->i2c_bus,
                             dev->i2c_addr,
                             reg,
                             value);
}

int ltr303_init(ltr303_t *dev,
                const struct device *i2c_bus,
                uint8_t i2c_addr)
{
    if (dev == NULL || i2c_bus == NULL) {
        return -EINVAL;
    }

    if (!device_is_ready(i2c_bus)) {
        return -ENODEV;
    }

    dev->i2c_bus = i2c_bus;
    dev->i2c_addr = i2c_addr;

    /* Enable active mode */
    int ret = write_reg(dev,
                        LTR303_REG_CONTR,
                        LTR303_MODE_ACTIVE);
    if (ret != 0) {
        return ret;
    }

    k_msleep(100);

    uint8_t part_id = 0;
    ret = read_reg(dev,
                   LTR303_REG_PART_ID,
                   &part_id);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

int ltr303_read_raw(ltr303_t *dev,
                    ltr303_raw_data_t *raw)
{
    if (dev == NULL || dev->i2c_bus == NULL || raw == NULL) {
        return -EINVAL;
    }

    uint8_t buffer[4];

    int ret = i2c_burst_read(dev->i2c_bus,
                             dev->i2c_addr,
                             LTR303_REG_DATA_CH1_0,
                             buffer,
                             sizeof(buffer));
    if (ret != 0) {
        return ret;
    }

    raw->ch1_ir_raw =
        ((uint16_t)buffer[1] << 8) | buffer[0];

    raw->ch0_visible_ir_raw =
        ((uint16_t)buffer[3] << 8) | buffer[2];

    return 0;
}