# Pico Base ↔ Sensor UART Project (Zephyr)

Two-board Zephyr project: an RP2040 Pico acts as the **base node** that sends
commands, and an RP2350 Pico 2 acts as the **sensor node** that responds.

---

## Hardware Wiring

```
  RP2040 Pico (Base)          RP2350 Pico 2 (Sensor)
  ─────────────────           ──────────────────────
  GP0  (UART0 TX) ──────────► GP5  (UART1 RX)
  GP1  (UART0 RX) ◄────────── GP4  (UART1 TX)
  GND             ─────────── GND
```

> Both boards run at **3.3 V logic** — no level shifter needed.

---

## Communication Protocol

Custom framing over **UART at 115200 baud**.

### Frame Format

| Byte | Field   | Description                              |
|------|---------|------------------------------------------|
| 0    | STX     | `0x02` — start of frame marker           |
| 1    | CMD/RSP | Command (base→sensor) or Response code   |
| 2    | LEN     | Payload length (0–16)                    |
| 3..N | Payload | Optional data                            |
| N+1  | CRC8    | CRC-8/Maxim over bytes 1..N              |

### Commands (Base → Sensor)

| Code | Name             | Payload      | Description                        |
|------|------------------|--------------|------------------------------------|
| 0x01 | CMD_PING         | —            | Sensor replies ACK                 |
| 0x02 | CMD_READ_TEMP    | —            | Sensor replies RSP_TEMP            |
| 0x03 | CMD_READ_HUMIDITY| —            | Sensor replies RSP_HUMIDITY        |
| 0x04 | CMD_READ_ALL     | —            | Sensor replies RSP_ALL             |
| 0x05 | CMD_SET_INTERVAL | uint16_t ms  | Set auto-read interval             |
| 0xFF | CMD_RESET        | —            | Sensor soft-resets                 |

### Responses (Sensor → Base)

| Code | Name         | Payload                    | Description           |
|------|--------------|----------------------------|-----------------------|
| 0x80 | RSP_ACK      | —                          | Command accepted      |
| 0x81 | RSP_NACK     | —                          | Command rejected      |
| 0x82 | RSP_TEMP     | int16_t (°C × 100)         | Temperature reading   |
| 0x83 | RSP_HUMIDITY | uint16_t (% × 100)         | Humidity reading      |
| 0x84 | RSP_ALL      | int16_t temp + uint16_t hum| Both readings         |
| 0xFF | RSP_ERROR    | uint8_t error_code         | Error details         |

---

## Project Structure

```
pico-sensor-project/
├── common/
│   └── protocol.h          ← Shared frame definitions, CRC, encode/decode
├── base_node/
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── boards/
│   │   └── rpi_pico.overlay
│   └── src/
│       └── main.c
└── sensor_node/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── boards/
    │   └── rpi_pico2.overlay
    └── src/
        └── main.c
```

---

## Building & Flashing

Make sure your Zephyr environment is activated (`west` available).

### Base Node (RP2040 Pico)

```bash
cd base_node
west build -b rpi_pico .
west flash
```

### Sensor Node (RP2350 Pico 2)

```bash
cd sensor_node
west build -b rpi_pico2 .
west flash
```

> **Flash method:** Hold BOOTSEL, plug USB, release. The board mounts as a
> drive. `west flash` copies the `.uf2` automatically via Zephyr's UF2 runner,
> or you can copy `build/zephyr/zephyr.uf2` manually.

---

## Viewing Output

Each board prints debug info over its USB serial port (115200 baud).

```bash
# Linux/macOS
screen /dev/ttyACM0 115200

# Or with minicom
minicom -D /dev/ttyACM0 -b 115200
```

Expected base node output:
```
=== Base Node (RP2040) starting ===
[BASE] UART ready — polling sensor every 2000 ms
[BASE] → PING
[BASE] ← PONG (ACK)
[BASE] → SET_INTERVAL 1000 ms
[BASE] ← ACK
[BASE] → READ_ALL
[BASE] ← Temp: 23.50 °C  Humidity: 55.00%
```

---

## Replacing the Simulated Sensor

In `sensor_node/src/main.c`, replace the bodies of `sensor_read_temp()` and
`sensor_read_humidity()` with your actual sensor driver calls (e.g., DHT22,
SHT3x via I2C/SPI). The rest of the protocol stack stays unchanged.

Example for an I2C sensor:

```c
static bool sensor_read_temp(int16_t *out_temp)
{
    // Replace with: sht3x_fetch_sample(&dev); sht3x_channel_get(&dev, ...);
    struct sensor_value val;
    if (sensor_sample_fetch(my_sensor_dev) < 0) return false;
    sensor_channel_get(my_sensor_dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
    *out_temp = (int16_t)(val.val1 * 100 + val.val2 / 10000);
    return true;
}
```

---

## Extending the Protocol

1. Add a new `CMD_*` entry in `common/protocol.h`
2. Add the corresponding `RSP_*` if needed
3. In `sensor_node/src/main.c`, add a `case CMD_YOUR_CMD:` in the switch
4. In `base_node/src/main.c`, add a `do_your_command()` wrapper
