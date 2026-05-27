#ifndef UART_COMM_H_
#define UART_COMM_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/kernel.h>

/* Semaphore posted by uart_comm_init() once the driver is ready.
   command_thread must take this before touching the message queue. */
extern struct k_sem uart_comm_ready;

int uart_comm_init(void);

void uart_comm_send(const char *msg);

int uart_comm_wait_line(char *buffer,
                        size_t max_len,
                        k_timeout_t timeout_ms);

#endif
