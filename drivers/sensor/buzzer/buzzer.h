#ifndef GPIO_BUZZER_H
#define GPIO_BUZZER_H

#include <zephyr/device.h>
#include <stdint.h>

int buzzer_on(const struct device *dev);
int buzzer_off(const struct device *dev);
int buzzer_beep(const struct device *dev, uint32_t freq_hz, uint32_t duration_ms);

#endif
