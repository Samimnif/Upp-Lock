#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "mpu6050_reg.h"

LOG_MODULE_REGISTER(mpu6050_custom, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT invensense_mpu6050_custom

enum mpu6050_accel_range
{
    MPU6050_ACCEL_RANGE_2G = 0,
    MPU6050_ACCEL_RANGE_4G = 1,
    MPU6050_ACCEL_RANGE_8G = 2,
    MPU6050_ACCEL_RANGE_16G = 3,
};

enum mpu6050_gyro_range
{
    MPU6050_GYRO_RANGE_250DPS = 0,
    MPU6050_GYRO_RANGE_500DPS = 1,
    MPU6050_GYRO_RANGE_1000DPS = 2,
    MPU6050_GYRO_RANGE_2000DPS = 3,
};

struct mpu6050_config
{
    struct i2c_dt_spec i2c;
    uint8_t accel_range;
    uint8_t gyro_range;
};

struct mpu6050_data
{
    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;
    int16_t temp_raw;
    int16_t gyro_x_raw;
    int16_t gyro_y_raw;
    int16_t gyro_z_raw;
};

static int16_t bytes_to_int16(uint8_t msb, uint8_t lsb)
{
    return (int16_t)(((uint16_t)msb << 8) | lsb);
}

static int mpu6050_write_reg(const struct device *dev, uint8_t reg, uint8_t value)
{
    const struct mpu6050_config *cfg = dev->config;

    return i2c_reg_write_byte_dt(&cfg->i2c, reg, value);
}

static int mpu6050_read_reg(const struct device *dev, uint8_t reg, uint8_t *value)
{
    const struct mpu6050_config *cfg = dev->config;

    return i2c_reg_read_byte_dt(&cfg->i2c, reg, value);
}

static int mpu6050_set_accel_range(const struct device *dev, uint8_t range)
{
    return mpu6050_write_reg(dev, MPU6050_REG_ACCEL_CONFIG,
                             range << MPU6050_ACCEL_FS_SHIFT);
}

static int mpu6050_set_gyro_range(const struct device *dev, uint8_t range)
{
    return mpu6050_write_reg(dev, MPU6050_REG_GYRO_CONFIG,
                             range << MPU6050_GYRO_FS_SHIFT);
}

static int mpu6050_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    const struct mpu6050_config *cfg = dev->config;
    struct mpu6050_data *data = dev->data;
    uint8_t buffer[14];
    int ret;

    if (chan != SENSOR_CHAN_ALL &&
        chan != SENSOR_CHAN_ACCEL_XYZ &&
        chan != SENSOR_CHAN_GYRO_XYZ &&
        chan != SENSOR_CHAN_DIE_TEMP)
    {
        return -ENOTSUP;
    }

    ret = i2c_burst_read_dt(&cfg->i2c, MPU6050_REG_ACCEL_XOUT_H,
                            buffer, sizeof(buffer));
    if (ret != 0)
    {
        LOG_ERR("failed to read sensor sample: %d", ret);
        return ret;
    }

    data->accel_x_raw = bytes_to_int16(buffer[0], buffer[1]);
    data->accel_y_raw = bytes_to_int16(buffer[2], buffer[3]);
    data->accel_z_raw = bytes_to_int16(buffer[4], buffer[5]);
    data->temp_raw = bytes_to_int16(buffer[6], buffer[7]);
    data->gyro_x_raw = bytes_to_int16(buffer[8], buffer[9]);
    data->gyro_y_raw = bytes_to_int16(buffer[10], buffer[11]);
    data->gyro_z_raw = bytes_to_int16(buffer[12], buffer[13]);

    return 0;
}

static int64_t accel_lsb_per_ms2(uint8_t range)
{
    switch (range)
    {
    case MPU6050_ACCEL_RANGE_2G:
        return 16384;
    case MPU6050_ACCEL_RANGE_4G:
        return 8192;
    case MPU6050_ACCEL_RANGE_8G:
        return 4096;
    case MPU6050_ACCEL_RANGE_16G:
        return 2048;
    default:
        return 16384;
    }
}

static int64_t gyro_lsb_per_rad_s(uint8_t range)
{
    switch (range)
    {
    case MPU6050_GYRO_RANGE_250DPS:
        return 131;
    case MPU6050_GYRO_RANGE_500DPS:
        return 655; /* 65.5 * 10 */
    case MPU6050_GYRO_RANGE_1000DPS:
        return 328; /* 32.8 * 10 */
    case MPU6050_GYRO_RANGE_2000DPS:
        return 164; /* 16.4 * 10 */
    default:
        return 131;
    }
}

static void accel_convert(struct sensor_value *val, int16_t raw, uint8_t range)
{
    int64_t micro_ms2 = (int64_t)raw * SENSOR_G / accel_lsb_per_ms2(range);

    val->val1 = micro_ms2 / 1000000;
    val->val2 = micro_ms2 % 1000000;
}

static void gyro_convert(struct sensor_value *val, int16_t raw, uint8_t range)
{
    int64_t micro_rad_s;

    if (range == MPU6050_GYRO_RANGE_250DPS)
    {
        micro_rad_s = (int64_t)raw * SENSOR_PI / (131 * 180);
    }
    else
    {
        /* Ranges above 250 dps use one decimal in gyro_lsb_per_rad_s(). */
        micro_rad_s = (int64_t)raw * SENSOR_PI * 10 / (gyro_lsb_per_rad_s(range) * 180);
    }

    val->val1 = micro_rad_s / 1000000;
    val->val2 = micro_rad_s % 1000000;
}

static void temp_convert(struct sensor_value *val, int16_t raw)
{
    int64_t micro_c = ((int64_t)raw * 1000000 / 340) + 36530000;

    val->val1 = micro_c / 1000000;
    val->val2 = micro_c % 1000000;
}

static int mpu6050_channel_get(const struct device *dev,
                               enum sensor_channel chan,
                               struct sensor_value *val)
{
    const struct mpu6050_config *cfg = dev->config;
    struct mpu6050_data *data = dev->data;

    switch (chan)
    {
    case SENSOR_CHAN_ACCEL_XYZ:
        accel_convert(&val[0], data->accel_x_raw, cfg->accel_range);
        accel_convert(&val[1], data->accel_y_raw, cfg->accel_range);
        accel_convert(&val[2], data->accel_z_raw, cfg->accel_range);
        return 0;

    case SENSOR_CHAN_GYRO_XYZ:
        gyro_convert(&val[0], data->gyro_x_raw, cfg->gyro_range);
        gyro_convert(&val[1], data->gyro_y_raw, cfg->gyro_range);
        gyro_convert(&val[2], data->gyro_z_raw, cfg->gyro_range);
        return 0;

    case SENSOR_CHAN_DIE_TEMP:
        temp_convert(val, data->temp_raw);
        return 0;

    default:
        return -ENOTSUP;
    }
}

static int mpu6050_init(const struct device *dev)
{
    const struct mpu6050_config *cfg = dev->config;
    uint8_t who_am_i = 0;
    int ret;

    if (!i2c_is_ready_dt(&cfg->i2c))
    {
        LOG_ERR("I2C bus is not ready");
        return -ENODEV;
    }

    ret = mpu6050_write_reg(dev, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_WAKEUP);
    if (ret != 0)
    {
        return ret;
    }

    k_msleep(100);

    ret = mpu6050_read_reg(dev, MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != 0)
    {
        return ret;
    }

    if (who_am_i != MPU6050_WHO_AM_I_VALUE)
    {
        LOG_ERR("unexpected WHO_AM_I: 0x%02x", who_am_i);
        return -EIO;
    }

    ret = mpu6050_write_reg(dev, MPU6050_REG_SMPLRT_DIV,
                            MPU6050_SMPLRT_DIV_1KHZ_125HZ);
    if (ret != 0)
    {
        return ret;
    }

    ret = mpu6050_write_reg(dev, MPU6050_REG_CONFIG, MPU6050_DLPF_CFG_44HZ);
    if (ret != 0)
    {
        return ret;
    }

    ret = mpu6050_set_accel_range(dev, cfg->accel_range);
    if (ret != 0)
    {
        return ret;
    }

    ret = mpu6050_set_gyro_range(dev, cfg->gyro_range);
    if (ret != 0)
    {
        return ret;
    }

    ret = mpu6050_write_reg(dev, MPU6050_REG_INT_ENABLE, 0x00);
    if (ret != 0)
    {
        return ret;
    }

    LOG_INF("MPU6050 initialized");
    return 0;
}

static const struct sensor_driver_api mpu6050_api = {
    .sample_fetch = mpu6050_sample_fetch,
    .channel_get = mpu6050_channel_get,
};

#define MPU6050_INIT(inst)                                       \
    static struct mpu6050_data mpu6050_data_##inst;              \
                                                                 \
    static const struct mpu6050_config mpu6050_config_##inst = { \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                       \
        .accel_range = DT_INST_PROP(inst, accel_range),          \
        .gyro_range = DT_INST_PROP(inst, gyro_range),            \
    };                                                           \
                                                                 \
    SENSOR_DEVICE_DT_INST_DEFINE(inst,                           \
                                 mpu6050_init,                   \
                                 NULL,                           \
                                 &mpu6050_data_##inst,           \
                                 &mpu6050_config_##inst,         \
                                 POST_KERNEL,                    \
                                 CONFIG_SENSOR_INIT_PRIORITY,    \
                                 &mpu6050_api);

DT_INST_FOREACH_STATUS_OKAY(MPU6050_INIT)
