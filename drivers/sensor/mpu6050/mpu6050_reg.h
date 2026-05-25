#ifndef MPU6050_REG_H_
#define MPU6050_REG_H_

/* MPU6050 I2C addresses */
#define MPU6050_I2C_ADDR_0          0x68  /* AD0 pin low/GND */
#define MPU6050_I2C_ADDR_1          0x69  /* AD0 pin high/VCC */

/* Register map */
#define MPU6050_REG_SMPLRT_DIV      0x19
#define MPU6050_REG_CONFIG          0x1A
#define MPU6050_REG_GYRO_CONFIG     0x1B
#define MPU6050_REG_ACCEL_CONFIG    0x1C
#define MPU6050_REG_INT_ENABLE      0x38
#define MPU6050_REG_ACCEL_XOUT_H    0x3B
#define MPU6050_REG_ACCEL_XOUT_L    0x3C
#define MPU6050_REG_ACCEL_YOUT_H    0x3D
#define MPU6050_REG_ACCEL_YOUT_L    0x3E
#define MPU6050_REG_ACCEL_ZOUT_H    0x3F
#define MPU6050_REG_ACCEL_ZOUT_L    0x40
#define MPU6050_REG_TEMP_OUT_H      0x41
#define MPU6050_REG_TEMP_OUT_L      0x42
#define MPU6050_REG_GYRO_XOUT_H     0x43
#define MPU6050_REG_GYRO_XOUT_L     0x44
#define MPU6050_REG_GYRO_YOUT_H     0x45
#define MPU6050_REG_GYRO_YOUT_L     0x46
#define MPU6050_REG_GYRO_ZOUT_H     0x47
#define MPU6050_REG_GYRO_ZOUT_L     0x48
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_PWR_MGMT_2      0x6C
#define MPU6050_REG_WHO_AM_I        0x75

/* Register values / masks */
#define MPU6050_WHO_AM_I_VALUE      0x68
#define MPU6050_PWR_WAKEUP          0x00
#define MPU6050_PWR_RESET           0x80

#define MPU6050_DLPF_CFG_44HZ       0x03
#define MPU6050_SMPLRT_DIV_1KHZ_125HZ 0x07

#define MPU6050_ACCEL_FS_SHIFT      3
#define MPU6050_GYRO_FS_SHIFT       3

#endif /* MPU6050_REG_H_ */
