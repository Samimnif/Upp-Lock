#include "command_handler.h"
#include "uart_comm.h"

#include <stdio.h>
#include <string.h>

// #include "drivers/ltr303/ltr303.h"

// extern ltr303_t g_ltr303;

void command_handler_process(void)
{
    char line[64];

    if (uart_comm_wait_line(line, sizeof(line), K_NO_WAIT) != 0) {
        /* No message available — nothing to process. */
        return;
    }

    if (strcmp(line, "PING") == 0) {
        uart_comm_send("PONG\n");
        return;
    }

    /*
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

        return;
    }
    */

    /* 
    if (strcmp(line, "READ_ALL") == 0) {
    }
    */

    uart_comm_send("UNKNOWN\n");
}
