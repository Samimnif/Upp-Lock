#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ltr303_reg.h"

LOG_MODULE_REGISTER(ltr303, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT liteon_ltr303

struct ltr303_config {
    struct i2c_dt_spec i2c;
};

struct ltr303_data {
    uint16_t ch0_raw;
    uint16_t ch1_raw;
};

static int ltr303_write_reg(const struct device *dev,
                            uint8_t reg,
                            uint8_t value)
{
    const struct ltr303_config *cfg = dev->config;

    return i2c_reg_write_byte_dt(&cfg->i2c, reg, value);
}

static int ltr303_read_reg(const struct device *dev,
                           uint8_t reg,
                           uint8_t *value)
{
    const struct ltr303_config *cfg = dev->config;

    return i2c_reg_read_byte_dt(&cfg->i2c, reg, value);
}

static int ltr303_sample_fetch(const struct device *dev,
                               enum sensor_channel chan)
{
    const struct ltr303_config *cfg = dev->config;
    struct ltr303_data *data = dev->data;

    uint8_t buffer[4];
    int ret;

    if (chan != SENSOR_CHAN_ALL &&
        chan != SENSOR_CHAN_LIGHT) {
        return -ENOTSUP;
    }

    ret = i2c_burst_read_dt(&cfg->i2c,
                            LTR303_REG_DATA_CH1_0,
                            buffer,
                            sizeof(buffer));

    if (ret != 0) {
        LOG_ERR("failed to read sample: %d", ret);
        return ret;
    }

    data->ch1_raw = ((uint16_t)buffer[1] << 8) | buffer[0];
    data->ch0_raw = ((uint16_t)buffer[3] << 8) | buffer[2];

    return 0;
}

static int ltr303_channel_get(const struct device *dev,
                              enum sensor_channel chan,
                              struct sensor_value *val)
{
    struct ltr303_data *data = dev->data;

    switch (chan) {
    case SENSOR_CHAN_LIGHT:
        /*
         * Placeholder lux conversion.
         * Replace with official Lite-On lux equation.
         */
        val->val1 = data->ch0_raw;
        val->val2 = 0;
        return 0;

    default:
        return -ENOTSUP;
    }
}


static int ltr303_init(const struct device *dev)
{
    const struct ltr303_config *cfg = dev->config;

    uint8_t part_id = 0;
    int ret;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    ret = ltr303_write_reg(dev,
                           LTR303_REG_CONTR,
                           LTR303_MODE_ACTIVE);
    if (ret != 0) {
        LOG_ERR("failed to enable sensor: %d", ret);
        return ret;
    }

    k_msleep(100);

    ret = ltr303_read_reg(dev,
                          LTR303_REG_PART_ID,
                          &part_id);
    if (ret != 0) {
        LOG_ERR("failed to read part id: %d", ret);
        return ret;
    }

    LOG_INF("LTR303 detected: part_id=0x%02x", part_id);

    return 0;
}

static const struct sensor_driver_api ltr303_api = {
    .sample_fetch = ltr303_sample_fetch,
    .channel_get = ltr303_channel_get,
};

#define LTR303_INIT(inst)                                        \
    static struct ltr303_data ltr303_data_##inst;                \
                                                                  \
    static const struct ltr303_config ltr303_config_##inst = {   \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                        \
    };                                                            \
                                                                  \
    SENSOR_DEVICE_DT_INST_DEFINE(inst,                            \
                                 ltr303_init,                     \
                                 NULL,                            \
                                 &ltr303_data_##inst,             \
                                 &ltr303_config_##inst,           \
                                 POST_KERNEL,                     \
                                 CONFIG_SENSOR_INIT_PRIORITY,     \
                                 &ltr303_api);

DT_INST_FOREACH_STATUS_OKAY(LTR303_INIT)