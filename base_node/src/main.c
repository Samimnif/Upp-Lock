/*
 * base_node/src/main.c
 *
 * Base node — Raspberry Pi Pico (RP2040)
 * Sends commands over UART0 to the sensor node and prints responses
 * to the console (UART1 / USB CDC via printk).
 *
 * Wiring (Base ↔ Sensor):
 *   Base GP0  (UART0 TX) → Sensor GP5  (UART1 RX)
 *   Base GP1  (UART0 RX) ← Sensor GP4  (UART1 TX)
 *   GND ↔ GND
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include "../../common/protocol.h"
#include <stdlib.h>

/* ── Config ────────────────────────────────────────────── */
#define COMMS_UART_NODE   DT_NODELABEL(uart1)
#define POLL_INTERVAL_MS  2000   /* How often to request all sensor data */
#define RX_TIMEOUT_MS     500    /* Max wait for a response              */

/* ── Globals ───────────────────────────────────────────── */
static const struct device *comms_uart;

/* ── Low-level UART helpers ────────────────────────────── */

static void uart_send_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(comms_uart, buf[i]);
    }
}

/**
 * Receive exactly `len` bytes within RX_TIMEOUT_MS.
 * Returns true on success.
 */
static bool uart_recv_bytes(uint8_t *buf, size_t len)
{
    int64_t deadline = k_uptime_get() + RX_TIMEOUT_MS;
    for (size_t i = 0; i < len; ) {
        unsigned char c;
        if (uart_poll_in(comms_uart, &c) == 0) {
            buf[i++] = c;
        } else {
            if (k_uptime_get() > deadline) {
                printk("[BASE] RX timeout after %zu bytes\n", i);
                return false;
            }
            k_sleep(K_USEC(100));
        }
    }
    return true;
}

/* ── Protocol helpers ──────────────────────────────────── */

/**
 * Send a command frame and receive a response frame.
 * Returns true on success (valid response received with matching CRC).
 */
static bool send_command(proto_cmd_t cmd,
                         const uint8_t *payload, uint8_t payload_len,
                         proto_frame_t *rsp_out)
{
    /* Build command frame */
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

    /* Read response header: STX + CMD + LEN (3 bytes) */
    uint8_t hdr[3];
    uint8_t c;

/* Find STX first */
    do {
        if (!uart_recv_bytes(&c, 1)) {
            printk("[BASE] No STX\n");
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
        printk("[BASE] Payload len too large: %u\n", rsp_len);
        return false;
    }

    /* Read payload + CRC */
    uint8_t tail[PROTO_MAX_PAYLOAD + 1];
    if (!uart_recv_bytes(tail, rsp_len + 1u)) {
        printk("[BASE] Response payload timeout\n");
        return false;
    }

    /* Reassemble full frame buffer and decode */
    uint8_t full[4 + PROTO_MAX_PAYLOAD];
    full[0] = hdr[0]; full[1] = hdr[1]; full[2] = hdr[2];
    memcpy(&full[3], tail, rsp_len + 1u);

    if (!proto_decode(full, 3u + rsp_len + 1u, rsp_out)) {
        printk("[BASE] CRC mismatch\n");
        return false;
    }

    return true;
}

/* ── Command wrappers ──────────────────────────────────── */

static void do_ping(void)
{
    proto_frame_t rsp;
    printk("[BASE] → PING\n");
    if (send_command(CMD_PING, NULL, 0, &rsp)) {
        if (rsp.cmd == RSP_ACK) {
            printk("[BASE] ← PONG (ACK)\n");
        } else {
            printk("[BASE] ← Unexpected rsp 0x%02X\n", rsp.cmd);
        }
    }
}

static bool detect_movement(int16_t ax, int16_t ay, int16_t az)
{
    static int prev_x = 0;
    static int prev_y = 0;
    static int prev_z = 0;

    static int movement_counter = 0;

    int x = ax;
    int y = ay;
    int z = az;

    int dx = abs(x - prev_x);
    int dy = abs(y - prev_y);
    int dz = abs(z - prev_z);

    prev_x = x;
    prev_y = y;
    prev_z = z;

    bool moving_now = (dx > 2000 ||
                       dy > 2000 ||
                       dz > 2000);

    if (moving_now) {
        movement_counter++;
    } else {
        movement_counter = 0;
    }

    /*
     * Base polls every 2000 ms.
     * 2 samples ≈ 4 seconds.
     * If you change POLL_INTERVAL_MS to 1000,
     * use movement_counter >= 3 for about 3 seconds.
     */
    if (movement_counter >= 2) {
        return true;
    }

    return false;
}


static int16_t get_i16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void do_read_all(void)
{
    proto_frame_t rsp;

    printk("[BASE] → READ_ALL\n");

    if (send_command(CMD_READ_ALL, NULL, 0, &rsp)) {
        if (rsp.cmd == RSP_ALL && rsp.len == 10) {

            uint16_t light_raw =
                ((uint16_t)rsp.payload[0] << 8) | rsp.payload[1];

            int16_t ax = get_i16(&rsp.payload[2]);
            int16_t ay = get_i16(&rsp.payload[4]);
            int16_t az = get_i16(&rsp.payload[6]);
            int16_t hall = get_i16(&rsp.payload[8]);

            bool is_dark = light_raw < 100;
            bool movement = detect_movement(ax, ay, az);
            bool hall_triggered = (hall > 500 || hall < -500);

            printk("[BASE] raw light=%u ax=%d ay=%d az=%d hall=%d\n",
                   light_raw, ax, ay, az, hall);

            printk("[BASE] Dark=%s Movement=%s Hall=%s\n",
                   is_dark ? "YES" : "NO",
                   movement ? "YES" : "NO",
                   hall_triggered ? "TRIGGERED" : "NORMAL");

        } else {
            printk("[BASE] Unexpected READ_ALL response: cmd=0x%02X len=%u\n",
           rsp.cmd,
           rsp.len);
        }
    }
}

/*static void do_set_interval(uint16_t ms)
{
    uint8_t payload[2] = { (uint8_t)(ms >> 8), (uint8_t)(ms & 0xFF) };
    proto_frame_t rsp;
    printk("[BASE] → SET_INTERVAL %u ms\n", ms);
    if (send_command(CMD_SET_INTERVAL, payload, 2, &rsp)) {
        printk("[BASE] ← %s\n", rsp.cmd == RSP_ACK ? "ACK" : "NACK");
    }
}*/

/* ── Entry point ───────────────────────────────────────── */

int main(void)
{
    printk("=== Base Node (PICO2) starting ===\n");

    comms_uart = DEVICE_DT_GET(COMMS_UART_NODE);
    if (!device_is_ready(comms_uart)) {
        printk("[BASE] UART not ready!\n");
        return -1;
    }
    printk("[BASE] UART ready — polling sensor every %d ms\n", POLL_INTERVAL_MS);

    /* Give the sensor node time to boot */
    k_sleep(K_MSEC(500));

    /* Initial handshake */
    do_ping();
    k_sleep(K_MSEC(100));

    /* Set the sensor's auto-read interval */
    

    /* Main polling loop */
    while (1) {
        do_read_all();
        k_sleep(K_MSEC(POLL_INTERVAL_MS));
    }

    return 0;
}
