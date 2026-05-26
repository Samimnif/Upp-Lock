#ifndef CONTROLLER_UART_H_
#define CONTROLLER_UART_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/kernel.h>

int controller_uart_init(void);

void controller_uart_send(const char *msg);

int controller_uart_wait_line(char *buffer,
                              uint32_t max_len,
                              k_timeout_t timeout_ms);

#endif