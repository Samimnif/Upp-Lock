#include "controller_uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#define UART_LINE_MAX_LEN 64
#define UART_MSGQ_DEPTH   8

K_MSGQ_DEFINE(controller_uart_msgq,
              UART_LINE_MAX_LEN,
              UART_MSGQ_DEPTH,
              4);

static const struct device *uart_dev;

static char isr_line_buffer[UART_LINE_MAX_LEN];
static volatile uint32_t isr_index;

static void uart_cb(const struct device *dev,
                    void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    if (!uart_irq_rx_ready(dev)) {
        return;
    }

    uint8_t c;

    while (uart_fifo_read(dev,
                          &c,
                          1) == 1) {

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {

            isr_line_buffer[isr_index] = '\0';

            k_msgq_put(&controller_uart_msgq,
                       isr_line_buffer,
                       K_NO_WAIT);

            isr_index = 0;
            continue;
        }

        if (isr_index < UART_LINE_MAX_LEN - 1) {
            isr_line_buffer[isr_index++] = c;
        }
    }
}

int controller_uart_init(void)
{
    uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart1));

    if (!device_is_ready(uart_dev)) {
        return -1;
    }

    uart_irq_callback_user_data_set(uart_dev,
                                    uart_cb,
                                    NULL);

    uart_irq_rx_enable(uart_dev);

    return 0;
}

void controller_uart_send(const char *msg)
{
    if (msg == NULL) {
        return;
    }

    while (*msg) {
        uart_poll_out(uart_dev,
                      *msg++);
    }
}

int controller_uart_wait_line(char *buffer,
                              uint32_t max_len,
                              int32_t timeout_ms)
{
    if (buffer == NULL) {
        return -1;
    }

    char temp[UART_LINE_MAX_LEN];

    int ret = k_msgq_get(&controller_uart_msgq,
                         temp,
                         K_MSEC(timeout_ms));

    if (ret != 0) {
        return ret;
    }

    strncpy(buffer,
            temp,
            max_len - 1);

    buffer[max_len - 1] = '\0';

    return 0;
}