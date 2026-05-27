/*
 * sensor_node/src/main.c
 *
 * Sensor node — Raspberry Pi Pico 2 (RP2350)
 * Listens on UART1 for commands from the base node and responds.
 * Simulates a temperature/humidity sensor (replace with real driver).
 *
 * Wiring (Base ↔ Sensor):
 *   Sensor GP8  (UART1 TX) → Base GP5  (UART1 RX)
 *   Sensor GP9  (UART1 RX) ← Base GP4  (UART1 TX)
 *   GND ↔ GND
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <string.h>
#include "../../common/protocol.h"

/* ── Config ────────────────────────────────────────────── */
#define COMMS_UART_NODE  DT_NODELABEL(uart1)

/* ── Simulated sensor state (replace with real driver) ─── */
static struct {
    int16_t  temp_c100;   /* Temperature in °C × 100  e.g. 2350 = 23.50°C */
    uint16_t hum_p100;    /* Humidity in % × 100      e.g. 5500 = 55.00%  */
    uint16_t interval_ms; /* Auto-poll interval (informational) */
} sensor = {
    .temp_c100   = 2350,
    .hum_p100    = 5500,
    .interval_ms = 1000,
};

/* ── Globals ───────────────────────────────────────────── */
static const struct device *comms_uart;

/* Raw RX ring buffer */
#define RX_BUF_SIZE  64
static uint8_t rx_buf[RX_BUF_SIZE];
static volatile size_t rx_head = 0;
static volatile size_t rx_tail = 0;

/* ── UART ISR callback ─────────────────────────────────── */
static void uart_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) return;

    while (uart_irq_rx_ready(dev)) {
        uint8_t c;
        int n = uart_fifo_read(dev, &c, 1);
        if (n > 0) {
            size_t next = (rx_head + 1) % RX_BUF_SIZE;
            if (next != rx_tail) {   /* drop if full */
                rx_buf[rx_head] = c;
                rx_head = next;
            }
        }
    }
}

static bool rx_byte_available(void) { return rx_head != rx_tail; }

static uint8_t rx_get_byte(void)
{
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

/* 
static void uart_send_bytes(const uint8_t *buf, size_t len)
{
    uart_fifo_fill(comms_uart, buf, (int)len);
}

*/

static void uart_send_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(comms_uart, buf[i]);
    }
}

/* ── Sensor read (replace with real driver calls) ──────── */
static bool sensor_read_temp(int16_t *out_temp)
{
    /* Simulate a small drift */
    sensor.temp_c100 += (int16_t)(k_uptime_get() & 0x3) - 1;
    *out_temp = sensor.temp_c100;
    return true;
}

static bool sensor_read_humidity(uint16_t *out_hum)
{
    sensor.hum_p100 += (uint16_t)(k_uptime_get() & 0x1);
    if (sensor.hum_p100 > 9999) sensor.hum_p100 = 9999;
    *out_hum = sensor.hum_p100;
    return true;
}

/* ── Response helpers ──────────────────────────────────── */

static void send_ack(void)
{
    proto_frame_t f = { .stx = PROTO_STX, .cmd = RSP_ACK, .len = 0 };
    uint8_t buf[4];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);
}

static void send_nack(void)
{
    proto_frame_t f = { .stx = PROTO_STX, .cmd = RSP_NACK, .len = 0 };
    uint8_t buf[4];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);
}

static void send_error(proto_err_t err)
{
    proto_frame_t f = {
        .stx = PROTO_STX, .cmd = RSP_ERROR,
        .len = 1, .payload = { (uint8_t)err }
    };
    uint8_t buf[5];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);
}

static void send_temp(void)
{
    int16_t t;
    if (!sensor_read_temp(&t)) { send_error(ERR_SENSOR_FAULT); return; }
    proto_frame_t f = { .stx = PROTO_STX, .cmd = RSP_TEMP, .len = 2 };
    proto_put_i16(&f, t);
    uint8_t buf[6];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);
}

static void send_humidity(void)
{
    uint16_t h;
    if (!sensor_read_humidity(&h)) { send_error(ERR_SENSOR_FAULT); return; }
    proto_frame_t f = { .stx = PROTO_STX, .cmd = RSP_HUMIDITY, .len = 2 };
    proto_put_u16(&f, h);
    uint8_t buf[6];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);
}

static void send_all(void)
{
    int16_t  t;
    uint16_t h;
    if (!sensor_read_temp(&t) || !sensor_read_humidity(&h)) {
        send_error(ERR_SENSOR_FAULT);
        return;
    }
    proto_frame_t f = {
        .stx = PROTO_STX, .cmd = RSP_ALL, .len = 4,
        .payload = {
            (uint8_t)(t >> 8), (uint8_t)(t & 0xFF),
            (uint8_t)(h >> 8), (uint8_t)(h & 0xFF),
        }
    };
    uint8_t buf[8];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);
    printk("[SENSOR] Sent: Temp %d.%02d°C  Hum %u.%02u%%\n",
           t / 100, t % 100, h / 100, h % 100);
}

/* ── Frame receiver state machine ──────────────────────── */
typedef enum {
    RX_STATE_IDLE,
    RX_STATE_CMD,
    RX_STATE_LEN,
    RX_STATE_PAYLOAD,
    RX_STATE_CRC,
} rx_state_t;

static void process_rx(void)
{
    static rx_state_t  state   = RX_STATE_IDLE;
    static proto_frame_t frame;
    static uint8_t     pay_idx = 0;

    while (rx_byte_available()) {
        uint8_t c = rx_get_byte();

        switch (state) {
        case RX_STATE_IDLE:
            if (c == PROTO_STX) {
                frame.stx = c;
                state = RX_STATE_CMD;
            }
            break;

        case RX_STATE_CMD:
            frame.cmd = c;
            state = RX_STATE_LEN;
            break;

        case RX_STATE_LEN:
            if (c > PROTO_MAX_PAYLOAD) {
                /* Invalid length — reset */
                state = RX_STATE_IDLE;
                break;
            }
            frame.len = c;
            pay_idx   = 0;
            state     = (c > 0) ? RX_STATE_PAYLOAD : RX_STATE_CRC;
            break;

        case RX_STATE_PAYLOAD:
            frame.payload[pay_idx++] = c;
            if (pay_idx >= frame.len) {
                state = RX_STATE_CRC;
            }
            break;

        case RX_STATE_CRC:
            frame.crc = c;
            state = RX_STATE_IDLE;

            /* Validate CRC */
            if (frame.crc != proto_frame_crc(&frame)) {
                printk("[SENSOR] Bad CRC — dropping frame\n");
                send_error(ERR_BAD_CRC);
                break;
            }

            /* Dispatch command */
            printk("[SENSOR] CMD 0x%02X\n", frame.cmd);
            switch ((proto_cmd_t)frame.cmd) {
            case CMD_PING:
                send_ack();
                break;
            case CMD_READ_TEMP:
                send_temp();
                break;
            case CMD_READ_HUMIDITY:
                send_humidity();
                break;
            case CMD_READ_ALL:
                send_all();
                break;
            case CMD_SET_INTERVAL:
                if (frame.len == 2) {
                    sensor.interval_ms = proto_get_u16(&frame);
                    printk("[SENSOR] Interval set to %u ms\n", sensor.interval_ms);
                    send_ack();
                } else {
                    send_nack();
                }
                break;
            case CMD_RESET:
                printk("[SENSOR] Resetting...\n");
                k_sleep(K_MSEC(10));
                sys_reboot(SYS_REBOOT_COLD);
                break;
            default:
                printk("[SENSOR] Unknown cmd 0x%02X\n", frame.cmd);
                send_error(ERR_UNKNOWN_CMD);
                break;
            }
            break;
        }
    }
}

/* ── Entry point ───────────────────────────────────────── */

int main(void)
{
    printk("=== Sensor Node (RP2350) starting ===\n");

    comms_uart = DEVICE_DT_GET(COMMS_UART_NODE);
    if (!device_is_ready(comms_uart)) {
        printk("[SENSOR] UART not ready!\n");
        return -1;
    }

    /* Enable interrupt-driven RX */
    uart_irq_callback_set(comms_uart, uart_cb);
    uart_irq_rx_enable(comms_uart);

    printk("[SENSOR] Listening for commands...\n");

    while (1) {
        process_rx();
        k_sleep(K_USEC(500));
    }

    return 0;
}
