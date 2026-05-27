#include <stdio.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_USB_DEVICE_STACK)
#include <zephyr/usb/usb_device.h>
#endif

#include "controller_uart.h"

#define RX_THREAD_STACK_SIZE 1024
#define RX_THREAD_PRIORITY   5

static void rx_thread(void)
{
    char line[64];

    while (1) {
        if (controller_uart_wait_line(line, sizeof(line), K_FOREVER) == 0) {
            printf("Controller RX <- %s\n", line);
        }
    }
}

K_THREAD_DEFINE(rx_thread_id,
                RX_THREAD_STACK_SIZE,
                rx_thread,
                NULL,
                NULL,
                NULL,
                RX_THREAD_PRIORITY,
                0,
                0);

int main(void)
{
#if defined(CONFIG_USB_DEVICE_STACK)
    if (usb_enable(NULL) != 0) {
        return 0;
    }

    /* Give the host time to enumerate /dev/ttyACM*. */
    k_sleep(K_SECONDS(2));
#endif

    if (controller_uart_init() != 0) {
        printf("Controller UART init failed\n");
        return 0;
    }

    printf("Controller node ready\n");
    printf("Sending PING every 1 second on UART1 GP4/GP5\n");

    while (1) {
        controller_uart_send("PING\n");
        printf("Controller TX -> PING\n");
        k_msleep(1000);
    }
}
