# MPU6050 Driver

This package is the MPU6050 sensor driver that will be used in the main program

## Wiring
```
MPU6050 VCC  -> 3.3V
MPU6050 GND  -> GND
MPU6050 SDA  -> Pico SDA / I2C0 SDA
MPU6050 SCL  -> Pico SCL / I2C0 SCL
MPU6050 AD0  -> GND for address 0x68
```

## Build and flash
```bash
west build -b rpi_pico2/rp2350a/m33 . --pristine
west flash
or
west flash --openocd /usr/local/bin/openocd
```

## Reading Results
```bash
minicom -D /dev/ttyACM0 -b 115200
```