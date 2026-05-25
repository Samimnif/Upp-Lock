#include <stdio.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include "../drivers/sensor/buzzer/buzzer.h"

#define MPU6050_NODE DT_ALIAS(mpu6050)
#define RGB_LED_NODE DT_ALIAS(led_strip)
#define BUZZER_NODE DT_ALIAS(buzzer)
#define DISPLAY_NODE DT_NODELABEL(gc9a01)
#define LCD_BL_NODE DT_ALIAS(display_bl)

#define LCD_W 240
#define LCD_H 240
#define LCD_CENTER_X 120
#define LCD_CENTER_Y 120
#define LCD_SCREEN_R 119
#define BALL_R 10

static uint16_t lcd_fb[LCD_W * LCD_H];

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return sys_cpu_to_le16(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static int sensor_value_to_milli_local(const struct sensor_value *v)
{
    return v->val1 * 1000 + v->val2 / 1000;
}

static void lcd_draw_ball(const struct device *display, int ball_x, int ball_y)
{
    const uint16_t black = rgb565(0, 0, 0);
    const uint16_t dim_ring = rgb565(20, 20, 20);
    const uint16_t blue = rgb565(0, 120, 255);

    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            int dx = x - LCD_CENTER_X;
            int dy = y - LCD_CENTER_Y;
            int d2 = dx * dx + dy * dy;

            /* Mask outside the physical round screen area. */
            if (d2 > LCD_SCREEN_R * LCD_SCREEN_R) {
                lcd_fb[y * LCD_W + x] = black;
                continue;
            }

            /* Faint circular border so you can see the screen boundary. */
            if (d2 > (LCD_SCREEN_R - 2) * (LCD_SCREEN_R - 2)) {
                lcd_fb[y * LCD_W + x] = dim_ring;
                continue;
            }

            int bx = x - ball_x;
            int by = y - ball_y;
            if ((bx * bx + by * by) <= BALL_R * BALL_R) {
                lcd_fb[y * LCD_W + x] = blue;
            } else {
                lcd_fb[y * LCD_W + x] = black;
            }
        }
    }

    struct display_buffer_descriptor desc = {
        .buf_size = sizeof(lcd_fb),
        .width = LCD_W,
        .height = LCD_H,
        .pitch = LCD_W,
    };

    display_write(display, 0, 0, &desc, lcd_fb);
}

static int clamp_int(int v, int min, int max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}


#if !DT_NODE_EXISTS(MPU6050_NODE)
#define MPU6050_NODE DT_NODELABEL(mpu6050)
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
    const struct device *rgb = DEVICE_DT_GET(RGB_LED_NODE);
    const struct device *buzzer = DEVICE_DT_GET(BUZZER_NODE);
    const struct device *display = DEVICE_DT_GET(DISPLAY_NODE);
    static const struct gpio_dt_spec lcd_bl = GPIO_DT_SPEC_GET(LCD_BL_NODE, gpios);

    struct sensor_value accel[3];
    struct sensor_value gyro[3];
    struct sensor_value temp;
    int ret;

    printk("MPU6050 Zephyr Sensor API demo starting...\n");

    if (!device_is_ready(imu))
    {
        printk("MPU6050 device is not ready\n");
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

    if (!device_is_ready(display))
    {
        printk("GC9A01 display is not ready\n");
        return 0;
    }

    if (!gpio_is_ready_dt(&lcd_bl))
    {
        printk("LCD backlight GPIO is not ready\n");
        return 0;
    }

    gpio_pin_configure_dt(&lcd_bl, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&lcd_bl, 1);

    display_blanking_off(display);
    lcd_draw_ball(display, LCD_CENTER_X, LCD_CENTER_Y);

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

        /* Map +/- 1g tilt to movement on the round display.
         * MPU values are m/s^2, so 9806 milli ~= 1g.
         */
        int ax_m = sensor_value_to_milli_local(&accel[0]);
        int ay_m = sensor_value_to_milli_local(&accel[1]);
        int ball_x = LCD_CENTER_X + (ax_m * 80 / 9806);
        int ball_y = LCD_CENTER_Y - (ay_m * 80 / 9806);

        ball_x = clamp_int(ball_x, LCD_CENTER_X - 90, LCD_CENTER_X + 90);
        ball_y = clamp_int(ball_y, LCD_CENTER_Y - 90, LCD_CENTER_Y + 90);
        lcd_draw_ball(display, ball_x, ball_y);

        /* Normal indication: dim blue blink */
        rgb_set(rgb, 0, 0, 40);
        k_sleep(K_MSEC(100));
        rgb_set(rgb, 0, 0, 0);

        k_sleep(K_MSEC(50));
    }
}