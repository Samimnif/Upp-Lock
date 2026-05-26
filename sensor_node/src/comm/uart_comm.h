#ifndef UART_COMM_H_
#define UART_COMM_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/kernel.h>

int uart_comm_init(void);

void uart_comm_send(const char *msg);

int uart_comm_wait_line(char *buffer,
                        size_t max_len,
                        k_timeout_t timeout_ms);

#endif