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
#include <limits.h>
#include "../../common/protocol.h"
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/led_strip.h>
#include "../../drivers/sensor/buzzer/buzzer.h"

/* ── Config ────────────────────────────────────────────── */
#define COMMS_UART_NODE  DT_NODELABEL(uart1)
#define RGB_LED_NODE     DT_ALIAS(led_strip)
#define BUZZER_NODE      DT_ALIAS(buzzer)
static const struct device *mpu6050 = DEVICE_DT_GET(DT_NODELABEL(mpu6050));
static const struct device *ss49e = DEVICE_DT_GET(DT_NODELABEL(ss49e));
static const struct device *ltr303 = DEVICE_DT_GET(DT_NODELABEL(ltr303));
static const struct device *rgb_led = DEVICE_DT_GET(RGB_LED_NODE);
static const struct device *buzzer = DEVICE_DT_GET(BUZZER_NODE);

//Check the threshold level
#define LIGHT_THRESHOLD 100

/* ── Globals ───────────────────────────────────────────── */
static const struct device *comms_uart;

static bool sensor_read_light(uint16_t *visible);

static int16_t sensor_value_to_centi_i16(const struct sensor_value *v)
{
    int64_t centi = (int64_t)v->val1 * 100 + v->val2 / 10000;

    if (centi > INT16_MAX) {
        return INT16_MAX;
    }
    if (centi < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)centi;
}


/* ── Status output helpers ─────────────────────────────── */
static bool rgb_ready;
static bool buzzer_ready;

static void status_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!rgb_ready) {
        return;
    }

    struct led_rgb pixel = {
        .r = r,
        .g = g,
        .b = b,
    };

    int ret = led_strip_update_rgb(rgb_led, &pixel, 1);
    if (ret != 0) {
        printk("[SENSOR] RGB update failed: %d\n", ret);
    }
}

static void status_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!buzzer_ready) {
        return;
    }

    int ret = buzzer_beep(buzzer, freq_hz, duration_ms);
    if (ret != 0) {
        printk("[SENSOR] Buzzer beep failed: %d\n", ret);
    }
}

static void status_startup_ok(void)
{
    status_rgb_set(0, 40, 0);
    status_beep(2000, 80);
    k_sleep(K_MSEC(80));
    status_rgb_set(0, 0, 0);
}

static void status_command_seen(void)
{
    status_rgb_set(0, 0, 40);
    //status_beep(2500, 20);
    k_sleep(K_MSEC(40));
    status_rgb_set(0, 0, 0);
}

static void status_read_ok(void)
{
    status_rgb_set(0, 40, 0);
    k_sleep(K_MSEC(40));
    status_rgb_set(0, 0, 0);
}

static void status_error(void)
{
    status_rgb_set(40, 0, 0);
    status_beep(1000, 60);
    k_sleep(K_MSEC(60));
    status_rgb_set(0, 0, 0);
}

static void status_alarm(uint32_t duration_ms)
{
    int64_t end = k_uptime_get() + duration_ms;

    printk("[SENSOR] ALARM for %u ms\n", duration_ms);

    while (k_uptime_get() < end) {
        status_rgb_set(60, 0, 0);
        status_beep(2500, 120);
        k_sleep(K_MSEC(120));
        status_rgb_set(0, 0, 0);
        status_beep(1400, 120);
        k_sleep(K_MSEC(120));
    }

    status_rgb_set(0, 0, 0);
}

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
    status_error();
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

    if (!sensor_read_light(&visible)) {
        send_error(ERR_SENSOR_FAULT);
        return;
    }

    struct sensor_value accel[3];
    struct sensor_value gyro[3];

    int ret = sensor_sample_fetch(mpu6050);
    if (ret != 0) {
        printk("[SENSOR] MPU fetch failed: %d\n", ret);
        send_error(ERR_SENSOR_FAULT);
        return;
    }

    ret = sensor_channel_get(mpu6050, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (ret != 0) {
        printk("[SENSOR] MPU accel channel get failed: %d\n", ret);
        send_error(ERR_SENSOR_FAULT);
        return;
    }

    ret = sensor_channel_get(mpu6050, SENSOR_CHAN_GYRO_XYZ, gyro);
    if (ret != 0) {
        printk("[SENSOR] MPU gyro channel get failed: %d\n", ret);
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

    int16_t ax = sensor_value_to_centi_i16(&accel[0]);
    int16_t ay = sensor_value_to_centi_i16(&accel[1]);
    int16_t az = sensor_value_to_centi_i16(&accel[2]);
    int16_t gx = sensor_value_to_centi_i16(&gyro[0]);
    int16_t gy = sensor_value_to_centi_i16(&gyro[1]);
    int16_t gz = sensor_value_to_centi_i16(&gyro[2]);
    int16_t hall_value = (int16_t)hall.val1;

    proto_frame_t f = {
        .stx = PROTO_STX,
        .cmd = RSP_ALL,
        .len = 16,
        .payload = {
            (uint8_t)(visible >> 8), (uint8_t)(visible & 0xFF),
            (uint8_t)(ax >> 8),      (uint8_t)(ax & 0xFF),
            (uint8_t)(ay >> 8),      (uint8_t)(ay & 0xFF),
            (uint8_t)(az >> 8),      (uint8_t)(az & 0xFF),
            (uint8_t)(gx >> 8),      (uint8_t)(gx & 0xFF),
            (uint8_t)(gy >> 8),      (uint8_t)(gy & 0xFF),
            (uint8_t)(gz >> 8),      (uint8_t)(gz & 0xFF),
            (uint8_t)(hall_value >> 8), (uint8_t)(hall_value & 0xFF)
        }
    };

    uint8_t buf[20];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    uart_send_bytes(buf, n);

    printk("[SENSOR] raw light=%u acc=(%d,%d,%d) gyro=(%d,%d,%d) hall=%d\n",
           visible, ax, ay, az, gx, gy, gz, hall_value);

    status_read_ok();
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
            status_command_seen();
            switch ((proto_cmd_t)frame.cmd) {
            case CMD_PING:
                send_ack();
                break;
            case CMD_READ_ALL:
                send_all();
                break;
            case CMD_SOUND_ALARM: {
                uint16_t duration_ms = 1000;
                if (frame.len >= 2) {
                    duration_ms = ((uint16_t)frame.payload[0] << 8) | frame.payload[1];
                }
                send_ack();
                status_alarm(duration_ms);
                break;
            }
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

static bool sensor_read_light(uint16_t *visible)
{
    struct sensor_value light;
    int ret;

    if (!device_is_ready(ltr303)) {
        printk("[SENSOR] LTR303 not ready\n");
        return false;
    }

    ret = sensor_sample_fetch(ltr303);
    if (ret != 0) {
        printk("[SENSOR] LTR303 fetch failed: %d\n", ret);
        return false;
    }

    ret = sensor_channel_get(ltr303, SENSOR_CHAN_LIGHT, &light);
    if (ret != 0) {
        printk("[SENSOR] LTR303 channel get failed: %d\n", ret);
        return false;
    }

    *visible = (uint16_t)light.val1;
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

    rgb_ready = device_is_ready(rgb_led);
    if (!rgb_ready) {
        printk("[SENSOR] RGB LED not ready\n");
    } else {
        printk("[SENSOR] RGB LED ready\n");
        status_rgb_set(0, 0, 0);
    }

    buzzer_ready = device_is_ready(buzzer);
    if (!buzzer_ready) {
        printk("[SENSOR] Buzzer not ready\n");
    } else {
        printk("[SENSOR] Buzzer ready\n");
    }

    status_startup_ok();

    /* Enable interrupt-driven RX */
    uart_irq_callback_set(comms_uart, uart_cb);
    uart_irq_rx_enable(comms_uart);

    if (!device_is_ready(ltr303)) {
        printk("[SENSOR] LTR303 not ready\n");
    } else {
        printk("[SENSOR] LTR303 ready\n");
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
