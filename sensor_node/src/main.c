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

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <string.h>
#include "../../common/protocol.h"
#include <zephyr/drivers/sensor.h>

#include "../../drivers/sensor/ltr303/ltr303.h"


/* ── Config ────────────────────────────────────────────── */
#define COMMS_UART_NODE  DT_NODELABEL(uart1)
#define LTR303_I2C_NODE DT_NODELABEL(i2c1)
#define LTR303_I2C_ADDR 0x29

static const struct device *mpu6050 = DEVICE_DT_GET(DT_NODELABEL(mpu6050));
static const struct device *ss49e = DEVICE_DT_GET(DT_NODELABEL(ss49e));

//Check the threshold level
#define LIGHT_THRESHOLD 100

/* ── Globals ───────────────────────────────────────────── */
static const struct device *comms_uart;

// Note: need to add the DR instead of adding it like this directly
static ltr303_t ltr303;
static bool ltr303_ready = false;

static bool sensor_read_light(uint16_t *visible, uint16_t *ir);

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


static void uart_send_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(comms_uart, buf[i]);
    }
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


static void send_all(void)
{
    uint16_t visible;
    uint16_t ir;

    if (!sensor_read_light(&visible, &ir)) {
        send_error(ERR_SENSOR_FAULT);
        return;
    }

    struct sensor_value accel[3];

    int ret = sensor_sample_fetch(mpu6050);
    if (ret != 0) {
        printk("[SENSOR] MPU fetch failed: %d\n", ret);
        send_error(ERR_SENSOR_FAULT);
        return;
    }

    ret = sensor_channel_get(mpu6050, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (ret != 0) {
        printk("[SENSOR] MPU channel get failed: %d\n", ret);
        send_error(ERR_SENSOR_FAULT);
        return;
    }

    struct sensor_value hall;

    ret = sensor_sample_fetch(ss49e);
    if (ret != 0) {
        printk("[SENSOR] SS49E fetch failed: %d\n", ret);
        send_error(ERR_SENSOR_FAULT);
        return;
    }

    ret = sensor_channel_get(ss49e, SENSOR_CHAN_MAGN_X, &hall);
    if (ret != 0) {
        printk("[SENSOR] SS49E channel get failed: %d\n", ret);
        send_error(ERR_SENSOR_FAULT);
        return;
    }
    printk("[SENSOR] accel raw x=%d.%06d y=%d.%06d z=%d.%06d\n",
       accel[0].val1, accel[0].val2,
       accel[1].val1, accel[1].val2,
       accel[2].val1, accel[2].val2);

    int16_t ax = (int16_t)(accel[0].val1 * 100 + accel[0].val2 / 10000);
    int16_t ay = (int16_t)(accel[1].val1 * 100 + accel[1].val2 / 10000);
    int16_t az = (int16_t)(accel[2].val1 * 100 + accel[2].val2 / 10000);
    int16_t hall_value = (int16_t)hall.val1;

    proto_frame_t f = {
        .stx = PROTO_STX,
        .cmd = RSP_ALL,
        .len = 10,
        .payload = {
            (uint8_t)(visible >> 8), (uint8_t)(visible & 0xFF),
            (uint8_t)(ax >> 8),      (uint8_t)(ax & 0xFF),
            (uint8_t)(ay >> 8),      (uint8_t)(ay & 0xFF),
            (uint8_t)(az >> 8),      (uint8_t)(az & 0xFF),
            (uint8_t)(hall_value >> 8), (uint8_t)(hall_value & 0xFF)
        }
    };

    uint8_t buf[14];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);

    printk("[SENSOR] raw light=%u ax=%d ay=%d az=%d hall=%d\n",
           visible, ax, ay, az, hall_value);
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
            case CMD_READ_ALL:
                send_all();
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

static bool sensor_read_light(uint16_t *visible, uint16_t *ir)
{
    if (!ltr303_ready) {
        return false;
    }

    ltr303_raw_data_t raw;
    int ret = ltr303_read_raw(&ltr303, &raw);
    if (ret != 0) {
        printk("[SENSOR] LTR303 read failed: %d\n", ret);
        return false;
    }

    *visible = raw.ch0_visible_ir_raw;
    *ir = raw.ch1_ir_raw;
    return true;
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

    const struct device *ltr303_i2c = DEVICE_DT_GET(LTR303_I2C_NODE);

    int ret = ltr303_init(&ltr303,
                          ltr303_i2c,
                          LTR303_I2C_ADDR);

    if (ret != 0) {
        printk("[SENSOR] LTR303 init failed: %d\n", ret);
    } else {
        printk("[SENSOR] LTR303 ready\n");
        ltr303_ready = true;
    }

    if (!device_is_ready(mpu6050)) {
    printk("[SENSOR] MPU6050 not ready\n");
    } else {
    printk("[SENSOR] MPU6050 ready\n");
    }

    if (!device_is_ready(ss49e)) {
        printk("[SENSOR] SS49E not ready\n");
    } else {
        printk("[SENSOR] SS49E ready\n");
    }

    printk("[SENSOR] Listening for commands...\n");

    while (1) {
        process_rx();
        k_sleep(K_USEC(500));
    }

    return 0;
}
