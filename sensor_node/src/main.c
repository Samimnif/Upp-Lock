#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "comm/uart_comm.h"
// #include "drivers/ltr303/ltr303.h"

// ltr303_t g_ltr303;

void main(void)
{
    // const struct device *i2c_dev =
    //     DEVICE_DT_GET(DT_NODELABEL(i2c0));

    if (uart_comm_init() != 0) {

        printf("UART init failed\n");
        return;
    }

    // if (ltr303_init(&g_ltr303,
    //                 i2c_dev,
    //                 LTR303_I2C_ADDR) != 0) {

    //     printf("LTR303 init failed\n");
    //     return;
    // }

    printf("Sensor node ready\n");

    while (1) {

        k_msleep(1000);
    }
}