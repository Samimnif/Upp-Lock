#ifndef MPU6050_H_
#define MPU6050_H_

#include <stdint.h>
#include <zephyr/device.h>

#include "mpu6050_reg.h"

typedef enum {
    MPU6050_ACCEL_RANGE_2G  = 0,
    MPU6050_ACCEL_RANGE_4G  = 1,
    MPU6050_ACCEL_RANGE_8G  = 2,
    MPU6050_ACCEL_RANGE_16G = 3,
} mpu6050_accel_range_t;

typedef enum {
    MPU6050_GYRO_RANGE_250DPS  = 0,
    MPU6050_GYRO_RANGE_500DPS  = 1,
    MPU6050_GYRO_RANGE_1000DPS = 2,
    MPU6050_GYRO_RANGE_2000DPS = 3,
} mpu6050_gyro_range_t;

typedef struct {
    const struct device *i2c_bus;
    uint8_t i2c_addr;
    mpu6050_accel_range_t accel_range;
    mpu6050_gyro_range_t gyro_range;
} mpu6050_t;

typedef struct {
    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;
    int16_t temp_raw;
    int16_t gyro_x_raw;
    int16_t gyro_y_raw;
    int16_t gyro_z_raw;
} mpu6050_raw_data_t;

typedef struct {
    double accel_x_g;
    double accel_y_g;
    double accel_z_g;
    double temperature_c;
    double gyro_x_dps;
    double gyro_y_dps;
    double gyro_z_dps;
} mpu6050_data_t;

int mpu6050_init(mpu6050_t *dev,
                 const struct device *i2c_bus,
                 uint8_t i2c_addr);

int mpu6050_set_accel_range(mpu6050_t *dev, mpu6050_accel_range_t range);
int mpu6050_set_gyro_range(mpu6050_t *dev, mpu6050_gyro_range_t range);

int mpu6050_read_who_am_i(mpu6050_t *dev, uint8_t *id);
int mpu6050_read_raw(mpu6050_t *dev, mpu6050_raw_data_t *raw);
int mpu6050_read(mpu6050_t *dev, mpu6050_data_t *data);

double mpu6050_accel_lsb_per_g(mpu6050_accel_range_t range);
double mpu6050_gyro_lsb_per_dps(mpu6050_gyro_range_t range);
double mpu6050_temp_to_celsius(int16_t temp_raw);

#endif /* MPU6050_H_ */