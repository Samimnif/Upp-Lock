# Upp-Lock Project

Goal: no more cycles going missing in Uppsala!

# Wiring

## UART Communication

### Sensor Node ↔ Base Node

| Sensor Node    | Base Node      |
| -------------- | -------------- |
| GP8 (UART1 TX) | GP5 (UART1 RX) |
| GP9 (UART1 RX) | GP4 (UART1 TX) |
| GND            | GND            |

---

# Sensor Connections

## LTR303 Light Sensor

Connected over I2C1.

| Signal | Pico Pin |
| ------ | -------- |
| SDA    | GP2      |
| SCL    | GP3      |
| VCC    | 3.3V     |
| GND    | GND      |

I2C address:

* `0x29`

---

## MPU6050 Accelerometer/Gyroscope

Connected over I2C0.

| Signal | Pico Pin |
| ------ | -------- |
| SDA    | GP4      |
| SCL    | GP5      |
| VCC    | 3.3V     |
| GND    | GND      |

I2C address:

* `0x68`

---

## SS49E / KY-035 Hall Sensor

Analog output connected to ADC.

| Signal | Pico Pin    |
| ------ | ----------- |
| OUT    | GP26 (ADC0) |
| VCC    | 3.3V        |
| GND    | GND         |

---

# UART Protocol

Current `RSP_ALL` payload format:

| Bytes | Data              |
| ----- | ----------------- |
| 0-1   | Light raw value   |
| 2-3   | Accelerometer X   |
| 4-5   | Accelerometer Y   |
| 6-7   | Accelerometer Z   |
| 8-9   | Hall sensor value |

Total payload length:

* 10 bytes

---

# Current Status

Working:

* UART communication
* All three sensors are integrated (but LTR303 was not connected with the DT)

In Progress:

* MPU6050 movement state logic
* Theft detection state machine
* Buzzer and LED integration
* Driver for LTR303 needs to be rewritten

---

# Build Notes

Build sensor node:

```bash
west build -b rpi_pico2/rp2350a/m33 sensor_node -d build_sensor -p always
```

Build base node:

```bash
west build -b rpi_pico2/rp2350a/m33 base_node -d build_base -p always
```

