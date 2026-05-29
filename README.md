# Upp-Lock Project

**Goal:** No more cycles going missing in Uppsala!

Upp-Lock is a smart bicycle anti-theft system developed using Raspberry Pi Pico boards and the Zephyr RTOS. The system consists of a sensor node mounted on the bicycle and a base node that monitors the bicycle state, receives sensor data, and triggers alarms when theft is detected.

---

# System Architecture

## Sensor Node

The sensor node continuously monitors:

* MPU6050 accelerometer and gyroscope
* LTR303 ambient light sensor
* SS49E Hall effect sensor
* WS2812 RGB status LED
* Piezo buzzer

The node communicates sensor data to the base node using UART.

## Base Node

The base node:

* Receives sensor data from the sensor node
* Allows arming and disarming using a push button
* Displays live sensor values on a GC9A01 round LCD display
* Indicates system status using an RGB LED
* Triggers alarms when motion thresholds are exceeded

---

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

I2C Address:

```text
0x29
```

---

## MPU6050 Accelerometer/Gyroscope

Connected over I2C0.

| Signal | Pico Pin |
| ------ | -------- |
| SDA    | GP4      |
| SCL    | GP5      |
| VCC    | 3.3V     |
| GND    | GND      |

I2C Address:

```text
0x68
```

---

## SS49E / KY-035 Hall Sensor

Analog output connected to ADC.

| Signal | Pico Pin    |
| ------ | ----------- |
| OUT    | GP26 (ADC0) |
| VCC    | 3.3V        |
| GND    | GND         |

---

## WS2812 RGB LED

| Signal | Pico Pin |
| ------ | -------- |
| DIN    | GP16     |
| VCC    | 5V       |
| GND    | GND      |

---

## Buzzer

| Signal | Pico Pin |
| ------ | -------- |
| SIG    | GP22     |
| VCC    | 3.3V     |
| GND    | GND      |

---

# Software Architecture

## Custom Zephyr Drivers

The following sensors use custom drivers developed specifically for this project:

### MPU6050 Driver

Provides:

* Accelerometer readings
* Gyroscope readings
* Temperature readings

Implemented using the Zephyr Sensor API.

### LTR303 Driver

Provides:

* Ambient light measurements

Implemented using the Zephyr Sensor API.

### SS49E Driver

Provides:

* Hall effect / magnetic field measurements
* Analog ADC sampling

Implemented using the Zephyr Sensor API.

### Buzzer Driver

Custom driver used to generate audible alarm signals.

---

## Built-in Zephyr Drivers

The project also uses:

* Zephyr UART driver
* Zephyr I2C driver
* Zephyr ADC driver
* Zephyr LED Strip driver (WS2812)
* Zephyr Display driver (GC9A01)

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

```text
10 bytes
```

---

# Alarm Logic

When the system is armed:

1. Sensor node continuously samples all sensors.
2. Motion thresholds are evaluated using MPU6050 readings.
3. Sensor data is sent to the base node.
4. Base node determines whether theft activity is occurring.
5. Visual and audible alarms are activated.

## Status Colors

| Color | State           |
| ----- | --------------- |
| Green | Disarmed        |
| Blue  | Armed           |
| Red   | Alarm Triggered |

---

# Display Interface

The GC9A01 round display shows:

* Armed / Disarmed state
* Accelerometer values
* Gyroscope values
* Hall sensor status
* Light sensor value
* Alarm state

---

# Current Status

## Working

* UART communication between sensor node and base node
* MPU6050 custom Zephyr driver
* LTR303 custom Zephyr driver
* SS49E custom Zephyr driver
* Sensor data acquisition
* Arm/disarm functionality
* RGB LED status indication
* Buzzer alarm
* GC9A01 display integration
* Live sensor monitoring on display

## In Progress

* Motion threshold tuning
* Theft detection optimization
* Interrupt-based alarm triggering
* Protocol extensions for configuration commands

---

# Build Notes

## Build Sensor Node

```bash
west build -b rpi_pico2/rp2350a/m33 sensor_node -d build_sensor -p always
```

## Build Base Node

```bash
west build -b rpi_pico2/rp2350a/m33 base_node -d build_base -p always
```

---

# Flashing

## Debug Probe

```bash
west flash
```

## UF2 Bootloader

```bash
west flash --runner uf2
```

---

# Course Requirements

This project was developed for the Embedded Systems course at Uppsala University.

Implemented requirements:

* Multiple sensors
* Analog sensor (SS49E Hall sensor)
* Custom Zephyr sensor drivers
* UART communication between nodes
* Sensor API integration
* Visual and audible feedback
* Interrupt-capable architecture
* Demonstration application running on Zephyr RTOS

---

# Authors

Upp-Lock Team

Uppsala University – Embedded Systems Project
