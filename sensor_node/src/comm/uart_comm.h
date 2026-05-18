#ifndef UART_COMM_H_
#define UART_COMM_H_

#include <stdint.h>

int uart_comm_init(void);

void uart_comm_send(const char *msg);

int uart_comm_wait_line(char *buffer,
                        uint32_t max_len,
                        int32_t timeout_ms);

#endif