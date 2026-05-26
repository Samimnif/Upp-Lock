#include <stdio.h>

#include <zephyr/kernel.h>

#include "controller_uart.h"

#define RX_THREAD_STACK_SIZE 1024
#define RX_THREAD_PRIORITY   5

static void rx_thread(void)
{
    char line[64];

    while (1) {

        if (controller_uart_wait_line(line,
                                      sizeof(line),
                                      K_FOREVER) == 0) {

            printf("RX -> %s\n",
                   line);
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

void main(void)
{
    if (controller_uart_init() != 0) {

        printf("UART init failed\n");
        return;
    }

    printf("Controller node ready\n");

    while (1) {

        controller_uart_send("PING\n");
        printf("PING SENT!\n");

        k_msleep(1000);

        // controller_uart_send("READ_LTR\n");
        // k_msleep(2000);
    }
}