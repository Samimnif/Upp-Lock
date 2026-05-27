#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "uart_comm.h"

#define CMD_THREAD_STACK_SIZE 1024
#define CMD_THREAD_PRIORITY   5

static void command_thread(void)
{
    /* Block here until uart_comm_init() has registered the ISR and
       enabled RX interrupts. Without this, bytes arriving before init
       are silently dropped and the msgq is never populated. */
    k_sem_take(&uart_comm_ready, K_FOREVER);

    char line[64];

    while (1) {
        if (uart_comm_wait_line(line, sizeof(line), K_FOREVER) != 0) {
            continue;
        }

        printf("Sensor RX <- %s\n", line);

        if (strcmp(line, "PING") == 0) {
            uart_comm_send("PONG\n");
            printf("Sensor TX -> PONG\n");
            continue;
        }

        uart_comm_send("UNKNOWN\n");
        printf("Sensor TX -> UNKNOWN\n");
    }
}

K_THREAD_DEFINE(command_thread_id,
                CMD_THREAD_STACK_SIZE,
                command_thread,
                NULL,
                NULL,
                NULL,
                CMD_THREAD_PRIORITY,
                0,
                0);
