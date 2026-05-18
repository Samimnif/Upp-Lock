#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "comm/uart_comm.h"
#include "drivers/ltr303/ltr303.h"

extern ltr303_t g_ltr303;

#define CMD_THREAD_STACK_SIZE 1024
#define CMD_THREAD_PRIORITY   5

static void command_thread(void)
{
    char line[64];

    while (1) {

        if (uart_comm_wait_line(line,
                                sizeof(line),
                                K_FOREVER) != 0) {
            continue;
        }

        if (strcmp(line, "PING") == 0) {

            uart_comm_send("PONG\n");
            continue;
        }

        if (strcmp(line, "READ_LTR") == 0) {

            ltr303_raw_data_t raw;

            if (ltr303_read_raw(&g_ltr303,
                                &raw) == 0) {

                char tx[64];

                snprintf(tx,
                         sizeof(tx),
                         "LTR,%u,%u\n",
                         raw.ch1_ir_raw,
                         raw.ch0_visible_ir_raw);

                uart_comm_send(tx);

            } else {

                uart_comm_send("ERR\n");
            }

            continue;
        }

        uart_comm_send("UNKNOWN\n");
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