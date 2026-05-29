/*
 * base_node/src/main.c
 *
 * Base node — Raspberry Pi Pico 2 (RP2350)
 * - Talks to the sensor node over UART1
 * - GP20 button toggles ARMED / DISARMED using a GPIO interrupt callback
 * - WS2812 RGB LED on GP28 shows state:
 *      Blue  = DISARMED
 *      Green = ARMED
 *      Red   = ALARM / theft detected
 * - When ARMED, accelerometer, gyro, or hall-effect changes trigger alarm command
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/printk.h>
#include "../../common/protocol.h"
#include "base_display.h"

#define COMMS_UART_NODE   DT_NODELABEL(uart1)
#define RGB_LED_NODE      DT_ALIAS(led_strip)
#define ARM_BUTTON_NODE   DT_ALIAS(sw0)

#define POLL_INTERVAL_MS  1000
#define RX_TIMEOUT_MS     500
#define ALARM_TIME_MS     1200
#define BUTTON_DEBOUNCE_MS 250

/* Tune these after seeing your real printed values. */
#define ACCEL_DELTA_THRESHOLD   60
#define GYRO_ABS_THRESHOLD      20
#define HALL_ABS_THRESHOLD      500

static const struct device *comms_uart;
static const struct device *rgb_led = DEVICE_DT_GET(RGB_LED_NODE);
static const struct gpio_dt_spec arm_button = GPIO_DT_SPEC_GET(ARM_BUTTON_NODE, gpios);
static struct gpio_callback arm_button_cb_data;

static volatile bool button_event;
static int64_t last_button_ms;
static bool armed;
static bool alarm_active;
static int64_t alarm_until_ms;

static bool rgb_ready;

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
        printk("[BASE] RGB update failed: %d\n", ret);
    }
}

static void update_state_led(void)
{
    if (alarm_active) {
        status_rgb_set(50, 0, 0);      /* red */
    } else if (armed) {
        status_rgb_set(0, 50, 0);      /* green */
    } else {
        status_rgb_set(0, 0, 40);      /* blue */
    }
}

static void arm_button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int64_t now = k_uptime_get();

    /* Simple ISR-side debounce. The main loop does the actual state change. */
    if ((now - last_button_ms) >= BUTTON_DEBOUNCE_MS) {
        last_button_ms = now;
        button_event = true;
    }
}

static void handle_button_event(void)
{
    if (!button_event) {
        return;
    }

    button_event = false;
    armed = !armed;
    alarm_active = false;

    printk("[BASE] System state: %s\n", armed ? "ARMED" : "DISARMED");
    update_state_led();
    struct base_display_values v = { .armed = armed, .alarm_active = alarm_active };
    base_display_update(&v);
}

static void uart_send_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(comms_uart, buf[i]);
    }
}

static bool uart_recv_bytes(uint8_t *buf, size_t len)
{
    int64_t deadline = k_uptime_get() + RX_TIMEOUT_MS;

    for (size_t i = 0; i < len;) {
        unsigned char c;
        if (uart_poll_in(comms_uart, &c) == 0) {
            buf[i++] = c;
        } else {
            if (k_uptime_get() > deadline) {
                return false;
            }
            k_sleep(K_USEC(100));
        }
    }

    return true;
}

static bool send_command(proto_cmd_t cmd,
                         const uint8_t *payload, uint8_t payload_len,
                         proto_frame_t *rsp_out)
{
    proto_frame_t cmd_frame = {
        .stx = PROTO_STX,
        .cmd = (uint8_t)cmd,
        .len = payload_len,
    };

    if (payload_len > 0 && payload != NULL) {
        memcpy(cmd_frame.payload, payload, payload_len);
    }

    uint8_t tx_buf[4 + PROTO_MAX_PAYLOAD];
    size_t tx_len = proto_encode(&cmd_frame, tx_buf, sizeof(tx_buf));
    if (tx_len == 0) {
        printk("[BASE] Encode error\n");
        return false;
    }

    uart_send_bytes(tx_buf, tx_len);

    uint8_t hdr[3];
    uint8_t c;

    do {
        if (!uart_recv_bytes(&c, 1)) {
            printk("[BASE] No response/STX\n");
            return false;
        }
    } while (c != PROTO_STX);

    hdr[0] = c;

    if (!uart_recv_bytes(&hdr[1], 2)) {
        printk("[BASE] Incomplete response header\n");
        return false;
    }

    uint8_t rsp_len = hdr[2];
    if (rsp_len > PROTO_MAX_PAYLOAD) {
        printk("[BASE] Response payload too large: %u\n", rsp_len);
        return false;
    }

    uint8_t tail[PROTO_MAX_PAYLOAD + 1];
    if (!uart_recv_bytes(tail, rsp_len + 1u)) {
        printk("[BASE] Response payload timeout\n");
        return false;
    }

    uint8_t full[4 + PROTO_MAX_PAYLOAD];
    full[0] = hdr[0];
    full[1] = hdr[1];
    full[2] = hdr[2];
    memcpy(&full[3], tail, rsp_len + 1u);

    if (!proto_decode(full, 3u + rsp_len + 1u, rsp_out)) {
        printk("[BASE] Bad response CRC\n");
        return false;
    }

    return true;
}

static int16_t get_i16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static bool send_alarm_command(void)
{
    proto_frame_t rsp;

    printk("[BASE] -> SOUND_ALARM\n");
    if (!send_command(CMD_SOUND_ALARM, NULL, 0, &rsp)) {
        printk("[BASE] Alarm command failed\n");
        return false;
    }

    if (rsp.cmd != RSP_ACK) {
        printk("[BASE] Alarm command unexpected response: 0x%02X\n", rsp.cmd);
        return false;
    }

    return true;
}

static bool detect_accel_theft(int16_t ax, int16_t ay, int16_t az)
{
    static bool have_prev;
    static int16_t prev_ax;
    static int16_t prev_ay;
    static int16_t prev_az;

    if (!have_prev) {
        prev_ax = ax;
        prev_ay = ay;
        prev_az = az;
        have_prev = true;
        return false;
    }

    int dx = abs(ax - prev_ax);
    int dy = abs(ay - prev_ay);
    int dz = abs(az - prev_az);

    prev_ax = ax;
    prev_ay = ay;
    prev_az = az;

    return (dx > ACCEL_DELTA_THRESHOLD ||
            dy > ACCEL_DELTA_THRESHOLD ||
            dz > ACCEL_DELTA_THRESHOLD);
}

static bool detect_gyro_theft(int16_t gx, int16_t gy, int16_t gz)
{
    return (abs(gx) > GYRO_ABS_THRESHOLD ||
            abs(gy) > GYRO_ABS_THRESHOLD ||
            abs(gz) > GYRO_ABS_THRESHOLD);
}

static bool detect_hall_theft(int16_t hall)
{
    return (hall > HALL_ABS_THRESHOLD || hall < -HALL_ABS_THRESHOLD);
}

static void trigger_alarm_if_needed(bool theft)
{
    if (!armed || !theft || alarm_active) {
        return;
    }

    printk("[BASE] THEFT DETECTED while ARMED\n");
    alarm_active = true;
    alarm_until_ms = k_uptime_get() + ALARM_TIME_MS;
    update_state_led();
    send_alarm_command();
}

static void handle_alarm_timeout(void)
{
    if (alarm_active && k_uptime_get() >= alarm_until_ms) {
        alarm_active = false;
        update_state_led();
        struct base_display_values v = { .armed = armed, .alarm_active = alarm_active };
        base_display_update(&v);
    }
}

static void do_ping(void)
{
    proto_frame_t rsp;
    printk("[BASE] -> PING\n");

    if (send_command(CMD_PING, NULL, 0, &rsp)) {
        printk("[BASE] <- %s\n", rsp.cmd == RSP_ACK ? "ACK" : "unexpected response");
    }
}

static void do_read_all(void)
{
    proto_frame_t rsp;

    if (!send_command(CMD_READ_ALL, NULL, 0, &rsp)) {
        return;
    }

    if (rsp.cmd != RSP_ALL || rsp.len != 16) {
        printk("[BASE] Unexpected READ_ALL response: cmd=0x%02X len=%u\n", rsp.cmd, rsp.len);
        return;
    }

    uint16_t light_raw = ((uint16_t)rsp.payload[0] << 8) | rsp.payload[1];
    int16_t ax = get_i16(&rsp.payload[2]);
    int16_t ay = get_i16(&rsp.payload[4]);
    int16_t az = get_i16(&rsp.payload[6]);
    int16_t gx = get_i16(&rsp.payload[8]);
    int16_t gy = get_i16(&rsp.payload[10]);
    int16_t gz = get_i16(&rsp.payload[12]);
    int16_t hall = get_i16(&rsp.payload[14]);

    bool accel_theft = detect_accel_theft(ax, ay, az);
    bool gyro_theft = detect_gyro_theft(gx, gy, gz);
    bool hall_theft = detect_hall_theft(hall);
    bool theft = accel_theft || gyro_theft || hall_theft;

    struct base_display_values display_values = {
        .armed = armed,
        .alarm_active = alarm_active,
        .accel_theft = accel_theft,
        .gyro_theft = gyro_theft,
        .hall_theft = hall_theft,
        .light_raw = light_raw,
        .ax = ax, .ay = ay, .az = az,
        .gx = gx, .gy = gy, .gz = gz,
        .hall = hall,
    };
    base_display_update(&display_values);

    printk("[BASE] %s light=%u acc=(%d,%d,%d) gyro=(%d,%d,%d) hall=%d\n",
           armed ? "ARMED" : "DISARMED",
           light_raw, ax, ay, az, gx, gy, gz, hall);

    printk("[BASE] theft flags: accel=%s gyro=%s hall=%s\n",
           accel_theft ? "YES" : "no",
           gyro_theft ? "YES" : "no",
           hall_theft ? "YES" : "no");

    trigger_alarm_if_needed(theft);
}

static int setup_button(void)
{
    if (!gpio_is_ready_dt(&arm_button)) {
        printk("[BASE] GP20 arm button GPIO not ready\n");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&arm_button, GPIO_INPUT);
    if (ret != 0) {
        printk("[BASE] Button configure failed: %d\n", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&arm_button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("[BASE] Button interrupt configure failed: %d\n", ret);
        return ret;
    }

    gpio_init_callback(&arm_button_cb_data, arm_button_isr, BIT(arm_button.pin));
    ret = gpio_add_callback(arm_button.port, &arm_button_cb_data);
    if (ret != 0) {
        printk("[BASE] Button callback add failed: %d\n", ret);
        return ret;
    }

    printk("[BASE] GP20 arm/disarm button ready using GPIO interrupt\n");
    return 0;
}

int main(void)
{
    printk("=== Base Node starting ===\n");

    comms_uart = DEVICE_DT_GET(COMMS_UART_NODE);
    if (!device_is_ready(comms_uart)) {
        printk("[BASE] UART not ready\n");
        return -1;
    }

    rgb_ready = device_is_ready(rgb_led);
    if (!rgb_ready) {
        printk("[BASE] RGB LED not ready\n");
    } else {
        printk("[BASE] RGB LED ready\n");
    }

    if (setup_button() != 0) {
        printk("[BASE] Continuing without button\n");
    }

    if (base_display_init() != 0) {
        printk("[BASE] Continuing without display\n");
    }

    armed = false;
    alarm_active = false;
    update_state_led();

    k_sleep(K_MSEC(500));
    do_ping();

    while (1) {
        handle_button_event();
        handle_alarm_timeout();
        do_read_all();
        handle_button_event();
        handle_alarm_timeout();
        k_sleep(K_MSEC(POLL_INTERVAL_MS));
    }

    return 0;
}
