#include "mpu6050.h"

#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

static int16_t bytes_to_int16(uint8_t msb, uint8_t lsb)
{
    return (int16_t)(((uint16_t)msb << 8) | lsb);
}

static int write_reg(mpu6050_t *dev, uint8_t reg, uint8_t value)
{
    if (dev == NULL || dev->i2c_bus == NULL) {
        return -EINVAL;
    }

    return i2c_reg_write_byte(dev->i2c_bus, dev->i2c_addr, reg, value);
}

static int read_reg(mpu6050_t *dev, uint8_t reg, uint8_t *value)
{
    if (dev == NULL || dev->i2c_bus == NULL || value == NULL) {
        return -EINVAL;
    }

    return i2c_reg_read_byte(dev->i2c_bus, dev->i2c_addr, reg, value);
}

int mpu6050_read_who_am_i(mpu6050_t *dev, uint8_t *id)
{
    return read_reg(dev, MPU6050_REG_WHO_AM_I, id);
}

int mpu6050_set_accel_range(mpu6050_t *dev, mpu6050_accel_range_t range)
{
    uint8_t value = ((uint8_t)range << MPU6050_ACCEL_FS_SHIFT);
    int ret = write_reg(dev, MPU6050_REG_ACCEL_CONFIG, value);

    if (ret == 0) {
        dev->accel_range = range;
    }

    return ret;
}

int mpu6050_set_gyro_range(mpu6050_t *dev, mpu6050_gyro_range_t range)
{
    uint8_t value = ((uint8_t)range << MPU6050_GYRO_FS_SHIFT);
    int ret = write_reg(dev, MPU6050_REG_GYRO_CONFIG, value);

    if (ret == 0) {
        dev->gyro_range = range;
    }

    return ret;
}

int mpu6050_init(mpu6050_t *dev,
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
    dev->accel_range = MPU6050_ACCEL_RANGE_2G;
    dev->gyro_range = MPU6050_GYRO_RANGE_250DPS;

    /* Wake up the MPU6050. By default it starts in sleep mode. */
    int ret = write_reg(dev, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_WAKEUP);
    if (ret != 0) {
        return ret;
    }

    k_msleep(100);

    uint8_t who_am_i = 0;
    ret = mpu6050_read_who_am_i(dev, &who_am_i);
    if (ret != 0) {
        return ret;
    }

    if (who_am_i != MPU6050_WHO_AM_I_VALUE) {
        return -EIO;
    }

    ret = write_reg(dev, MPU6050_REG_SMPLRT_DIV, MPU6050_SMPLRT_DIV_1KHZ_125HZ);
    if (ret != 0) {
        return ret;
    }

    ret = write_reg(dev, MPU6050_REG_CONFIG, MPU6050_DLPF_CFG_44HZ);
    if (ret != 0) {
        return ret;
    }

    ret = mpu6050_set_accel_range(dev, MPU6050_ACCEL_RANGE_2G);
    if (ret != 0) {
        return ret;
    }

    ret = mpu6050_set_gyro_range(dev, MPU6050_GYRO_RANGE_250DPS);
    if (ret != 0) {
        return ret;
    }

    return write_reg(dev, MPU6050_REG_INT_ENABLE, 0x00);
}

int mpu6050_read_raw(mpu6050_t *dev, mpu6050_raw_data_t *raw)
{
    if (dev == NULL || dev->i2c_bus == NULL || raw == NULL) {
        return -EINVAL;
    }

    uint8_t buffer[14];
    int ret = i2c_burst_read(dev->i2c_bus,
                             dev->i2c_addr,
                             MPU6050_REG_ACCEL_XOUT_H,
                             buffer,
                             sizeof(buffer));
    if (ret != 0) {
        return ret;
    }

    raw->accel_x_raw = bytes_to_int16(buffer[0], buffer[1]);
    raw->accel_y_raw = bytes_to_int16(buffer[2], buffer[3]);
    raw->accel_z_raw = bytes_to_int16(buffer[4], buffer[5]);
    raw->temp_raw    = bytes_to_int16(buffer[6], buffer[7]);
    raw->gyro_x_raw  = bytes_to_int16(buffer[8], buffer[9]);
    raw->gyro_y_raw  = bytes_to_int16(buffer[10], buffer[11]);
    raw->gyro_z_raw  = bytes_to_int16(buffer[12], buffer[13]);

    return 0;
}

int mpu6050_read(mpu6050_t *dev, mpu6050_data_t *data)
{
    if (dev == NULL || data == NULL) {
        return -EINVAL;
    }

    mpu6050_raw_data_t raw;
    int ret = mpu6050_read_raw(dev, &raw);
    if (ret != 0) {
        return ret;
    }

    double accel_scale = mpu6050_accel_lsb_per_g(dev->accel_range);
    double gyro_scale = mpu6050_gyro_lsb_per_dps(dev->gyro_range);

    data->accel_x_g = raw.accel_x_raw / accel_scale;
    data->accel_y_g = raw.accel_y_raw / accel_scale;
    data->accel_z_g = raw.accel_z_raw / accel_scale;
    data->temperature_c = mpu6050_temp_to_celsius(raw.temp_raw);
    data->gyro_x_dps = raw.gyro_x_raw / gyro_scale;
    data->gyro_y_dps = raw.gyro_y_raw / gyro_scale;
    data->gyro_z_dps = raw.gyro_z_raw / gyro_scale;

    return 0;
}

double mpu6050_accel_lsb_per_g(mpu6050_accel_range_t range)
{
    switch (range) {
    case MPU6050_ACCEL_RANGE_2G:
        return 16384.0;
    case MPU6050_ACCEL_RANGE_4G:
        return 8192.0;
    case MPU6050_ACCEL_RANGE_8G:
        return 4096.0;
    case MPU6050_ACCEL_RANGE_16G:
        return 2048.0;
    default:
        return 16384.0;
    }
}

double mpu6050_gyro_lsb_per_dps(mpu6050_gyro_range_t range)
{
    switch (range) {
    case MPU6050_GYRO_RANGE_250DPS:
        return 131.0;
    case MPU6050_GYRO_RANGE_500DPS:
        return 65.5;
    case MPU6050_GYRO_RANGE_1000DPS:
        return 32.8;
    case MPU6050_GYRO_RANGE_2000DPS:
        return 16.4;
    default:
        return 131.0;
    }
}

double mpu6050_temp_to_celsius(int16_t temp_raw)
{
    return ((double)temp_raw / 340.0) + 36.53;
}