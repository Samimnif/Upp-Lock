#ifndef BASE_DISPLAY_H
#define BASE_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

struct base_display_values {
    bool armed;
    bool alarm_active;
    bool accel_theft;
    bool gyro_theft;
    bool hall_theft;
    uint16_t light_raw;
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
    int16_t hall;
};

int base_display_init(void);
void base_display_update(const struct base_display_values *v);

#endif /* BASE_DISPLAY_H */
