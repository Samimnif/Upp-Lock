#include <stdio.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>

#include "../drivers/sensor/buzzer/buzzer.h"

#define MPU6050_NODE DT_ALIAS(mpu6050)
#define LTR303_NODE DT_ALIAS(ltr303)
#define RGB_LED_NODE DT_ALIAS(led_strip)
#define BUZZER_NODE DT_ALIAS(buzzer)
#define SS49E_NODE  DT_NODELABEL(my_ss49e)

#if !DT_NODE_EXISTS(MPU6050_NODE)
#define MPU6050_NODE DT_NODELABEL(mpu6050)
#endif

#if !DT_NODE_EXISTS(LTR303_NODE)
#define LTR303_NODE DT_NODELABEL(ltr303)
#endif

static void print_sensor_value(const struct sensor_value *val)
{
    printk("%d.%06d", val->val1, abs(val->val2));
}

static int rgb_set(const struct device *rgb, uint8_t r, uint8_t g, uint8_t b)
{
    struct led_rgb pixel = {
        .r = r,
        .g = g,
        .b = b,
    };

    return led_strip_update_rgb(rgb, &pixel, 1);
}

int main(void)
{
    const struct device *imu = DEVICE_DT_GET(MPU6050_NODE);
    const struct device *ltr = DEVICE_DT_GET_ANY(liteon_ltr303);
    const struct device *rgb = DEVICE_DT_GET(RGB_LED_NODE);
    const struct device *buzzer = DEVICE_DT_GET(BUZZER_NODE);
    const struct device *ss49e   = DEVICE_DT_GET(SS49E_NODE);

    struct sensor_value accel[3];
    struct sensor_value gyro[3];
    struct sensor_value temp;
    struct sensor_value gauss_val;
    struct sensor_value lux;
    int ret;

    printk("MPU6050 Zephyr Sensor API demo starting...\n");

    if (!device_is_ready(imu))
    {
        printk("MPU6050 device is not ready\n");
        return 0;
    }

    if (!device_is_ready(ltr)) {
        return 0;
    }

    bool rgb_ready = device_is_ready(rgb);
    if (!rgb_ready)
    {
        printk("RGB LED strip device is not ready\n");
    }

    if (!device_is_ready(buzzer))
    {
        printk("Buzzer device is not ready\n");
        return 0;
    }

    /* Startup indication: green flash + short beep */
    if (rgb_ready)
    {
        rgb_set(rgb, 0, 255, 0);
    }

    buzzer_beep(buzzer, 2000, 150);
    k_sleep(K_MSEC(150));
    if (rgb_ready)
    {
        rgb_set(rgb, 0, 255, 255);
    }

    while (1)
    {
        ret = sensor_sample_fetch(imu);
        if (ret != 0)
        {
            printk("sensor_sample_fetch failed: %d\n", ret);

            /* Error indication: red flash + beep */
            rgb_set(rgb, 255, 0, 0);
            buzzer_beep(buzzer, 2000, 50);
            k_sleep(K_MSEC(100));
            rgb_set(rgb, 0, 0, 0);

            k_sleep(K_SECONDS(1));
            continue;
        }

        sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
        sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro);
        sensor_channel_get(imu, SENSOR_CHAN_DIE_TEMP, &temp);

        printk("ACC [m/s^2] x=");
        print_sensor_value(&accel[0]);
        printk(" y=");
        print_sensor_value(&accel[1]);
        printk(" z=");
        print_sensor_value(&accel[2]);
        printk("\n");

        printk("GYRO [rad/s] x=");
        print_sensor_value(&gyro[0]);
        printk(" y=");
        print_sensor_value(&gyro[1]);
        printk(" z=");
        print_sensor_value(&gyro[2]);
        printk("\n");

        printk("TEMP [C] ");
        print_sensor_value(&temp);
        printk("\n\n");



        /* SS49E */
        ret = sensor_sample_fetch(ss49e);
        if (ret == 0) {
            ret = sensor_channel_get(ss49e, SENSOR_CHAN_MAGN_XYZ, &gauss_val);
            if (ret == 0) {
                printk("MAGN [Gauss] %d.%06d\n",
                       gauss_val.val1, abs(gauss_val.val2));
            }
        } else {
            printk("SS49E fetch failed: %d\n", ret);
        }

        printk("\n");

        /* LTR */
        sensor_sample_fetch(ltr);
        sensor_channel_get(ltr, SENSOR_CHAN_LIGHT, &lux);
        printk("lux: %d\n", lux.val1);

        /* Normal indication: dim blue blink */
        rgb_set(rgb, 0, 0, 40);
        k_sleep(K_MSEC(100));
        rgb_set(rgb, 0, 0, 0);

        k_sleep(K_SECONDS(1));
    }
}